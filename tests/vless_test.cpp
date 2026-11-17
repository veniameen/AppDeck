#include "../src/vless_policy.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
int main(){
 unsigned checks=0;auto check=[&](bool pass,const char* label){++checks;if(!pass){std::cerr<<"FAILED VLESS assertion "<<checks<<": "<<label<<'\n';std::exit(1);}};
 using deck::VlessParseError;using deck::VlessSecurity;deck::VlessEndpoint result{};
 const std::string identity="01234567-89ab-cdef-0123-456789abcdef";
 const std::string base="vless://"+identity+"@vpn.example.net:443";
 const std::string tls=base+"?security=tls&type=tcp";
 const std::string publicKey="BAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8";
 const std::string reality=base+"?security=reality&type=tcp&sni=example.net&pbk="+publicKey;
 auto parse=[&](const std::string& s){return deck::vlessParse(s.data(),s.size(),result);};
 auto reject=[&](const std::string& s,VlessParseError error,const char* label){
  std::memset(&result,0x7e,sizeof result);check(parse(s)==error,label);
  check(!result.uuid[0]&&!result.host[0]&&!result.port&&!result.sni[0]&&!result.fingerprint[0]&&!result.realityPublicKey[0]&&!result.realityShortId[0]&&!result.name[0]&&!result.ipv6&&!result.vision,"failure clears all output fields");
 };
 check(parse(tls)==VlessParseError::Ok,"TLS over TCP accepted");
 check(std::string(result.uuid)==identity&&std::string(result.host)=="vpn.example.net"&&result.port==443&&result.security==VlessSecurity::Tls&&!result.ipv6&&!result.vision,"TLS fields preserved");
 check(parse(tls+"&flow=xtls-rprx-vision&sni=WWW.Example.NET&fp=chrome#Office%20A%2BB")==VlessParseError::Ok,"TLS options and encoded name accepted");
 check(result.vision&&std::string(result.sni)=="www.example.net"&&std::string(result.fingerprint)=="chrome"&&std::string(result.name)=="Office A+B","options represented without dropping values");
 check(parse(reality+"&sid=01aBcDeF&fp=firefox")==VlessParseError::Ok,"REALITY accepted");
 check(result.security==VlessSecurity::Reality&&std::string(result.realityPublicKey)==publicKey&&std::string(result.realityShortId)=="01abcdef","REALITY values preserved");
 check(parse(reality)==VlessParseError::Ok&&!result.realityShortId[0]&&std::string(result.sni)=="example.net"&&std::string(result.fingerprint)=="chrome","REALITY defaults fingerprint to chrome");
 check(parse(reality+"&sid=")==VlessParseError::Ok&&!result.realityShortId[0],"empty short ID supported");
 check(parse(reality+"&sid=0011223344556677")==VlessParseError::Ok,"eight-byte short ID supported");
 for(const char* fp:{"chrome","firefox","safari","ios","android","edge","randomized"})check(parse(tls+"&fp="+fp)==VlessParseError::Ok,"supported fingerprints");
 for(const char* host:{"[::1]","[2001:DB8::1]","[::ffff:192.0.2.1]","[1:2:3:4:5:6:7:8]"}){
  check(parse("vless://"+identity+"@"+host+":65535?type=tcp&security=tls")==VlessParseError::Ok&&result.ipv6&&result.port==65535,"valid bracketed IPv6");
 }
 check(parse("vless://01234567-89AB-CDEF-0123-456789ABCDEF@%76pn.EXAMPLE.net:1?%73ecurity=tls&type=tcp")==VlessParseError::Ok,"encoded host/query and uppercase UUID accepted");
 check(std::string(result.uuid)==identity&&std::string(result.host)=="vpn.example.net","UUID and DNS spelling normalized");
 check(parse("vless://"+identity+"@192.0.2.1:443?security=tls&type=tcp")==VlessParseError::Ok&&!result.ipv6,"IPv4 literal accepted");
 check(parse(tls+"#%D0%9F%D1%80%D0%BE%D1%84%D0%B8%D0%BB%D1%8C%20%F0%9F%94%92")==VlessParseError::Ok,"percent-encoded UTF-8 display name accepted");
 check(parse(tls+"#A+B%26C%23D%25")==VlessParseError::Ok&&std::string(result.name)=="A+B&C#D%","fragment decoding is not form decoding");
 reject("",VlessParseError::Empty,"empty URI");check(deck::vlessParse(nullptr,result)==VlessParseError::Empty,"null URI");
 reject("https://"+tls.substr(8),VlessParseError::Scheme,"other schemes rejected");
 reject("VLESS://"+tls.substr(8),VlessParseError::Scheme,"noncanonical scheme rejected");
 reject(base,VlessParseError::Query,"missing query");reject(base+"?",VlessParseError::Query,"empty query");
 reject("vless://vpn.example.net:443?security=tls&type=tcp",VlessParseError::Authority,"missing identity");
 reject("vless://short@vpn.example.net:443?security=tls&type=tcp",VlessParseError::Uuid,"non-UUID credential rejected");
 reject("vless://01234567%2D89ab-cdef-0123-456789abcdef@vpn.example.net:443?security=tls&type=tcp",VlessParseError::Uuid,"UUID must use canonical delimiters");
 for(const char* host:{"[::1","[]","[12345::]","[1::2::3]","[fe80::1%25en0]","-bad.example","bad-.example","bad..example","bad_example","127.1","192.168.001.1","256.0.0.1","1234","bad/example","bad@host"}){
  std::string uri="vless://"+identity+"@"+host+":443?security=tls&type=tcp";
  reject(uri,VlessParseError::Host,"malformed or ambiguous host rejected");
 }
 for(const char* port:{"0","65536","-1","443x","","123456","443:80"})reject("vless://"+identity+"@vpn.example.net:"+port+"?security=tls&type=tcp",VlessParseError::Port,"invalid port");
 reject("vless://"+identity+"@bad%2:443?security=tls&type=tcp",VlessParseError::Encoding,"invalid host percent escape");
 reject("vless://"+identity+"@bad%2fhost:443?security=tls&type=tcp",VlessParseError::Host,"decoded host delimiter rejected");
 reject("vless://"+identity+"@bad%00host:443?security=tls&type=tcp",VlessParseError::Control,"encoded host NUL rejected");
 reject(base+"?type=tcp",VlessParseError::Security,"missing security never means cleartext");
 check(parse(base+"?security=tls")==VlessParseError::Ok,"omitted transport defaults to TCP");
 check(parse(tls+"&encryption=none")==VlessParseError::Ok,"explicit VLESS encryption none accepted inside TLS");
 reject(tls+"&encryption=auto",VlessParseError::Encryption,"unsupported VLESS encryption rejected");
 reject(tls+"&encryption=none&encryption=none",VlessParseError::DuplicateOption,"duplicate encryption rejected");
 for(const char* security:{"none","","xtls","TLS"})reject(base+"?security="+security+"&type=tcp",VlessParseError::Security,"unsupported security rejected");
 for(const char* type:{"ws","grpc","xhttp","httpupgrade","raw",""})reject(base+"?security=tls&type="+type,VlessParseError::Transport,"unsupported transport rejected");
 for(const char* key:{"allowInsecure","insecure","skip-cert-verify","alpn","host","path","serviceName","unknown"})reject(tls+"&"+key+"=1",VlessParseError::UnsupportedOption,"unsupported options never ignored");
 reject(tls+"&security=reality",VlessParseError::DuplicateOption,"duplicate security rejected");
 reject(tls+"&%74ype=tcp",VlessParseError::DuplicateOption,"encoded duplicate key rejected");
 for(const char* option:{"flow=xtls-rprx-vision","sni=example.net","fp=chrome"})reject(tls+"&"+option+"&"+option,VlessParseError::DuplicateOption,"duplicate optional key rejected");
 reject(reality+"&pbk="+publicKey,VlessParseError::DuplicateOption,"duplicate public key rejected");
 reject(reality+"&sid=&sid=",VlessParseError::DuplicateOption,"duplicate empty short ID rejected");
 reject(tls+"&flow=xtls-rprx-direct",VlessParseError::Flow,"unsupported flow");
 check(parse(tls+"&flow=")==VlessParseError::Ok&&!result.vision,"explicit empty flow means no flow");
 reject(tls+"&sni=192.0.2.1",VlessParseError::ServerName,"IP is not DNS SNI");
 reject(tls+"&sni=bad+name",VlessParseError::ServerName,"plus never becomes space in SNI");
 reject(tls+"&fp=random",VlessParseError::Fingerprint,"unsupported fingerprint");
 reject(base+"?security=reality&type=tcp",VlessParseError::RealityKey,"REALITY requires public key");
 reject(base+"?security=reality&type=tcp&pbk="+publicKey,VlessParseError::ServerName,"REALITY requires explicit server name");
 for(const std::string& key:{publicKey.substr(1),publicKey+"=",publicKey.substr(0,42)+"9",publicKey.substr(0,42)+"+",publicKey.substr(0,42)+"/"})reject(base+"?security=reality&type=tcp&pbk="+key,VlessParseError::RealityKey,"canonical 32-byte base64url required");
 for(const char* sid:{"0","001","gg","001122334455667788"})reject(reality+"&sid="+sid,VlessParseError::ShortId,"short ID is zero to eight hex bytes");
 reject(tls+"&pbk="+publicKey,VlessParseError::IncompatibleOption,"TLS cannot silently discard REALITY public key");
 reject(tls+"&sid=",VlessParseError::IncompatibleOption,"TLS cannot silently discard REALITY short ID");
 for(const char* suffix:{"&","&&fp=chrome","&fp","&=chrome"})reject(tls+suffix,VlessParseError::Query,"malformed query delimiters");
 reject(tls+"&fp=chro%ZZme",VlessParseError::Encoding,"malformed option escape");
 for(const char* suffix:{"#%","#%0","#%GG","#%C0%AF","#%ED%A0%80","#%F4%90%80%80","#%E2%82"})reject(tls+suffix,VlessParseError::Encoding,"malformed UTF-8 or escape in display name");
 for(const char* suffix:{"#%00","#%0A","#%7F","#%C2%85","#%E2%80%A8","#%E2%80%AE","&fp=chrome%0d%0a"})reject(tls+suffix,VlessParseError::Control,"decoded controls rejected");
 reject(tls+"#raw space",VlessParseError::Encoding,"raw spaces require percent encoding");
 reject(tls+"\n",VlessParseError::Control,"raw trailing control rejected");
 reject(tls+std::string(1,'\0')+"ignored",VlessParseError::Control,"embedded NUL cannot truncate imported link");
 reject(tls+"&sni="+std::string(64,'a')+".net",VlessParseError::ServerName,"oversized DNS label rejected");
 reject(tls+"#"+std::string(257,'a'),VlessParseError::TooLong,"oversized display name rejected");
 check(parse(tls+"#"+std::string(256,'a'))==VlessParseError::Ok&&std::strlen(result.name)==256,"maximum display name fits");
 reject(tls+"&fp="+std::string(257,'a'),VlessParseError::TooLong,"oversized option rejected");
 reject(std::string(deck::vlessUriCap+1,'x'),VlessParseError::TooLong,"bounded URI input");
 check(deck::vlessParse(std::string(deck::vlessUriCap+1,'x').c_str(),result)==VlessParseError::TooLong,"C-string overload is bounded");
 // Exhaustively inject ASCII controls into each component; tests never print a source URI.
 for(unsigned c=0;c<0x80;++c)if(c<0x20||c==0x7f){char escape[4];std::snprintf(escape,sizeof escape,"%%%02X",c);
  reject(tls+"#"+escape,VlessParseError::Control,"all escaped ASCII controls rejected");
  reject(tls+std::string(1,(char)c),VlessParseError::Control,"all literal ASCII controls rejected");
 }
 // Every prefix is safe to parse, including buffers without a terminating NUL.
 for(std::size_t n=0;n<reality.size();++n){deck::VlessEndpoint partial{};auto error=deck::vlessParse(reality.data(),n,partial);check(error!=VlessParseError::Ok,"truncated REALITY link rejected");}
 std::cout<<checks<<" VLESS parser assertions passed\n";
}
