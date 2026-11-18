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
enum class Adapter{Codex,VSCode,Chromium,Electron,Claude,Generic,Unsupported};
inline const char* adapterName(Adapter a){switch(a){case Adapter::Codex:return "codex";case Adapter::VSCode:return "vscode";case Adapter::Chromium:return "chromium";case Adapter::Electron:return "electron";case Adapter::Claude:return "claude";case Adapter::Generic:return "generic";default:return "unsupported";}}
inline Adapter adapter(const char* a){if(equal(a,"codex"))return Adapter::Codex;if(equal(a,"vscode"))return Adapter::VSCode;if(equal(a,"chromium"))return Adapter::Chromium;if(equal(a,"electron"))return Adapter::Electron;if(equal(a,"claude"))return Adapter::Claude;if(equal(a,"generic"))return Adapter::Generic;return Adapter::Unsupported;}
inline bool supportsProfiles(Adapter a){return a==Adapter::Codex||a==Adapter::VSCode||a==Adapter::Chromium||a==Adapter::Electron||a==Adapter::Claude;}
inline bool proxyUsesChromiumFlags(Adapter a){return supportsProfiles(a);}
inline Adapter detect(const char* bundle,const char* name,bool electron){
 if(equal(bundle,"com.openai.codex")||equal(bundle,"com.openai.codex.desktop")||equal(name,"Codex"))return Adapter::Codex;
 // Claude has its own adapter; current builds use Chromium's --user-data-dir.
 if(equal(bundle,"com.anthropic.claudefordesktop"))return Adapter::Claude;
 if(prefix(bundle,"com.microsoft.VSCode")||equal(bundle,"com.todesktop.230313mzl4w4u92"))return Adapter::VSCode;
 if(prefix(bundle,"com.google.Chrome")||prefix(bundle,"com.brave.Browser")||prefix(bundle,"com.microsoft.edgemac")||equal(bundle,"org.chromium.Chromium"))return Adapter::Chromium;
 return electron?Adapter::Electron:Adapter::Generic;
}
inline bool authEnv(const char* k){return equal(k,"OPENAI_API_KEY")||equal(k,"CODEX_API_KEY")||equal(k,"CODEX_ACCESS_TOKEN")||equal(k,"OPENAI_ACCESS_TOKEN")||equal(k,"CODEX_CHATGPT_ACCESS_TOKEN");}
inline bool neverShare(const char* n){return equal(n,"auth.json")||equal(n,".credentials.json")||equal(n,"sessions")||equal(n,"state_5.sqlite")||equal(n,"history.jsonl")||equal(n,"user-data");}
}
