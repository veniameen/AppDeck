#pragma once
// Pure proxy rules: endpoint lines, HTTP CONNECT and SOCKS5 (RFC 1928/1929) messages, and the switches
// an app is started with. No sockets, no AppKit, no allocation; unit-tested in tests/proxy_test.cpp and
// used by the local bridge (src/proxy_bridge.cpp) and the main app.
#include "core.hpp"
namespace deck {
enum class ProxyScheme{Http,Socks5};
inline const char* proxySchemeName(ProxyScheme s){return s==ProxyScheme::Socks5?"socks5":"http";}
inline bool proxySchemeParse(const char* s,ProxyScheme& out){if(equal(s,"http")){out=ProxyScheme::Http;return true;}if(equal(s,"socks5")){out=ProxyScheme::Socks5;return true;}return false;}

constexpr Size proxyFieldCap=256;
struct ProxyEndpoint{char host[proxyFieldCap];unsigned port;char user[proxyFieldCap];char pass[proxyFieldCap];};

// Where a new bridge proves an upstream works: a tunnel to this host and port (no TLS is spoken).
constexpr const char* proxyProbeHost="api.openai.com";constexpr unsigned proxyProbePort=443;
// Loopback never goes through the proxy (the bridge itself lives there).
constexpr const char* proxyBypassChromium="localhost;127.0.0.1;[::1]";
constexpr const char* proxyBypassEnv="localhost,127.0.0.1,::1";

inline bool proxyCopy(const char* from,const char* to,char* out,Size cap){
 Size n=(Size)(to-from);if(n>=cap)return false;for(Size i=0;i<n;++i)out[i]=from[i];out[n]=0;return true;
}
inline bool proxyPort(const char* from,const char* to,unsigned& port){
 if(from>=to||to-from>5)return false;unsigned v=0;for(const char* p=from;p<to;++p){if(*p<'0'||*p>'9')return false;v=v*10+(unsigned)(*p-'0');}
 if(v<1||v>65535)return false;port=v;return true;
}
// One address per line, as the owner's proxy provider hands them out:
//   host:port                  (login and password come from the proxy's common fields)
//   host:port:login:password   (the password may itself contain ':')
//   [v6]:port[:login:password]
// Spaces around the line are ignored; control characters, spaces inside the host and empty parts are not.
inline bool proxyParse(const char* line,ProxyEndpoint& out){
 out.host[0]=out.user[0]=out.pass[0]=0;out.port=0;if(!line)return false;
 const char* b=line;while(*b==' '||*b=='\t')++b;const char* e=b+length(b);while(e>b&&(e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n'))--e;
 if(b==e)return false;for(const char* p=b;p<e;++p)if((unsigned char)*p<0x20||*p==0x7f)return false;
 const char *hostEnd,*rest;
 if(*b=='['){const char* close=b;while(close<e&&*close!=']')++close;if(close==e||close==b+1)return false;
  if(!proxyCopy(b+1,close,out.host,proxyFieldCap))return false;for(const char* p=b+1;p<close;++p)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||(*p>='A'&&*p<='F')||*p==':'||*p=='.'))return false;
  if(close+1>=e||close[1]!=':')return false;rest=close+2;hostEnd=close;}
 else{hostEnd=b;while(hostEnd<e&&*hostEnd!=':')++hostEnd;if(hostEnd==b||hostEnd==e)return false;
  for(const char* p=b;p<hostEnd;++p)if(*p==' '||*p=='/'||*p=='@'||*p=='\\')return false;
  if(hostEnd-b>253||!proxyCopy(b,hostEnd,out.host,proxyFieldCap))return false;rest=hostEnd+1;}
 const char* portEnd=rest;while(portEnd<e&&*portEnd!=':')++portEnd;if(!proxyPort(rest,portEnd,out.port))return false;
 if(portEnd==e)return true;
 const char* user=portEnd+1;const char* userEnd=user;while(userEnd<e&&*userEnd!=':')++userEnd;
 if(userEnd==user||userEnd==e||userEnd+1==e)return false; // "host:port:" or a login without a password
 if(!proxyCopy(user,userEnd,out.user,proxyFieldCap)||!proxyCopy(userEnd+1,e,out.pass,proxyFieldCap))return false;
 return true;
}
// Login/password for an endpoint: its own, else the proxy's common ones (both or neither).
inline void proxyCredentials(ProxyEndpoint& ep,const char* user,const char* pass){
 if(ep.user[0])return;if(!user||!*user||!pass)return;
 Size u=length(user),p=length(pass);if(u>=proxyFieldCap||p>=proxyFieldCap)return;
 for(Size i=0;i<=u;++i)ep.user[i]=user[i];for(Size i=0;i<=p;++i)ep.pass[i]=pass[i];
}

inline Size base64(const unsigned char* in,Size n,char* out,Size cap){
 const char* abc="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";Size need=(n+2)/3*4;if(need+1>cap)return 0;Size o=0;
 for(Size i=0;i<n;i+=3){unsigned v=(unsigned)in[i]<<16|(i+1<n?(unsigned)in[i+1]<<8:0u)|(i+2<n?(unsigned)in[i+2]:0u);
  out[o++]=abc[v>>18&63];out[o++]=abc[v>>12&63];out[o++]=i+1<n?abc[v>>6&63]:'=';out[o++]=i+2<n?abc[v&63]:'=';}
 out[o]=0;return o;
}
// "Proxy-Authorization: Basic …\r\n" for login:password, or nothing without credentials. Returns length, 0 on overflow/none.
inline Size proxyAuthHeader(const char* user,const char* pass,char* out,Size cap){
 if(!user||!*user)return 0;unsigned char pair[2*proxyFieldCap+2];Size u=length(user),p=length(pass);if(u+p+1>sizeof pair)return 0;
 for(Size i=0;i<u;++i)pair[i]=(unsigned char)user[i];pair[u]=':';for(Size i=0;i<p;++i)pair[u+1+i]=(unsigned char)pass[i];
 const char* head="Proxy-Authorization: Basic ";Size h=length(head);if(h>=cap)return 0;for(Size i=0;i<h;++i)out[i]=head[i];
 Size b=base64(pair,u+1+p,out+h,cap-h);if(!b||h+b+3>cap)return 0;out[h+b]='\r';out[h+b+1]='\n';out[h+b+2]=0;return h+b+2;
}
inline bool proxyAppend(char* out,Size cap,Size& used,const char* s,Size n){if(used+n+1>cap)return false;for(Size i=0;i<n;++i)out[used++]=s[i];out[used]=0;return true;}
inline bool proxyAppendNumber(char* out,Size cap,Size& used,unsigned v){char d[12];Size n=0;do{d[n++]=(char)('0'+v%10);v/=10;}while(v);char r[12];for(Size i=0;i<n;++i)r[i]=d[n-1-i];return proxyAppend(out,cap,used,r,n);}
inline bool proxyAppendTarget(char* out,Size cap,Size& used,const char* host,unsigned port){
 bool v6=false;for(const char* p=host;*p;++p)if(*p==':')v6=true;
 return (v6?proxyAppend(out,cap,used,"[",1):true)&&proxyAppend(out,cap,used,host,length(host))&&(v6?proxyAppend(out,cap,used,"]",1):true)&&proxyAppend(out,cap,used,":",1)&&proxyAppendNumber(out,cap,used,port);
}
// The tunnel request to an HTTP upstream. Returns its length, 0 when it does not fit.
inline Size proxyConnectRequest(const char* host,unsigned port,const char* user,const char* pass,char* out,Size cap){
 Size used=0;if(!cap)return 0;out[0]=0;
 if(!proxyAppend(out,cap,used,"CONNECT ",8)||!proxyAppendTarget(out,cap,used,host,port)||!proxyAppend(out,cap,used," HTTP/1.1\r\nHost: ",17)||!proxyAppendTarget(out,cap,used,host,port)||!proxyAppend(out,cap,used,"\r\n",2))return 0;
 char auth[1024];Size a=proxyAuthHeader(user,pass,auth,sizeof auth);if(user&&*user&&!a)return 0;
 if(a&&!proxyAppend(out,cap,used,auth,a))return 0;
 if(!proxyAppend(out,cap,used,"Proxy-Connection: keep-alive\r\n\r\n",32))return 0;return used;
}
// End of an HTTP head (index just past "\r\n\r\n"), or 0 when it is not complete yet.
inline Size httpHeadEnd(const char* buf,Size n){for(Size i=3;i<n;++i)if(buf[i-3]=='\r'&&buf[i-2]=='\n'&&buf[i-1]=='\r'&&buf[i]=='\n')return i+1;return 0;}
// Status code of a response head ("HTTP/1.1 200 Connection established"), -1 when invalid.
inline int httpStatus(const char* head,Size n){
 if(n<12||!(head[0]=='H'&&head[1]=='T'&&head[2]=='T'&&head[3]=='P'&&head[4]=='/'))return -1;Size i=5;while(i<n&&head[i]!=' ')++i;
 if(i+4>n)return -1;int v=0;for(Size k=i+1;k<i+4;++k){if(head[k]<'0'||head[k]>'9')return -1;v=v*10+(head[k]-'0');}
 if(i+4<n&&head[i+4]!=' '&&head[i+4]!='\r')return -1; // exactly three digits ("HTTP/1.1 2000" is not 200)
 return v;
}
// A request from the app to the bridge: CONNECT host:port, or an absolute-form http:// request.
struct ProxyRequest{bool connect;char host[proxyFieldCap];unsigned port;Size lineEnd;Size headLen;};
inline bool proxyParseRequest(const char* buf,Size n,ProxyRequest& r){
 r.connect=false;r.host[0]=0;r.port=0;r.lineEnd=0;r.headLen=httpHeadEnd(buf,n);if(!r.headLen)return false;
 Size eol=0;while(eol+1<r.headLen&&!(buf[eol]=='\r'&&buf[eol+1]=='\n'))++eol;r.lineEnd=eol;
 Size sp1=0;while(sp1<eol&&buf[sp1]!=' ')++sp1;if(sp1==0||sp1>=eol)return false;
 Size t=sp1+1,sp2=t;while(sp2<eol&&buf[sp2]!=' ')++sp2;if(sp2>=eol||sp2==t)return false;
 const char* target=buf+t;const char* end=buf+sp2;
 if(sp1==7&&prefix(buf,"CONNECT ")){r.connect=true;}
 else{if(!(end-target>7&&prefix(target,"http://")))return false;target+=7;} // https:// always arrives as CONNECT
 const char* hostEnd;const char* portFrom=nullptr;const char* stop=end;
 if(!r.connect){const char* slash=target;while(slash<end&&*slash!='/'&&*slash!='?')++slash;stop=slash;}
 if(*target=='['){const char* close=target;while(close<stop&&*close!=']')++close;if(close==stop)return false;if(!proxyCopy(target+1,close,r.host,proxyFieldCap))return false;hostEnd=close+1;}
 else{hostEnd=target;while(hostEnd<stop&&*hostEnd!=':')++hostEnd;if(!proxyCopy(target,hostEnd,r.host,proxyFieldCap))return false;}
 if(!r.host[0])return false;for(const char* p=r.host;*p;++p)if((unsigned char)*p<=0x20||*p=='@'||*p=='/'||*p=='\\')return false;
 if(hostEnd<stop){if(*hostEnd!=':')return false;portFrom=hostEnd+1;if(!proxyPort(portFrom,stop,r.port))return false;}
 else{if(r.connect)return false;r.port=80;}
 return true;
}
inline bool proxyHeaderIs(const char* line,Size n,const char* name){Size k=length(name);if(n<=k||line[k]!=':')return false;for(Size i=0;i<k;++i){char a=line[i],b=name[i];if(a>='A'&&a<='Z')a=(char)(a-'A'+'a');if(b>='A'&&b<='Z')b=(char)(b-'A'+'a');if(a!=b)return false;}return true;}
// A "Connection" header that asks for a protocol upgrade (a WebSocket over ws://).
inline bool proxyUpgrade(const char* head,Size n){
 Size i=0;while(i+1<n&&!(head[i]=='\r'&&head[i+1]=='\n'))++i;i+=2;
 while(i<n){Size e=i;while(e+1<n&&!(head[e]=='\r'&&head[e+1]=='\n'))++e;if(e+1>=n||e==i)return false;
  if(proxyHeaderIs(head+i,e-i,"connection"))for(Size k=i+11;k+7<=e;++k){const char* w="upgrade";Size m=0;while(m<7){char a=head[k+m];if(a>='A'&&a<='Z')a=(char)(a-'A'+'a');if(a!=w[m])break;++m;}if(m==7)return true;}
  i=e+2;}
 return false;
}
// A plain http:// request on its way to an HTTP upstream: the app's own Proxy-* headers are dropped and
// the upstream's Proxy-Authorization added. originForm (SOCKS5 upstream / direct target): the request
// line gets the origin form ("GET /path HTTP/1.1") and no Proxy-Authorization is added.
// The bridge serves one request per connection, so the app's Connection/Keep-Alive headers become
// "Connection: close"; only an upgrade request keeps its own Connection header.
inline Size proxyRewriteHead(const char* head,Size n,const char* user,const char* pass,bool originForm,char* out,Size cap){
 Size used=0,eol=0;if(!cap)return 0;out[0]=0;while(eol+1<n&&!(head[eol]=='\r'&&head[eol+1]=='\n'))++eol;if(eol+1>=n)return 0;
 if(originForm){Size sp1=0;while(sp1<eol&&head[sp1]!=' ')++sp1;Size t=sp1+1;if(t+7>eol||!prefix(head+t,"http://"))return 0;Size path=t+7;while(path<eol&&head[path]!='/'&&head[path]!=' '&&head[path]!='?')++path;
  if(!proxyAppend(out,cap,used,head,sp1+1))return 0;if(head[path]!='/'&&!proxyAppend(out,cap,used,"/",1))return 0;if(!proxyAppend(out,cap,used,head+path,eol-path))return 0;}
 else if(!proxyAppend(out,cap,used,head,eol))return 0;
 if(!proxyAppend(out,cap,used,"\r\n",2))return 0;
 if(!originForm){char auth[1024];Size a=proxyAuthHeader(user,pass,auth,sizeof auth);if(user&&*user&&!a)return 0;if(a&&!proxyAppend(out,cap,used,auth,a))return 0;}
 bool upgrade=proxyUpgrade(head,n);
 Size i=eol+2;while(i<n){Size e=i;while(e+1<n&&!(head[e]=='\r'&&head[e+1]=='\n'))++e;if(e+1>=n)break;Size len=e-i;
  if(len==0){if(!upgrade&&!proxyAppend(out,cap,used,"Connection: close\r\n",19))return 0;if(!proxyAppend(out,cap,used,"\r\n",2))return 0;return used;}
  bool drop=proxyHeaderIs(head+i,len,"proxy-authorization")||proxyHeaderIs(head+i,len,"proxy-connection")||(!upgrade&&(proxyHeaderIs(head+i,len,"connection")||proxyHeaderIs(head+i,len,"keep-alive")));
  if(!drop&&!proxyAppend(out,cap,used,head+i,len+2))return 0;i=e+2;}
 return 0;
}
// ---- SOCKS5 ----
inline Size socksGreeting(bool auth,unsigned char* out,Size cap){if(cap<4)return 0;out[0]=5;if(auth){out[1]=2;out[2]=0;out[3]=2;return 4;}out[1]=1;out[2]=0;return 3;}
// Chosen method (0 none, 2 login), 0xFF when a SOCKS5 server accepts none of ours, -2 when the reply is not
// SOCKS5 at all (an HTTP or TLS server answering the greeting), -1 when it is not complete.
inline int socksMethod(const unsigned char* in,Size n){if(n<2)return -1;if(in[0]!=5)return -2;return in[1];}
inline Size socksAuth(const char* user,const char* pass,unsigned char* out,Size cap){
 Size u=length(user),p=length(pass);if(u<1||u>255||p>255||3+u+p>cap)return 0;out[0]=1;out[1]=(unsigned char)u;for(Size i=0;i<u;++i)out[2+i]=(unsigned char)user[i];
 out[2+u]=(unsigned char)p;for(Size i=0;i<p;++i)out[3+u+i]=(unsigned char)pass[i];return 3+u+p;
}
// Login result: 0 accepted, 1 rejected, -2 a refusal that is not a login reply, -1 not complete. The status
// byte alone decides success, as in curl: RFC 1929 says version 1, but some servers answer 05 00. A nonzero
// status counts as a rejected login only under version 1 or 5; with any other version byte it is a protocol error.
inline int socksAuthStatus(const unsigned char* in,Size n){if(n<2)return -1;if(in[1]==0)return 0;return in[0]==1||in[0]==5?1:-2;}
// Methods that may be sent again after an upstream stayed silent (RFC 9110 idempotent methods): the first
// upstream may have passed the request on, so anything else waits for its answer instead.
inline bool proxyIdempotent(const char* head,Size n){
 static const char* const methods[]={"GET ","HEAD ","OPTIONS ","TRACE ","PUT ","DELETE "};
 for(const char* m:methods){Size k=length(m);if(n>=k&&prefix(head,m))return true;}
 return false;
}
inline bool proxyIPv4(const char* h,unsigned char ip[4]){unsigned part=0,dots=0,digits=0;for(const char* p=h;;++p){if(*p>='0'&&*p<='9'){part=part*10+(unsigned)(*p-'0');if(++digits>3||part>255)return false;}
  else if(*p=='.'||!*p){if(!digits||dots>3)return false;ip[dots++]=(unsigned char)part;part=0;digits=0;if(!*p)break;}else return false;}return dots==4;}
// An IPv6 literal without brackets or zone ("2001:db8::1", "::ffff:1.2.3.4") as 16 bytes.
inline bool proxyIPv6(const char* h,unsigned char ip[16]){
 if(!h||!*h)return false;unsigned w[8];int n=0,gap=-1;const char* p=h;
 if(*p==':'){if(p[1]!=':')return false;p+=2;gap=0;}
 while(*p){
  if(n==8)return false;const char* q=p;while(*q&&*q!=':'&&*q!='.')++q;
  if(*q=='.'){unsigned char v4[4];if(n>6||!proxyIPv4(p,v4))return false;w[n++]=(unsigned)v4[0]<<8|v4[1];w[n++]=(unsigned)v4[2]<<8|v4[3];break;} // dotted tail ends the address
  unsigned v=0;int digits=0;
  for(;*p&&*p!=':';++p){char c=*p;unsigned x;if(c>='0'&&c<='9')x=(unsigned)(c-'0');else if(c>='a'&&c<='f')x=(unsigned)(c-'a'+10);else if(c>='A'&&c<='F')x=(unsigned)(c-'A'+10);else return false;if(++digits>4)return false;v=v*16+x;}
  if(!digits)return false;w[n++]=v;
  if(*p==':'){++p;if(*p==':'){if(gap>=0)return false;gap=n;++p;}else if(!*p)return false;}
 }
 if(gap<0?n!=8:n>7)return false;
 int zeros=8-n,k=0;for(int i=0;i<n;++i){if(i==gap)for(int z=0;z<zeros;++z){ip[k++]=0;ip[k++]=0;}ip[k++]=(unsigned char)(w[i]>>8);ip[k++]=(unsigned char)(w[i]&255);}
 if(gap==n)for(int z=0;z<zeros;++z){ip[k++]=0;ip[k++]=0;}
 return k==16;
}
inline Size socksConnect(const char* host,unsigned port,unsigned char* out,Size cap){
 if(port<1||port>65535||cap<10)return 0;out[0]=5;out[1]=1;out[2]=0;unsigned char ip[16];Size used;
 if(proxyIPv4(host,ip)){out[3]=1;for(int i=0;i<4;++i)out[4+i]=ip[i];used=8;}
 else if(proxyIPv6(host,ip)){if(cap<22)return 0;out[3]=4;for(int i=0;i<16;++i)out[4+i]=ip[i];used=20;}
 else{Size h=length(host);if(h<1||h>255||7+h>cap)return 0;out[3]=3;out[4]=(unsigned char)h;for(Size i=0;i<h;++i)out[5+i]=(unsigned char)host[i];used=5+h;} // names resolve at the proxy (socks5h)
 out[used]=(unsigned char)(port>>8);out[used+1]=(unsigned char)(port&255);return used+2;
}
// Reply to CONNECT: -1 while incomplete; otherwise its length with code 0 for success, the server's REP code
// (1-255) for a refusal, or -2 when it is not a SOCKS5 reply (version byte) or a success with an unknown ATYP.
// A refusal is decided on its first two bytes: the tunnel ends there, so its address type does not matter.
inline long socksReply(const unsigned char* in,Size n,int& code){
 code=-1;if(n<2)return -1;
 if(in[0]!=5){code=-2;return (long)n;}
 if(in[1]!=0){code=in[1];return (long)n;}
 if(n<5)return -1;Size need;
 switch(in[3]){case 1:need=10;break;case 4:need=22;break;case 3:need=7+(Size)in[4];break;default:code=-2;return (long)n;}
 if(n<need)return -1;code=0;return (long)need;
}
}
