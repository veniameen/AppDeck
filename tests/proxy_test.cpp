#include "../src/proxy_policy.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
int main(){
 unsigned checks=0;auto check=[&](bool b){++checks;if(!b){std::cerr<<"FAILED assertion "<<checks<<'\n';std::exit(1);}};
 using deck::ProxyEndpoint;using deck::Size;ProxyEndpoint e;
 // Endpoint lines as providers hand them out.
 check(deck::proxyParse("203.0.113.7:8000",e)&&!std::strcmp(e.host,"203.0.113.7")&&e.port==8000&&!e.user[0]&&!e.pass[0]);
 check(deck::proxyParse("  proxy.example.net:3128:alice:s3cr3t \r\n",e)&&!std::strcmp(e.host,"proxy.example.net")&&e.port==3128&&!std::strcmp(e.user,"alice")&&!std::strcmp(e.pass,"s3cr3t"));
 check(deck::proxyParse("h:1:u:p:with:colons",e)&&!std::strcmp(e.user,"u")&&!std::strcmp(e.pass,"p:with:colons"));
 check(deck::proxyParse("[2001:db8::1]:1080:bob:pw",e)&&!std::strcmp(e.host,"2001:db8::1")&&e.port==1080&&!std::strcmp(e.user,"bob"));
 check(deck::proxyParse("[::1]:9",e)&&!std::strcmp(e.host,"::1")&&e.port==9);
 for(const char* bad:{"","   ","host","host:","host:0","host:65536","host:80a",":80","host:80:","host:80:user","host:80:user:","host:80::pass","ho st:80","user@host:80","http://host:80","[::1]","[]:80","[::1]80","[zz::1]:80","host:123456","h\tx:80","h:8\x01"})check(!deck::proxyParse(bad,e));
 {std::string longHost(254,'a');check(!deck::proxyParse((longHost+":80").c_str(),e));std::string ok(253,'a');check(deck::proxyParse((ok+":80").c_str(),e));}
 // Common credentials fill only endpoints without their own, and only as a pair.
 check(deck::proxyParse("h:1",e));deck::proxyCredentials(e,"u","p");check(!std::strcmp(e.user,"u")&&!std::strcmp(e.pass,"p"));
 check(deck::proxyParse("h:1:own:pw",e));deck::proxyCredentials(e,"u","p");check(!std::strcmp(e.user,"own")&&!std::strcmp(e.pass,"pw"));
 check(deck::proxyParse("h:1",e));deck::proxyCredentials(e,"",nullptr);check(!e.user[0]);deck::proxyCredentials(e,"u",nullptr);check(!e.user[0]);
 // Base64 (RFC 4648 vectors) and the Basic header.
 char b[64];auto b64=[&](const char* s){deck::base64((const unsigned char*)s,std::strlen(s),b,sizeof b);return std::string(b);};
 check(b64("")=="");check(b64("f")=="Zg==");check(b64("fo")=="Zm8=");check(b64("foo")=="Zm9v");check(b64("foobar")=="Zm9vYmFy");check(b64("alice:s3cr3t")=="YWxpY2U6czNjcjN0");
 check(deck::base64((const unsigned char*)"foobar",6,b,8)==0);
 char a[256];check(deck::proxyAuthHeader("alice","s3cr3t",a,sizeof a)>0&&std::string(a)=="Proxy-Authorization: Basic YWxpY2U6czNjcjN0\r\n");
 check(deck::proxyAuthHeader("",nullptr,a,sizeof a)==0);check(deck::proxyAuthHeader("alice","x",a,10)==0);
 // CONNECT request.
 char req[1024];Size n=deck::proxyConnectRequest("api.openai.com",443,"alice","s3cr3t",req,sizeof req);
 check(n==std::strlen(req)&&std::string(req)=="CONNECT api.openai.com:443 HTTP/1.1\r\nHost: api.openai.com:443\r\nProxy-Authorization: Basic YWxpY2U6czNjcjN0\r\nProxy-Connection: keep-alive\r\n\r\n");
 n=deck::proxyConnectRequest("2001:db8::1",8443,nullptr,nullptr,req,sizeof req);check(std::string(req)=="CONNECT [2001:db8::1]:8443 HTTP/1.1\r\nHost: [2001:db8::1]:8443\r\nProxy-Connection: keep-alive\r\n\r\n");
 check(deck::proxyConnectRequest("api.openai.com",443,"alice","s3cr3t",req,40)==0);
 // Responses.
 const char* ok="HTTP/1.1 200 Connection established\r\nVia: x\r\n\r\nrest";check(deck::httpHeadEnd(ok,std::strlen(ok))==std::strlen(ok)-4&&deck::httpStatus(ok,std::strlen(ok))==200);
 check(deck::httpHeadEnd("HTTP/1.1 200 OK\r\n",17)==0);check(deck::httpStatus("HTTP/1.0 407 Proxy Authentication Required\r\n\r\n",46)==407);
 check(deck::httpStatus("SSH-2.0-OpenSSH\r\n\r\n",19)==-1);check(deck::httpStatus("HTTP/1.1 2x0 \r\n\r\n",17)==-1);check(deck::httpStatus("HTTP/1.1",8)==-1);
 check(deck::httpStatus("HTTP/1.1 200\r\n\r\n",16)==200);check(deck::httpStatus("HTTP/1.1 2000 x\r\n\r\n",19)==-1);check(deck::httpStatus("HTTP/1.1 200",12)==200);
 // Requests from the app.
 deck::ProxyRequest r;const char* c1="CONNECT chatgpt.com:443 HTTP/1.1\r\nHost: chatgpt.com:443\r\nUser-Agent: x\r\n\r\n";
 check(deck::proxyParseRequest(c1,std::strlen(c1),r)&&r.connect&&!std::strcmp(r.host,"chatgpt.com")&&r.port==443&&r.headLen==std::strlen(c1));
 const char* c2="CONNECT [::1]:8443 HTTP/1.1\r\n\r\n";check(deck::proxyParseRequest(c2,std::strlen(c2),r)&&r.connect&&!std::strcmp(r.host,"::1")&&r.port==8443);
 const char* g1="GET http://example.com/a?b=1 HTTP/1.1\r\nHost: example.com\r\nProxy-Connection: keep-alive\r\nProxy-Authorization: Basic Zm9v\r\nAccept: */*\r\n\r\n";
 check(deck::proxyParseRequest(g1,std::strlen(g1),r)&&!r.connect&&!std::strcmp(r.host,"example.com")&&r.port==80);
 const char* g2="GET http://example.com:8080 HTTP/1.1\r\n\r\n";check(deck::proxyParseRequest(g2,std::strlen(g2),r)&&r.port==8080);
 for(const char* bad:{"CONNECT chatgpt.com HTTP/1.1\r\n\r\n","CONNECT :443 HTTP/1.1\r\n\r\n","GET /local HTTP/1.1\r\n\r\n","GET https://x/ HTTP/1.1\r\n\r\n","CONNECT a@b:443 HTTP/1.1\r\n\r\n","CONNECT chatgpt.com:443 HTTP/1.1\r\n","BROKEN\r\n\r\n","CONNECT x:99999 HTTP/1.1\r\n\r\n"})check(!deck::proxyParseRequest(bad,std::strlen(bad),r));
 // Plain http: to an HTTP upstream the app's Proxy-* headers are replaced by ours; to SOCKS5 the origin form is used.
 char h[1024];n=deck::proxyRewriteHead(g1,std::strlen(g1),"alice","s3cr3t",false,h,sizeof h);
 check(n==std::strlen(h)&&std::string(h)=="GET http://example.com/a?b=1 HTTP/1.1\r\nProxy-Authorization: Basic YWxpY2U6czNjcjN0\r\nHost: example.com\r\nAccept: */*\r\nConnection: close\r\n\r\n");
 n=deck::proxyRewriteHead(g1,std::strlen(g1),"alice","s3cr3t",true,h,sizeof h);check(std::string(h)=="GET /a?b=1 HTTP/1.1\r\nHost: example.com\r\nAccept: */*\r\nConnection: close\r\n\r\n");
 n=deck::proxyRewriteHead(g2,std::strlen(g2),nullptr,nullptr,true,h,sizeof h);check(std::string(h)=="GET / HTTP/1.1\r\nConnection: close\r\n\r\n");
 const char* g3="POST http://h?q HTTP/1.1\r\nPROXY-CONNECTION: close\r\n\r\n";n=deck::proxyRewriteHead(g3,std::strlen(g3),nullptr,nullptr,true,h,sizeof h);check(std::string(h)=="POST /?q HTTP/1.1\r\nConnection: close\r\n\r\n");
 check(deck::proxyRewriteHead(g1,std::strlen(g1),"alice","s3cr3t",false,h,30)==0);check(deck::proxyRewriteHead("GET http://x/ HTTP/1.1\r\n",24,nullptr,nullptr,false,h,sizeof h)==0);
 // One request per connection: the app's keep-alive becomes "Connection: close"; an upgrade keeps its header.
 const char* g4="GET http://h/ HTTP/1.1\r\nConnection: keep-alive\r\nKeep-Alive: timeout=5\r\nHost: h\r\n\r\n";n=deck::proxyRewriteHead(g4,std::strlen(g4),nullptr,nullptr,true,h,sizeof h);
 check(n==std::strlen(h)&&std::string(h)=="GET / HTTP/1.1\r\nHost: h\r\nConnection: close\r\n\r\n");
 const char* g5="GET http://h/ws HTTP/1.1\r\nHost: h\r\nconnection: keep-alive, UPGRADE\r\nUpgrade: websocket\r\n\r\n";n=deck::proxyRewriteHead(g5,std::strlen(g5),"alice","s3cr3t",false,h,sizeof h);
 check(std::string(h)=="GET http://h/ws HTTP/1.1\r\nProxy-Authorization: Basic YWxpY2U6czNjcjN0\r\nHost: h\r\nconnection: keep-alive, UPGRADE\r\nUpgrade: websocket\r\n\r\n");
 check(deck::proxyUpgrade(g5,std::strlen(g5))&&!deck::proxyUpgrade(g4,std::strlen(g4))&&!deck::proxyUpgrade(g1,std::strlen(g1)));
 {const char* up="GET http://h/ HTTP/1.1\r\nUpgrade: websocket\r\n\r\n";check(!deck::proxyUpgrade(up,std::strlen(up)));} // Upgrade alone is not a request to upgrade
 // SOCKS5.
 unsigned char s[600];check(deck::socksGreeting(true,s,sizeof s)==4&&s[0]==5&&s[1]==2&&s[2]==0&&s[3]==2);check(deck::socksGreeting(false,s,sizeof s)==3&&s[1]==1&&s[2]==0);
 const unsigned char m2[]={5,2},m0[]={5,0},mff[]={5,0xFF},mgss[]={5,1},bad4[]={4,0},httpAnswer[]={'H','T'},tlsAlert[]={0x15,3};
 check(deck::socksMethod(m2,1)==-1&&deck::socksMethod(m2,2)==2&&deck::socksMethod(m0,2)==0&&deck::socksMethod(mff,2)==0xFF);
 check(deck::socksMethod(mgss,2)==1); // a method never offered: the bridge calls it a protocol error
 // Not SOCKS5 at all (SOCKS4, an HTTP server's "HTTP/1.1 400", a TLS alert): -2, never "no acceptable method".
 check(deck::socksMethod(bad4,2)==-2&&deck::socksMethod(httpAnswer,2)==-2&&deck::socksMethod(tlsAlert,2)==-2);
 n=deck::socksAuth("alice","s3cr3t",s,sizeof s);check(n==14&&s[0]==1&&s[1]==5&&!std::memcmp(s+2,"alice",5)&&s[7]==6&&!std::memcmp(s+8,"s3cr3t",6));
 check(deck::socksAuth("","x",s,sizeof s)==0);{std::string u(256,'u');check(deck::socksAuth(u.c_str(),"x",s,sizeof s)==0);}
 const unsigned char okA[]={1,0},noA[]={1,1},ok5[]={5,0},no5[]={5,1},okOdd[]={0x48,0},noOdd[]={0x48,0x54};
 check(deck::socksAuthStatus(okA,1)==-1&&deck::socksAuthStatus(okA,2)==0&&deck::socksAuthStatus(noA,2)==1);
 check(deck::socksAuthStatus(ok5,2)==0&&deck::socksAuthStatus(no5,2)==1); // version 5 instead of 1: accepted as curl does
 check(deck::socksAuthStatus(okOdd,2)==0&&deck::socksAuthStatus(noOdd,2)==-2); // the status byte decides; other versions never mean a wrong login
 n=deck::socksConnect("api.openai.com",443,s,sizeof s);check(n==7+14&&s[0]==5&&s[1]==1&&s[3]==3&&s[4]==14&&!std::memcmp(s+5,"api.openai.com",14)&&s[19]==1&&s[20]==0xBB);
 n=deck::socksConnect("10.0.0.2",80,s,sizeof s);check(n==10&&s[3]==1&&s[4]==10&&s[7]==2&&s[8]==0&&s[9]==80);
 for(const char* ip:{"256.1.1.1","1.2.3","1.2.3.4.5","1..2.3","01234.1.1.1"}){n=deck::socksConnect(ip,80,s,sizeof s);check(n>0&&s[3]==3);} // not an IPv4 literal: sent as a name
 check(deck::socksConnect("h",0,s,sizeof s)==0);check(deck::socksConnect("h",70000,s,sizeof s)==0);
 // IPv6 literals go as ATYP 4, not as a name.
 n=deck::socksConnect("2001:db8::1",443,s,sizeof s);check(n==22&&s[3]==4&&s[4]==0x20&&s[5]==0x01&&s[6]==0x0d&&s[7]==0xb8&&s[19]==1&&s[20]==1&&s[21]==0xBB);
 check(deck::socksConnect("::1",80,s,21)==0);
 {unsigned char ip[16];auto v6=[&](const char* t,const char* hex){if(!deck::proxyIPv6(t,ip))return false;char got[33];for(int i=0;i<16;++i){got[2*i]="0123456789abcdef"[ip[i]>>4];got[2*i+1]="0123456789abcdef"[ip[i]&15];}got[32]=0;return std::string(got)==hex;};
  check(v6("::","00000000000000000000000000000000"));check(v6("::1","00000000000000000000000000000001"));check(v6("1::","00010000000000000000000000000000"));
  check(v6("2001:DB8:0:0:8:800:200C:417A","20010db80000000000080800200c417a"));check(v6("fe80::1:2","fe800000000000000000000000010002"));
  check(v6("::ffff:192.0.2.1","00000000000000000000ffffc0000201"));check(v6("1:2:3:4:5:6:7:8","00010002000300040005000600070008"));check(v6("1:2:3:4:5:6:1.2.3.4","00010002000300040005000601020304"));
  for(const char* bad:{"",":","1:","1:::2",":1","1::2::3","12345::","g::1","1:2:3:4:5:6:7:8:9","1:2:3:4:5:6:7::8","1:2:3:4:5:6:7:1.2.3.4","::1.2.3","fe80::1%en0","1.2.3.4","[::1]","::1.2.3.4:5"})check(!deck::proxyIPv6(bad,ip));}
 int code;const unsigned char rep4[]={5,0,0,1,1,2,3,4,0,80},repName[]={5,0,0,3,3,'a','b','c',1,187},rep6[22]={5,0,0,4},fail[]={5,5,0,1,0,0,0,0,0,0};
 check(deck::socksReply(rep4,4,code)==-1);check(deck::socksReply(rep4,9,code)==-1);check(deck::socksReply(rep4,10,code)==10&&code==0);
 check(deck::socksReply(repName,10,code)==10&&code==0);check(deck::socksReply(rep6,22,code)==22&&code==0);check(deck::socksReply(fail,10,code)==10&&code==5);
 check(deck::socksReply(fail,1,code)==-1&&code==-1);check(deck::socksReply(fail,2,code)==2&&code==5); // a refusal needs only its first two bytes
 const unsigned char refusedOdd[]={5,4,0,9},refused255[]={5,0xFF,0,1},okOdd4[]={5,0,0,9,0},junk[]={4,0,0,1,0},httpReply[]={'H','T','T','P','/'};
 check(deck::socksReply(refusedOdd,4,code)==4&&code==4);   // REP 4 with an unknown ATYP is still socks-4
 check(deck::socksReply(refused255,4,code)==4&&code==255); // REP 255 is a server's code, not a protocol marker
 check(deck::socksReply(okOdd4,5,code)==5&&code==-2);      // success with an unknown ATYP: its end is unknown
 check(deck::socksReply(junk,5,code)>0&&code==-2);check(deck::socksReply(httpReply,5,code)==5&&code==-2); // not SOCKS5
 check(deck::socksReply(m0,2,code)==-1&&deck::socksReply(rep4,4,code)==-1); // a success waits for its whole address
 // Methods that may go to another upstream after silence.
 for(const char* yes:{"GET http://h/ HTTP/1.1\r\n\r\n","HEAD http://h/ HTTP/1.1","OPTIONS * HTTP/1.1","TRACE / HTTP/1.1","PUT /x HTTP/1.1","DELETE /x HTTP/1.1"})check(deck::proxyIdempotent(yes,std::strlen(yes)));
 for(const char* no:{"POST http://h/ HTTP/1.1","PATCH /x HTTP/1.1","get / HTTP/1.1","GETX / HTTP/1.1","CONNECT h:443 HTTP/1.1","GET",""})check(!deck::proxyIdempotent(no,std::strlen(no)));
 check(!deck::proxyIdempotent("GET / HTTP/1.1",3)); // only the bytes it is given
 // Scheme names round-trip.
 deck::ProxyScheme sc;check(deck::proxySchemeParse("socks5",sc)&&sc==deck::ProxyScheme::Socks5&&!std::strcmp(deck::proxySchemeName(sc),"socks5"));check(deck::proxySchemeParse("http",sc)&&sc==deck::ProxyScheme::Http);check(!deck::proxySchemeParse("https",sc));
 std::cout<<checks<<" proxy policy assertions passed\n";
}
