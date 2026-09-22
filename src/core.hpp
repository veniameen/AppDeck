#pragma once
// Dependency-free C++17 policy helpers. Tests run both on Linux and macOS.
namespace deck {
using Size=unsigned long;
inline Size length(const char* s){Size n=0;if(s)while(s[n])++n;return n;}
inline bool equal(const char* a,const char* b){if(!a||!b)return a==b;while(*a&&*a==*b){++a;++b;}return *a==*b;}
inline bool prefix(const char* a,const char* p){if(!a||!p)return false;while(*p)if(*a++!=*p++)return false;return true;}
inline bool suffix(const char* a,const char* b){auto n=length(a),m=length(b);return n>=m&&equal(a+n-m,b);}
inline bool uuid(const char* s){if(length(s)!=36)return false;for(int i=0;i<36;++i){if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')))return false;}return true;}
inline bool inside(const char* base,const char* path){Size n=length(base);return n&&prefix(path,base)&&(path[n]=='/'||path[n]=='\0');}
inline bool safeLeaf(const char* s){if(!s||!*s||equal(s,".")||equal(s,".."))return false;for(;*s;++s)if(*s=='/'||*s=='\\')return false;return true;}
enum class Adapter{Codex,VSCode,Chromium,Electron,Claude,Unsupported};
inline const char* adapterName(Adapter a){switch(a){case Adapter::Codex:return "codex";case Adapter::VSCode:return "vscode";case Adapter::Chromium:return "chromium";case Adapter::Electron:return "electron";case Adapter::Claude:return "claude";default:return "unsupported";}}
inline Adapter adapter(const char* a){if(equal(a,"codex"))return Adapter::Codex;if(equal(a,"vscode"))return Adapter::VSCode;if(equal(a,"chromium"))return Adapter::Chromium;if(equal(a,"electron"))return Adapter::Electron;if(equal(a,"claude"))return Adapter::Claude;return Adapter::Unsupported;}
inline Adapter detect(const char* bundle,const char* name,bool electron){
 if(equal(bundle,"com.openai.codex")||equal(bundle,"com.openai.codex.desktop")||equal(name,"Codex"))return Adapter::Codex;
 // Claude Desktop honours CLAUDE_USER_DATA_DIR (its own switch for the Electron user-data folder).
 if(equal(bundle,"com.anthropic.claudefordesktop"))return Adapter::Claude;
 if(prefix(bundle,"com.microsoft.VSCode")||equal(bundle,"com.todesktop.230313mzl4w4u92"))return Adapter::VSCode;
 if(prefix(bundle,"com.google.Chrome")||prefix(bundle,"com.brave.Browser")||prefix(bundle,"com.microsoft.edgemac")||equal(bundle,"org.chromium.Chromium"))return Adapter::Chromium;
 return electron?Adapter::Electron:Adapter::Unsupported;
}
// Set only a top-level authentication store. Preserve all workspace restrictions.
// Reject ambiguous constructs instead of silently rewriting arbitrary TOML.
// This intentionally is NOT a general TOML parser. Output must fit cap.
enum class ConfigResult{Ok,TooLarge,Ambiguous};
inline ConfigResult codexConfig(const char* in,char* out,Size cap){
 const char* header="# Managed by AppDeck: separate file-based login per profile.\ncli_auth_credentials_store = \"file\"\n";
 Size used=0;auto emit=[&](const char* p,Size n){if(used+n>=cap)return false;for(Size k=0;k<n;++k)out[used++]=p[k];return true;};
 if(!emit(header,length(header)))return ConfigResult::TooLarge;
 bool root=true;const char* p=in?in:"";
 // Multiline strings can contain text that looks like keys/tables. Fail closed.
 for(const char* q=p;*q;++q)if((q[0]=='\"'&&q[1]=='\"'&&q[2]=='\"')||(q[0]=='\''&&q[1]=='\''&&q[2]=='\''))return ConfigResult::Ambiguous;
 while(*p){const char* end=p;while(*end&&*end!='\n')++end;const char* t=p;while(t<end&&(*t==' '||*t=='\t'||*t=='\r'))++t;
  if(t<end&&*t=='[')root=false;
  bool skip=prefix(t,"# Managed by AppDeck: separate file-based login per profile.");
  if(root&&t<end&&*t!='#'){
   const char* key="cli_auth_credentials_store";Size k=length(key);
   if(prefix(t,key)){const char* z=t+k;while(z<end&&(*z==' '||*z=='\t'))++z;if(z<end&&*z=='=')skip=true;}
   // A quoted or dotted form is legal TOML, but outside this small editor's remit.
   if((*t=='\"'||*t=='\'')&&prefix(t+1,key))return ConfigResult::Ambiguous;
   if(prefix(t,key)&&!skip)return ConfigResult::Ambiguous;
  }
  Size n=(Size)(end-p)+(*end=='\n'?1:0);
  if(!skip&&!emit(p,n))return ConfigResult::TooLarge;
  p=end+(*end=='\n'?1:0);
 }
 if(used&&out[used-1]!='\n'&&!emit("\n",1))return ConfigResult::TooLarge;
 out[used]=0;return ConfigResult::Ok;
}
inline bool authEnv(const char* k){return equal(k,"OPENAI_API_KEY")||equal(k,"CODEX_API_KEY")||equal(k,"CODEX_ACCESS_TOKEN")||equal(k,"OPENAI_ACCESS_TOKEN")||equal(k,"CODEX_CHATGPT_ACCESS_TOKEN");}
inline bool neverShare(const char* n){return equal(n,"auth.json")||equal(n,".credentials.json")||equal(n,"sessions")||equal(n,"state_5.sqlite")||equal(n,"history.jsonl")||equal(n,"user-data");}
}
