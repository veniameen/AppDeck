#pragma once
// Bounded, allocation-free VLESS share-link parsing. This validates configuration only;
// it neither opens a tunnel nor establishes process/network isolation.
#include "proxy_policy.hpp"
namespace deck {
constexpr Size vlessUriCap=4096;
enum class VlessSecurity{Tls,Reality};
enum class VlessParseError{Ok,Empty,TooLong,Scheme,Authority,Uuid,Host,Port,Encoding,Control,
 Query,DuplicateOption,UnsupportedOption,Security,Transport,Flow,ServerName,Fingerprint,RealityKey,ShortId,IncompatibleOption,Encryption};
struct VlessEndpoint{
 char uuid[37],host[254],sni[254],fingerprint[16],realityPublicKey[44],realityShortId[17],name[257];
 unsigned port;VlessSecurity security;bool ipv6,vision;
};
inline int vlessHex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
inline char vlessLower(char c){return c>='A'&&c<='Z'?(char)(c-'A'+'a'):c;}
inline bool vlessAlphaNumeric(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}
// Decode once, with '+' kept literal (a URI query is not HTML form data).
inline VlessParseError vlessDecode(const char* from,const char* end,char* out,Size cap){
 Size used=0;for(const char* p=from;p<end;++p){unsigned char c=(unsigned char)*p;
  if(c=='%'){if(end-p<3||vlessHex(p[1])<0||vlessHex(p[2])<0)return VlessParseError::Encoding;c=(unsigned char)(vlessHex(p[1])*16+vlessHex(p[2]));p+=2;}
  if(c<0x20||c==0x7f)return VlessParseError::Control;
  if(used+1>=cap)return VlessParseError::TooLong;out[used++]=(char)c;
 }out[used]=0;return VlessParseError::Ok;
}
// Strict ASCII DNS names; numeric-only forms must be unambiguous dotted IPv4.
// SNI uses DNS names only. Internationalized names must already be ASCII IDNA.
inline bool vlessDnsName(const char* s,bool allowAddress){
 Size n=length(s);if(!n||n>253)return false;bool numeric=true;
 for(Size i=0;i<n;++i)if((s[i]<'0'||s[i]>'9')&&s[i]!='.')numeric=false;
 if(numeric){if(!allowAddress)return false;unsigned char ip[4];if(!proxyIPv4(s,ip))return false;
  for(Size i=0;i<n;++i)if((i==0||s[i-1]=='.')&&s[i]=='0'&&i+1<n&&s[i+1]!='.')return false;return true;}
 Size label=0;for(Size i=0;i<n;++i){char c=s[i];if(c=='.'){if(!label||s[i-1]=='-')return false;label=0;}
  else{if(!vlessAlphaNumeric(c)&&c!='-')return false;if(!label&&c=='-')return false;if(++label>63)return false;}}
 return (label&&s[n-1]!='-')||(n>1&&s[n-1]=='.');
}
inline int vlessBase64Url(char c){
 if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;
 if(c>='0'&&c<='9')return c-'0'+52;return c=='-'?62:c=='_'?63:-1;
}
inline bool vlessRealityKey(const char* s){
 if(length(s)!=43)return false;for(Size i=0;i<43;++i)if(vlessBase64Url(s[i])<0)return false;
 return (vlessBase64Url(s[42])&3)==0; // Exactly 32 bytes; unused RFC 4648 pad bits must be zero.
}
inline VlessParseError vlessDisplayName(const char* s){
 Size n=length(s);for(Size i=0;i<n;){unsigned char lead=(unsigned char)s[i++];unsigned code=lead,tail=0,minimum=0;
  if(lead>=0x80){if(lead>=0xc2&&lead<=0xdf){tail=1;code=lead&31;minimum=0x80;}
   else if(lead>=0xe0&&lead<=0xef){tail=2;code=lead&15;minimum=0x800;}
   else if(lead>=0xf0&&lead<=0xf4){tail=3;code=lead&7;minimum=0x10000;}else return VlessParseError::Encoding;
   if(tail>n-i)return VlessParseError::Encoding;
   for(unsigned j=0;j<tail;++j){unsigned char c=(unsigned char)s[i++];if((c&0xc0)!=0x80)return VlessParseError::Encoding;code=code<<6|(c&63);}
   if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return VlessParseError::Encoding;
  }
  if(code<0x20||(code>=0x7f&&code<=0x9f)||code==0x2028||code==0x2029||(code>=0x202a&&code<=0x202e)||(code>=0x2066&&code<=0x2069))return VlessParseError::Control;
 }return VlessParseError::Ok;
}
// Prefer this length-aware overload for imported data: embedded NUL is rejected.
// A rejection always clears out; errors contain no source text or credentials.
inline VlessParseError vlessParse(const char* uri,Size size,VlessEndpoint& out){
 out={};if(!uri||!size)return VlessParseError::Empty;if(size>vlessUriCap)return VlessParseError::TooLong;
 for(Size i=0;i<size;++i){unsigned char c=(unsigned char)uri[i];if(c<0x20||c==0x7f)return VlessParseError::Control;
  if(c==' '||c>=0x80)return VlessParseError::Encoding;}
 const char* scheme="vless://";if(size<8)return VlessParseError::Scheme;
 for(Size i=0;i<8;++i)if(uri[i]!=scheme[i])return VlessParseError::Scheme;
 const char* end=uri+size;const char* query=uri+8;while(query<end&&*query!='?'&&*query!='#')++query;
 if(query==end||*query!='?')return VlessParseError::Query;
 const char* user=uri+8;const char* at=user;while(at<query&&*at!='@')++at;
 if(at==query||at==user)return VlessParseError::Authority;VlessEndpoint result{};
 if(at-user!=36||!proxyCopy(user,at,result.uuid,sizeof result.uuid)||!uuid(result.uuid))return VlessParseError::Uuid;
 for(char& c:result.uuid)c=vlessLower(c);
 const char* host=at+1;const char* port=nullptr;if(host==query)return VlessParseError::Host;
 if(*host=='['){const char* close=host+1;while(close<query&&*close!=']')++close;
  if(close==query||close+1==query||close[1]!=':'||!proxyCopy(host+1,close,result.host,sizeof result.host))return VlessParseError::Host;
  unsigned char address[16];if(!proxyIPv6(result.host,address))return VlessParseError::Host;result.ipv6=true;port=close+2;
 }else{const char* stop=host;while(stop<query&&*stop!=':')++stop;
  if(stop==query)return VlessParseError::Authority;
  VlessParseError err=vlessDecode(host,stop,result.host,sizeof result.host);if(err!=VlessParseError::Ok)return err;
  if(!vlessDnsName(result.host,true))return VlessParseError::Host;port=stop+1;
 }
 if(!proxyPort(port,query,result.port))return VlessParseError::Port;
 for(char& c:result.host)c=vlessLower(c);
 const char* fragment=query+1;while(fragment<end&&*fragment!='#')++fragment;
 if(fragment==query+1)return VlessParseError::Query;
 unsigned seen=0;const char* p=query+1;
 while(p<fragment){const char* stop=p;while(stop<fragment&&*stop!='&')++stop;const char* equals=p;
  while(equals<stop&&*equals!='=')++equals;if(equals==p||equals==stop)return VlessParseError::Query;
  char key[32],value[257];VlessParseError err=vlessDecode(p,equals,key,sizeof key);if(err!=VlessParseError::Ok)return err;
  err=vlessDecode(equals+1,stop,value,sizeof value);if(err!=VlessParseError::Ok)return err;
  const char* keys[]={"security","type","flow","sni","fp","pbk","sid","encryption"};unsigned index=0;
  while(index<8&&!equal(key,keys[index]))++index;if(index==8)return VlessParseError::UnsupportedOption;
  unsigned bit=1u<<index;if(seen&bit)return VlessParseError::DuplicateOption;seen|=bit;
  switch(index){
   case 0:if(equal(value,"tls"))result.security=VlessSecurity::Tls;else if(equal(value,"reality"))result.security=VlessSecurity::Reality;else return VlessParseError::Security;break;
   case 1:if(!equal(value,"tcp"))return VlessParseError::Transport;break;
   case 2:if(value[0]&&!equal(value,"xtls-rprx-vision"))return VlessParseError::Flow;result.vision=value[0]!=0;break;
   case 3:if(!vlessDnsName(value,false))return VlessParseError::ServerName;
    for(Size i=0;i<=length(value);++i)result.sni[i]=vlessLower(value[i]);break;
   case 4:if(!equal(value,"chrome")&&!equal(value,"firefox")&&!equal(value,"safari")&&!equal(value,"ios")&&!equal(value,"android")&&!equal(value,"edge")&&!equal(value,"randomized"))return VlessParseError::Fingerprint;
    proxyCopy(value,value+length(value),result.fingerprint,sizeof result.fingerprint);break;
   case 5:if(!vlessRealityKey(value))return VlessParseError::RealityKey;proxyCopy(value,value+43,result.realityPublicKey,sizeof result.realityPublicKey);break;
   case 6:{Size n=length(value);if(n>16||(n&1))return VlessParseError::ShortId;
    for(Size i=0;i<n;++i){if(vlessHex(value[i])<0)return VlessParseError::ShortId;result.realityShortId[i]=vlessLower(value[i]);}break;}
   case 7:if(!equal(value,"none"))return VlessParseError::Encryption;break;
  }
  if(stop==fragment)break;p=stop+1;if(p==fragment)return VlessParseError::Query;
 }
 // Omitted transport means TCP. VLESS's own encryption=none still requires TLS/REALITY above it.
 if(!(seen&1))return VlessParseError::Security;
 if(result.security==VlessSecurity::Reality){if(!(seen&(1u<<5)))return VlessParseError::RealityKey;
  if(!result.sni[0])return VlessParseError::ServerName;
  if(!result.fingerprint[0]){const char* fp="chrome";proxyCopy(fp,fp+6,result.fingerprint,sizeof result.fingerprint);}}
 else if(seen&((1u<<5)|(1u<<6)))return VlessParseError::IncompatibleOption;
 if(!result.sni[0])proxyCopy(result.host,result.host+length(result.host),result.sni,sizeof result.sni);
 if(fragment<end){VlessParseError err=vlessDecode(fragment+1,end,result.name,sizeof result.name);if(err!=VlessParseError::Ok)return err;
  err=vlessDisplayName(result.name);if(err!=VlessParseError::Ok)return err;}
 out=result;return VlessParseError::Ok;
}
inline VlessParseError vlessParse(const char* uri,VlessEndpoint& out){
 if(!uri){out={};return VlessParseError::Empty;}Size n=0;while(n<=vlessUriCap&&uri[n])++n;return vlessParse(uri,n,out);
}
}
