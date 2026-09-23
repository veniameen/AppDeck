// appdeck-proxy: the local bridge between an app and an upstream proxy that wants a login.
// Chromium and Electron accept --proxy-server but cannot send proxy credentials, so AppDeck starts this
// helper, points the app at 127.0.0.1:<port> as a plain HTTP proxy, and the helper forwards every request
// through the configured HTTP or SOCKS5 upstream, adding the login and password on the way.
// A separate executable that links libSystem only (no AppKit, no C++ runtime). The protocol rules live in
// proxy_policy.hpp (tests/proxy_test.cpp); tests/proxy_bridge_test.sh runs this binary against offline
// loopback fixtures.
//
// The configuration arrives on stdin, never in argv or the environment:
//   scheme http|socks5
//   endpoint <host> <port> <user-hex|-> <pass-hex|->    1-32 lines, in failover order; hex of UTF-8 bytes
//   <empty line>                                        (end of input also ends it)
// appdeck-proxy serve  prints "port <n>" and closes stdout, then expects "watch <pid>" within 60 s and
//                      serves until that process exits. Stdin is ignored after the watch line.
// appdeck-proxy check  opens a tunnel to api.openai.com:443 (APPDECK_PROXY_PROBE=host:port in tests)
//                      through each endpoint in turn and prints "ok <i> <ms>", "auth <i>" or
//                      "fail <i> <reason>". Exit 0 when any endpoint is ok, else 1.
// Exit 2: bad usage or configuration, with one line on stderr that never contains a credential.
#include "proxy_policy.hpp"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

namespace {
using deck::Size;
constexpr int maxEndpoints=32,maxClients=512,maxAddresses=4;
constexpr int connectMs=6000,handshakeMs=10000,headMs=30000,replyMs=5000,watchMs=60000,configMs=60000;
constexpr int halfClosedMs=300000; // a tunnel with one direction finished closes after 5 idle minutes
constexpr Size headCap=16384,relayCap=32768,lineCap=4096;

deck::ProxyScheme gScheme=deck::ProxyScheme::Http;
deck::ProxyEndpoint gEndpoints[maxEndpoints];
int gCount=0;
int gLast=0;    // the endpoint that last opened a tunnel (atomic access)
int gClients=0; // connections being served (atomic access)

const char badRequest[]="HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
const char badGateway[]="HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
const char established[]="HTTP/1.1 200 Connection established\r\n\r\n";

long long nowMs(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
int leftMs(long long deadline){long long d=deadline-nowMs();return d<0?0:d>INT32_MAX?INT32_MAX:(int)d;}
void wipe(void* p,Size n){volatile unsigned char* q=(volatile unsigned char*)p;while(n--)*q++=0;}
void nonBlocking(int fd){int f=fcntl(fd,F_GETFL);if(f>=0)fcntl(fd,F_SETFL,f|O_NONBLOCK);fcntl(fd,F_SETFD,FD_CLOEXEC);}
void tune(int fd){int one=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);}
// Upstream sockets notice a vanished peer within a few minutes; tunnels themselves have no idle limit.
void keepAlive(int fd){int one=1,idle=60,interval=15,count=4;setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,&one,sizeof one);
 setsockopt(fd,IPPROTO_TCP,TCP_KEEPALIVE,&idle,sizeof idle);setsockopt(fd,IPPROTO_TCP,TCP_KEEPINTVL,&interval,sizeof interval);setsockopt(fd,IPPROTO_TCP,TCP_KEEPCNT,&count,sizeof count);}
void abortive(int fd){linger l={1,0};setsockopt(fd,SOL_SOCKET,SO_LINGER,&l,sizeof l);} // close() then sends RST
bool writeAll(int fd,const char* s,Size n){while(n){ssize_t w=write(fd,s,n);if(w>0){s+=w;n-=(Size)w;}else if(w<0&&errno==EINTR)continue;else return false;}return true;}
void devNull(int fd){int n=open("/dev/null",O_RDWR);if(n>=0){dup2(n,fd);if(n!=fd)close(n);}}

// 1 ready, 0 deadline passed, -1 error.
int waitFor(int fd,short events,long long deadline){
 for(;;){pollfd p={fd,events,0};int r=poll(&p,1,leftMs(deadline));if(r>0)return 1;if(r==0)return 0;if(errno!=EINTR)return -1;}
}
// 1 sent, 0 deadline passed, -1 error (non-blocking socket).
int sendAll(int fd,const void* data,Size n,long long deadline){
 const char* p=(const char*)data;
 while(n){ssize_t w=write(fd,p,n);if(w>0){p+=w;n-=(Size)w;continue;}if(w<0&&errno==EINTR)continue;
  if(w<0&&errno==EAGAIN){int r=waitFor(fd,POLLOUT,deadline);if(r<=0)return r;continue;}return -1;}
 return 1;
}
// Bytes read, 0 at end of stream, -1 error, -2 deadline passed (non-blocking socket).
long recvSome(int fd,void* buf,Size cap,long long deadline){
 for(;;){ssize_t r=read(fd,buf,cap);if(r>=0)return r;if(errno==EINTR)continue;if(errno!=EAGAIN)return -1;
  int w=waitFor(fd,POLLIN,deadline);if(w==0)return -2;if(w<0)return -1;}
}

// ---- stdin lines ----
struct LineIn{char buf[8192];Size have;bool eof;};
LineIn gIn;
// 1 line (without "\r\n"), 0 end of input, -1 deadline passed, -2 line too long.
int readLine(char* out,Size cap,long long deadline){
 for(;;){
  for(Size i=0;i<gIn.have;++i)if(gIn.buf[i]=='\n'){
   Size n=i;if(n&&gIn.buf[n-1]=='\r')--n;if(n>=cap)return -2;memcpy(out,gIn.buf,n);out[n]=0;
   memmove(gIn.buf,gIn.buf+i+1,gIn.have-i-1);gIn.have-=i+1;return 1;}
  if(gIn.eof){if(!gIn.have)return 0;Size n=gIn.have;if(n>=cap)return -2;memcpy(out,gIn.buf,n);out[n]=0;gIn.have=0;return 1;}
  if(gIn.have==sizeof gIn.buf)return -2;
  pollfd p={0,POLLIN,0};int r=poll(&p,1,leftMs(deadline));
  if(r==0)return -1;if(r<0&&errno==EINTR)continue;
  ssize_t got=r<0?-1:read(0,gIn.buf+gIn.have,sizeof gIn.buf-gIn.have);
  if(got>0)gIn.have+=(Size)got;else if(got<0&&(errno==EINTR||errno==EAGAIN))continue;else gIn.eof=true;
 }
}
int split(char* s,char** tok,int cap){int n=0;while(*s){while(*s==' '||*s=='\t')*s++=0;if(!*s)break;if(n==cap)return cap+1;tok[n++]=s;while(*s&&*s!=' '&&*s!='\t')++s;}return n;}
int hexDigit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
// "-" (none) or the hex of 1-255 bytes without NUL.
bool hexField(const char* s,char* out){
 if(!strcmp(s,"-")){out[0]=0;return true;}Size n=strlen(s);if(!n||n%2||n/2>=deck::proxyFieldCap)return false;
 for(Size i=0;i<n;i+=2){int hi=hexDigit(s[i]),lo=hexDigit(s[i+1]);if(hi<0||lo<0||!(hi|lo))return false;out[i/2]=(char)(hi<<4|lo);}
 out[n/2]=0;return true;
}
bool parseEndpoint(char** t,deck::ProxyEndpoint& ep){
 // Host and port go through the same rules as the owner's endpoint lines; an IPv6 literal gets brackets.
 char spec[300];bool v6=strchr(t[1],':')&&t[1][0]!='[';
 int n=snprintf(spec,sizeof spec,v6?"[%s]:%s":"%s:%s",t[1],t[2]);
 if(n<0||n>=(int)sizeof spec||!deck::proxyParse(spec,ep)||ep.user[0])return false;
 if(!hexField(t[3],ep.user)||!hexField(t[4],ep.pass))return false;
 return ep.user[0]||!ep.pass[0]; // a password needs a login; a login may have an empty password
}
const char* readConfig(char* msg,Size cap){
 bool scheme=false;int lineNo=0;char line[lineCap];const char* why=nullptr;long long deadline=nowMs()+configMs;
 for(;;){
  int r=readLine(line,sizeof line,deadline);
  if(r==-1){why="timed out waiting for the configuration";break;}
  if(r==-2){why="configuration line too long";break;}
  if(r==0||!line[0])break;
  ++lineNo;char* tok[6];int n=split(line,tok,5);
  if(n==2&&!strcmp(tok[0],"scheme")){
   if(scheme||!deck::proxySchemeParse(tok[1],gScheme)){snprintf(msg,cap,"line %d: bad or repeated scheme",lineNo);why=msg;break;}scheme=true;}
  else if(n==5&&!strcmp(tok[0],"endpoint")){
   if(gCount==maxEndpoints){snprintf(msg,cap,"line %d: more than %d endpoints",lineNo,maxEndpoints);why=msg;break;}
   if(!parseEndpoint(tok,gEndpoints[gCount])){wipe(&gEndpoints[gCount],sizeof gEndpoints[gCount]);snprintf(msg,cap,"line %d: malformed endpoint",lineNo);why=msg;break;}
   ++gCount;}
  else{snprintf(msg,cap,"line %d: unknown directive",lineNo);why=msg;break;}
 }
 wipe(line,sizeof line);
 if(!why&&!scheme)why="no scheme line";
 if(!why&&!gCount)why="no endpoint line";
 return why;
}

// ---- upstream tunnels ----
enum class Why{Ok,Resolve,Connect,Timeout,Refused,Protocol,Status,Socks,Auth};
struct Outcome{Why why;int code;};
// Bytes received from the upstream during a handshake; what follows the handshake goes to the app.
struct Inbox{unsigned char buf[headCap];Size have;};
void take(Inbox& in,Size n){memmove(in.buf,in.buf+n,in.have-n);in.have-=n;}
// 1 when at least `need` bytes are buffered, 0 at end of stream, -1 error or overflow, -2 deadline passed.
long fill(int fd,Inbox& in,Size need,long long deadline){
 while(in.have<need){if(in.have==sizeof in.buf)return -1;long r=recvSome(fd,in.buf+in.have,sizeof in.buf-in.have,deadline);if(r<=0)return r;in.have+=(Size)r;}
 return 1;
}
Outcome failed(long r){return {r==-2?Why::Timeout:Why::Protocol,0};} // a closed or reset handshake is a protocol failure

// TCP connection to an upstream: every resolved address in turn (at most four), 6 s each.
int dial(const char* host,unsigned port,Why& why){
 addrinfo hints;memset(&hints,0,sizeof hints);hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;hints.ai_flags=AI_NUMERICSERV;
 char service[8];snprintf(service,sizeof service,"%u",port);addrinfo* list=nullptr;
 if(getaddrinfo(host,service,&hints,&list)!=0||!list){why=Why::Resolve;return -1;}
 int fd=-1,tried=0;why=Why::Connect;
 for(addrinfo* a=list;a&&tried<maxAddresses;a=a->ai_next,++tried){
  int s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(s<0)continue;
  nonBlocking(s);tune(s);int err=0;
  if(connect(s,a->ai_addr,a->ai_addrlen)!=0){err=errno;
   if(err==EINPROGRESS){int w=waitFor(s,POLLOUT,nowMs()+connectMs);socklen_t l=sizeof err;
    if(w==0)err=ETIMEDOUT;else if(w<0||getsockopt(s,SOL_SOCKET,SO_ERROR,&err,&l)!=0)err=EIO;}}
  if(!err){keepAlive(s);fd=s;why=Why::Ok;break;}
  why=err==ECONNREFUSED?Why::Refused:err==ETIMEDOUT?Why::Timeout:Why::Connect;close(s);
 }
 freeaddrinfo(list);return fd;
}
Outcome httpTunnel(int fd,const deck::ProxyEndpoint& ep,const char* host,unsigned port,Inbox& in,long long deadline){
 char req[2048];Size n=deck::proxyConnectRequest(host,port,ep.user,ep.pass,req,sizeof req);if(!n)return {Why::Protocol,0};
 int s=sendAll(fd,req,n,deadline);wipe(req,sizeof req);if(s<=0)return {s==0?Why::Timeout:Why::Protocol,0};
 Size end;while(!(end=deck::httpHeadEnd((const char*)in.buf,in.have))){long r=fill(fd,in,in.have+1,deadline);if(r!=1)return failed(r);}
 int status=deck::httpStatus((const char*)in.buf,end);
 if(status==200){take(in,end);return {Why::Ok,0};}
 if(status==407)return {Why::Auth,407};
 return status<0?Outcome{Why::Protocol,0}:Outcome{Why::Status,status};
}
Outcome socksTunnel(int fd,const deck::ProxyEndpoint& ep,const char* host,unsigned port,Inbox& in,long long deadline){
 bool login=ep.user[0]!=0;unsigned char msg[600];Size n=deck::socksGreeting(login,msg,sizeof msg);
 int s=sendAll(fd,msg,n,deadline);if(s<=0)return {s==0?Why::Timeout:Why::Protocol,0};
 long r=fill(fd,in,2,deadline);if(r!=1)return failed(r);
 int method=deck::socksMethod(in.buf,in.have);take(in,2);
 if(method==0xFF)return {Why::Auth,0}; // none of our methods: it wants a login we do not have
 if(method==2&&login){
  n=deck::socksAuth(ep.user,ep.pass,msg,sizeof msg);if(!n)return {Why::Protocol,0};
  s=sendAll(fd,msg,n,deadline);wipe(msg,sizeof msg);if(s<=0)return {s==0?Why::Timeout:Why::Protocol,0};
  r=fill(fd,in,2,deadline);if(r!=1)return failed(r);
  int status=deck::socksAuthStatus(in.buf,in.have);take(in,2);if(status!=0)return {Why::Auth,0};
 }else if(method!=0)return {Why::Protocol,0};
 n=deck::socksConnect(host,port,msg,sizeof msg);if(!n)return {Why::Protocol,0};
 s=sendAll(fd,msg,n,deadline);if(s<=0)return {s==0?Why::Timeout:Why::Protocol,0};
 for(;;){int code;long len=deck::socksReply(in.buf,in.have,code);
  if(len>0){if(code==0){take(in,(Size)len);return {Why::Ok,0};}return {code==0xFF?Why::Protocol:Why::Socks,code};}
  r=fill(fd,in,in.have+1,deadline);if(r!=1)return failed(r);}
}
// A tunnel to host:port through endpoint i: the socket, or -1 with the reason in `out`.
int tunnelVia(int i,const char* host,unsigned port,Inbox& in,Outcome& out){
 in.have=0;Why why;int fd=dial(gEndpoints[i].host,gEndpoints[i].port,why);if(fd<0){out={why,0};return -1;}
 long long deadline=nowMs()+handshakeMs;
 out=gScheme==deck::ProxyScheme::Socks5?socksTunnel(fd,gEndpoints[i],host,port,in,deadline):httpTunnel(fd,gEndpoints[i],host,port,in,deadline);
 if(out.why!=Why::Ok){close(fd);in.have=0;return -1;}
 return fd;
}
// Failover: the endpoints in order, starting with the one that worked last.
int tunnel(const char* host,unsigned port,Inbox& in){
 int first=__atomic_load_n(&gLast,__ATOMIC_RELAXED);
 for(int k=0;k<gCount;++k){int i=(first+k)%gCount;Outcome o;int fd=tunnelVia(i,host,port,in,o);if(fd>=0){__atomic_store_n(&gLast,i,__ATOMIC_RELAXED);return fd;}}
 return -1;
}
// A plain TCP connection to the first reachable HTTP upstream (absolute-form requests); `which` gets its index.
int dialAny(int& which){
 int first=__atomic_load_n(&gLast,__ATOMIC_RELAXED);
 for(int k=0;k<gCount;++k){int i=(first+k)%gCount;Why why;int fd=dial(gEndpoints[i].host,gEndpoints[i].port,why);if(fd>=0){which=i;return fd;}}
 return -1;
}

// ---- one app connection ----
struct Session{char head[headCap];Inbox up;char flow[2][relayCap];Size off[2],len[2];};
bool preload(Session& s,int d,const void* data,Size n){if(s.off[d]+s.len[d]+n>relayCap)return false;memcpy(s.flow[d]+s.off[d]+s.len[d],data,n);s.len[d]+=n;return true;}
// Answer, stop sending, and read what the app still sends for a moment, so the answer is not lost to a reset.
void finish(int c,const char* answer){
 sendAll(c,answer,strlen(answer),nowMs()+replyMs);shutdown(c,SHUT_WR);
 char sink[4096];long long deadline=nowMs()+2000;for(int k=0;k<64&&recvSome(c,sink,sizeof sink,deadline)>0;++k){}
 close(c);
}
// Copies both ways until both sides are finished; the end of one direction is passed on as a half-close.
// Direction 0 goes app -> upstream, 1 upstream -> app. firstHead holds the upstream's first response head
// back from the app and turns a 407 (wrong upstream login) into 502, as for tunnels.
void relay(Session& s,int c,int u,bool firstHead){
 int fd[2]={c,u};bool eof[2]={false,false},shut[2]={false,false},broken=false;
 while(!(shut[0]&&shut[1])){
  pollfd p[2];
  for(int k=0;k<2;++k){p[k].fd=-1;p[k].events=0;p[k].revents=0;}
  for(int d=0;d<2;++d){
   if(!eof[d]&&s.off[d]+s.len[d]<relayCap)p[d].events|=POLLIN;
   if(s.len[d]&&!(d==1&&firstHead))p[1-d].events|=POLLOUT;
  }
  for(int k=0;k<2;++k)if(p[k].events)p[k].fd=fd[k]; // no interest, no wakeups (a hung-up socket would spin)
  int r=poll(p,2,shut[0]||shut[1]?halfClosedMs:-1);
  if(r<0){if(errno==EINTR)continue;broken=true;break;}
  if(r==0){broken=true;break;}
  for(int k=0;k<2&&!broken;++k){
   short rv=p[k].revents;if(rv&POLLNVAL){broken=true;break;}
   if((p[k].events&POLLIN)&&(rv&(POLLIN|POLLHUP|POLLERR))){int d=k;
    ssize_t n=read(fd[k],s.flow[d]+s.off[d]+s.len[d],relayCap-s.off[d]-s.len[d]);
    if(n>0)s.len[d]+=(Size)n;else if(n==0)eof[d]=true;else if(errno!=EAGAIN&&errno!=EINTR)broken=true;}
   if(!broken&&(p[k].events&POLLOUT)&&(rv&(POLLOUT|POLLHUP|POLLERR))){int d=1-k;
    ssize_t n=write(fd[k],s.flow[d]+s.off[d],s.len[d]);
    if(n>0){s.off[d]+=(Size)n;s.len[d]-=(Size)n;if(!s.len[d])s.off[d]=0;}else if(n<0&&errno!=EAGAIN&&errno!=EINTR)broken=true;}
  }
  if(broken)break;
  if(firstHead){Size end=deck::httpHeadEnd(s.flow[1]+s.off[1],s.len[1]);
   if(end&&deck::httpStatus(s.flow[1]+s.off[1],end)==407){abortive(u);close(u);finish(c,badGateway);return;}
   if(end||eof[1]||s.off[1]+s.len[1]==relayCap)firstHead=false;}
  for(int d=0;d<2;++d)if(eof[d]&&!s.len[d]&&!shut[d]){shutdown(fd[1-d],SHUT_WR);shut[d]=true;}
 }
 if(broken){abortive(c);abortive(u);} // pass a failure on as a reset, not as a clean end
 close(c);close(u);
}
void handle(int c,Session& s){
 nonBlocking(c);tune(c);s.off[0]=s.off[1]=s.len[0]=s.len[1]=0;s.up.have=0;
 Size have=0,end;long long deadline=nowMs()+headMs;
 while(!(end=deck::httpHeadEnd(s.head,have))){
  if(have==headCap){finish(c,badRequest);return;}
  long r=recvSome(c,s.head+have,headCap-have,deadline);if(r<=0){close(c);return;}have+=(Size)r;}
 deck::ProxyRequest req;if(!deck::proxyParseRequest(s.head,have,req)){finish(c,badRequest);return;}
 const char* early=s.head+end;Size earlyLen=have-end; // a request body or a TLS hello sent ahead of our answer
 if(req.connect){
  int u=tunnel(req.host,req.port,s.up);if(u<0){finish(c,badGateway);return;}
  if(!preload(s,1,established,sizeof established-1)||!preload(s,1,s.up.buf,s.up.have)||!preload(s,0,early,earlyLen)){abortive(u);close(u);finish(c,badGateway);return;}
  relay(s,c,u,false);return;
 }
 int u,which=0;bool socks=gScheme==deck::ProxyScheme::Socks5;
 if(socks)u=tunnel(req.host,req.port,s.up);else u=dialAny(which);
 if(u<0){finish(c,badGateway);return;}
 const deck::ProxyEndpoint& ep=gEndpoints[which];
 Size n=deck::proxyRewriteHead(s.head,end,socks?nullptr:ep.user,socks?nullptr:ep.pass,socks,s.flow[0],relayCap);
 if(!n){wipe(s.flow[0],relayCap);close(u);finish(c,badRequest);return;}
 s.len[0]=n;
 if(!preload(s,0,early,earlyLen)||!preload(s,1,s.up.buf,s.up.have)){wipe(s.flow[0],relayCap);close(u);finish(c,badGateway);return;}
 relay(s,c,u,!socks);
}
void* serveClient(void* arg){
 int c=(int)(intptr_t)arg;Session* s=(Session*)malloc(sizeof(Session));
 if(s){handle(c,*s);wipe(s->flow[0],relayCap);free(s);}else close(c); // flow[0] may hold our Proxy-Authorization
 __atomic_sub_fetch(&gClients,1,__ATOMIC_ACQ_REL);return nullptr;
}
void* acceptLoop(void* arg){
 int ls=(int)(intptr_t)arg;pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
 for(;;){
  int c=accept(ls,nullptr,nullptr);
  if(c<0){if(errno!=EINTR&&errno!=ECONNABORTED)usleep(errno==EMFILE||errno==ENFILE?100000:10000);continue;}
  fcntl(c,F_SETFD,FD_CLOEXEC);
  if(__atomic_add_fetch(&gClients,1,__ATOMIC_ACQ_REL)>maxClients){__atomic_sub_fetch(&gClients,1,__ATOMIC_ACQ_REL);close(c);continue;}
  pthread_t t;if(pthread_create(&t,&attr,serveClient,(void*)(intptr_t)c)!=0){__atomic_sub_fetch(&gClients,1,__ATOMIC_ACQ_REL);close(c);}
 }
 return nullptr;
}
// Returns when the process exits (at once when it is already gone).
void watchProcess(pid_t pid){
 int kq=kqueue();
 if(kq>=0){struct kevent ev;EV_SET(&ev,pid,EVFILT_PROC,EV_ADD|EV_ONESHOT,NOTE_EXIT,0,nullptr);
  if(kevent(kq,&ev,1,nullptr,0,nullptr)==0){for(;;){struct kevent got;int n=kevent(kq,nullptr,0,&got,1,nullptr);if(n>0)return;if(n<0&&errno!=EINTR)break;}}
  else if(errno==ESRCH)return;
 }
 while(kill(pid,0)==0||errno==EPERM)sleep(1);
}
int serve(){
 // Two descriptors per connection: lift the default limit of 256.
 rlimit rl;if(getrlimit(RLIMIT_NOFILE,&rl)==0){rlim_t want=4096;if(rl.rlim_max<want)want=rl.rlim_max;if(rl.rlim_cur<want){rl.rlim_cur=want;setrlimit(RLIMIT_NOFILE,&rl);}}
 setsid(); // a Ctrl-C or hang-up meant for the manager's terminal does not cut an app off; the watch decides
 int ls=socket(AF_INET,SOCK_STREAM,0);sockaddr_in a;memset(&a,0,sizeof a);a.sin_len=sizeof a;a.sin_family=AF_INET;a.sin_port=0;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 socklen_t l=sizeof a;
 if(ls<0||bind(ls,(sockaddr*)&a,sizeof a)!=0||listen(ls,SOMAXCONN)!=0||getsockname(ls,(sockaddr*)&a,&l)!=0){fputs("appdeck-proxy: cannot listen on 127.0.0.1\n",stderr);return 1;}
 fcntl(ls,F_SETFD,FD_CLOEXEC);
 pthread_t t;pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
 if(pthread_create(&t,&attr,acceptLoop,(void*)(intptr_t)ls)!=0){fputs("appdeck-proxy: cannot start\n",stderr);return 1;}
 char line[64];int n=snprintf(line,sizeof line,"port %u\n",(unsigned)ntohs(a.sin_port));
 if(!writeAll(1,line,(Size)n))return 1;
 devNull(1); // the manager's reader sees the end of stdout right after the port
 long long deadline=nowMs()+watchMs;
 for(;;){
  int r=readLine(line,sizeof line,deadline);
  if(r==0||r==-1)_exit(0); // stdin closed or no watch in time: nobody needs this bridge
  if(r==1&&!line[0])continue;
  char* tok[3];int k=r==1?split(line,tok,2):0;char* e=nullptr;long pid=k==2&&!strcmp(tok[0],"watch")?strtol(tok[1],&e,10):0;
  if(pid<=0||pid>INT32_MAX||!e||*e){fputs("appdeck-proxy: expected \"watch <pid>\"\n",stderr);_exit(2);}
  devNull(0);devNull(2); // from now on the manager may go away; nothing is read or written
  watchProcess((pid_t)pid);_exit(0); // connection threads may be busy: no process teardown
 }
}
const char* reason(const Outcome& o,char* buf,Size cap){
 switch(o.why){
  case Why::Resolve:return "resolve";case Why::Connect:return "connect";case Why::Timeout:return "timeout";case Why::Refused:return "refused";
  case Why::Status:snprintf(buf,cap,"status-%d",o.code);return buf;case Why::Socks:snprintf(buf,cap,"socks-%d",o.code);return buf;
  default:return "protocol";
 }
}
int check(){
 const char* host=deck::proxyProbeHost;unsigned port=deck::proxyProbePort;deck::ProxyEndpoint probe;
 if(const char* env=getenv("APPDECK_PROXY_PROBE")){
  if(!deck::proxyParse(env,probe)||probe.user[0]){fputs("appdeck-proxy: APPDECK_PROXY_PROBE must be host:port\n",stderr);return 2;}
  host=probe.host;port=probe.port;}
 Inbox* in=(Inbox*)malloc(sizeof(Inbox));if(!in)return 1;bool any=false;
 for(int i=0;i<gCount;++i){
  long long started=nowMs();Outcome o;int fd=tunnelVia(i,host,port,*in,o);char line[64],code[16];
  if(fd>=0){close(fd);any=true;snprintf(line,sizeof line,"ok %d %lld\n",i,nowMs()-started);}
  else if(o.why==Why::Auth)snprintf(line,sizeof line,"auth %d\n",i);
  else snprintf(line,sizeof line,"fail %d %s\n",i,reason(o,code,sizeof code));
  writeAll(1,line,strlen(line)); // one line per endpoint as soon as it is known
 }
 free(in);return any?0:1;
}
}

int main(int argc,char** argv){
 signal(SIGPIPE,SIG_IGN);
 bool serveMode=argc==2&&!strcmp(argv[1],"serve"),checkMode=argc==2&&!strcmp(argv[1],"check");
 if(!serveMode&&!checkMode){fputs("usage: appdeck-proxy serve|check (configuration on stdin)\n",stderr);return 2;}
 char msg[96];const char* why=readConfig(msg,sizeof msg);
 if(why){fprintf(stderr,"appdeck-proxy: %s\n",why);return 2;}
 return serveMode?serve():check();
}
