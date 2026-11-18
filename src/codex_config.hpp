#pragma once
#include "core.hpp"

namespace deck {
inline constexpr char bundledMarketplaceSuffix[]="/.tmp/bundled-marketplaces/openai-bundled";
struct ConfigSpan {const char* begin=nullptr;const char* end=nullptr;};

inline const char* configWhitespace(const char* p){
 while(*p==' '||*p=='\t'||*p=='\r')++p;
 return p;
}
// Match one bare or quoted TOML key, leaving its delimiter for the caller.
inline bool configKey(const char*& p,const char* key){
 p=configWhitespace(p);char quote=(*p=='"'||*p=='\'')?*p++:0;
 if(!prefix(p,key))return false;
 p+=length(key);
 if(quote&&*p++!=quote)return false;
 p=configWhitespace(p);return true;
}
inline bool configLineEnd(const char* p){
 p=configWhitespace(p);return !*p||*p=='\n'||*p=='#';
}
inline bool bundledMarketplaceTable(const char* p){
 return *p++=='['&&configKey(p,"marketplaces")&&*p++=='.'&&
        configKey(p,"openai-bundled")&&*p++==']'&&configLineEnd(p);
}
inline ConfigSpan configStringField(const char* p,const char* key){
 if(!configKey(p,key)||*p++!='=')return {};
 p=configWhitespace(p);const char* start=p;char quote=*p++;
 if(quote!='"'&&quote!='\'')return {};
 while(*p&&*p!='\n'&&*p!=quote){
  if(quote=='"'&&*p=='\\'&&p[1]&&p[1]!='\n')++p;
  ++p;
 }
 if(*p!=quote||!configLineEnd(p+1))return {};
 return {start,p+1};
}
// Keep multiline arrays and inline tables together. Their elements cannot set the
// enclosing table's source. Multiline strings are rejected by codexConfig below.
inline const char* configStatementEnd(const char* p){
 unsigned depth=0;char quote=0;
 while(*p){
  char c=*p;
  if(c=='\n'){if(!depth||quote)break;}
  else if(quote){
   if(c==quote)quote=0;else if(c=='\\'&&quote=='"'&&p[1]&&p[1]!='\n')++p;
  }else if(c=='#'){while(*p&&*p!='\n')++p;continue;}
  else if(c=='\''||c=='"')quote=c;
  else if(c=='['||c=='{')++depth;
  else if((c==']'||c=='}')&&depth)--depth;
  ++p;
 }
 return p;
}
inline ConfigSpan bundledMarketplaceSource(const char* config){
 bool inTable=false,local=false;
 ConfigSpan source;
 for(const char* p=config;*p;){
  const char* key=configWhitespace(p);
  if(*key=='['){
   if(inTable)break;
   inTable=bundledMarketplaceTable(key);
  }else if(inTable){
   ConfigSpan type=configStringField(key,"source_type");
   if(type.begin)local=type.end-type.begin==7&&prefix(type.begin+1,"local");
   ConfigSpan value=configStringField(key,"source");
   if(value.begin)source=value;
  }
  const char* end=configStatementEnd(p);p=*end?end+1:end;
 }
 Size suffixLen=sizeof(bundledMarketplaceSuffix)-1;
 if(local&&source.begin&&source.begin[1]=='/'&&(Size)(source.end-source.begin)>=suffixLen+2&&
    prefix(source.end-1-suffixLen,bundledMarketplaceSuffix))return source;
 return {};
}

// Project only the authentication store and Desktop's generated bundled marketplace
// path. Unknown tables, plugin preferences and organization restrictions pass through.
enum class ConfigResult{Ok,TooLarge,Ambiguous};
inline ConfigResult codexConfig(const char* in,char* out,Size cap,const char* profileHome=nullptr){
 const char* header="# Managed by AppDeck: separate file-based login per profile.\ncli_auth_credentials_store = \"file\"\n";
 Size used=0;auto emit=[&](const char* p,Size n){if(used+n>=cap)return false;for(Size k=0;k<n;++k)out[used++]=p[k];return true;};
 if(!emit(header,length(header)))return ConfigResult::TooLarge;
 bool root=true;const char* p=in?in:"";
 // Multiline strings can contain text that looks like keys/tables. Fail closed.
 for(const char* q=p;*q;++q)if((q[0]=='\"'&&q[1]=='\"'&&q[2]=='\"')||(q[0]=='\''&&q[1]=='\''&&q[2]=='\''))return ConfigResult::Ambiguous;
 ConfigSpan source=profileHome?bundledMarketplaceSource(p):ConfigSpan{};
 while(*p){const char* end=configStatementEnd(p);const char* t=configWhitespace(p);
  if(t<end&&*t=='[')root=false;
  bool skip=prefix(t,"# Managed by AppDeck: separate file-based login per profile.");
  if(root&&t<end&&*t!='#'){
   const char* key="cli_auth_credentials_store";Size k=length(key);
   if(prefix(t,key)){const char* z=t+k;while(z<end&&(*z==' '||*z=='\t'))++z;if(z<end&&*z=='='){
    if(!configStringField(t,key).begin)return ConfigResult::Ambiguous;
    skip=true;
   }}
   // A quoted or dotted form is legal TOML, but outside this small editor's remit.
   if((*t=='\"'||*t=='\'')&&prefix(t+1,key))return ConfigResult::Ambiguous;
   if(prefix(t,key)&&!skip)return ConfigResult::Ambiguous;
  }
  Size n=(Size)(end-p)+(*end=='\n'?1:0);
  if(source.begin&&source.begin>=p&&source.begin<end){
   if(!emit(p,(Size)(source.begin-p))||!emit("\"",1))return ConfigResult::TooLarge;
   for(const char* s=profileHome;*s;++s){
    unsigned char c=(unsigned char)*s;
    if(c<32||c==127){
     const char hex[]="0123456789abcdef";
     char escaped[]={'\\','u','0','0',hex[c>>4],hex[c&15]};
     if(!emit(escaped,sizeof escaped))return ConfigResult::TooLarge;
    }else{
     if((c=='"'||c=='\\')&&!emit("\\",1))return ConfigResult::TooLarge;
     if(!emit(s,1))return ConfigResult::TooLarge;
    }
   }
   if(!emit(bundledMarketplaceSuffix,sizeof(bundledMarketplaceSuffix)-1)||!emit("\"",1)||
      !emit(source.end,(Size)(p+n-source.end)))return ConfigResult::TooLarge;
  }else if(!skip&&!emit(p,n))return ConfigResult::TooLarge;
  p=end+(*end=='\n'?1:0);
 }
 if(used&&out[used-1]!='\n'&&!emit("\n",1))return ConfigResult::TooLarge;
 out[used]=0;return ConfigResult::Ok;
}
}
