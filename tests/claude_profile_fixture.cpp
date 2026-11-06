// A native stand-in for launch configuration checks. No vendor code or network requests.
#include "../src/mac.hpp"
using namespace mac;
int main(int argc,char** argv){
 const char* userData=nullptr;for(int i=1;i<argc;++i)if(strncmp(argv[i],"--user-data-dir=",16)==0)userData=argv[i]+16;
 constexpr const char prefix[]="/private/tmp/appdeck-claude-profile-test-";
 if(!userData||strncmp(userData,prefix,sizeof(prefix)-1)!=0)return 2;
 umask(0077);if(!dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8))return 10;
 Pool pool;Obj app=send(cls("NSApplication"),"sharedApplication");send<bool>(app,"setActivationPolicy:",(Int)1);
 Obj result=dict(),arguments=array(),environment=dict();for(int i=0;i<argc;++i)add(arguments,str(argv[i]));
 put(result,"arguments",arguments);put(result,"environment",environment);
 Obj source=send(send(cls("NSProcessInfo"),"processInfo"),"environment");
 for(const char* key:{"HOME","CFFIXED_USER_HOME","TMPDIR","MAC_CHROMIUM_TMPDIR","XDG_CONFIG_HOME","XDG_CACHE_HOME","XDG_DATA_HOME","XDG_STATE_HOME","CLAUDE_USER_DATA_DIR","CLAUDE_CONFIG_DIR","CLAUDE_SECURESTORAGE_CONFIG_DIR","CLAUDE_CODE_TMPDIR","CLAUDECODE","ANTHROPIC_API_KEY","ANTHROPIC_AUTH_TOKEN","ANTHROPIC_BASE_URL","CLAUDE_CODE_OAUTH_TOKEN","HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy","NO_PROXY","no_proxy","grpc_proxy","GRPC_PROXY","no_grpc_proxy","NO_GRPC_PROXY","NODE_USE_ENV_PROXY","CLAUDE_CODE_HTTP_PROXY","CLAUDE_CODE_HTTPS_PROXY","CLAUDE_CODE_PROXY_RESOLVES_HOSTS","CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC","DISABLE_TELEMETRY","DISABLE_ERROR_REPORTING","DISABLE_GROWTHBOOK","DO_NOT_TRACK"})put(environment,key,get(source,key));
 Obj fm=send(cls("NSFileManager"),"defaultManager"),nativeHome=get(source,"HOME"),marker=join(str(userData),"fixture-first-run.txt");
 put(result,"workingDirectory",send(fm,"currentDirectoryPath"));
 put(result,"firstRun",boolean(!send<bool>(fm,"fileExistsAtPath:",marker)));
 put(result,"sourceSentinelPresent",boolean(send<bool>(fm,"fileExistsAtPath:",join(nativeHome,".claude/source-sentinel.txt"))));
 put(result,"pid",num(send<int>(send(cls("NSProcessInfo"),"processInfo"),"processIdentifier")));
 Obj codeOverride=get(source,"CLAUDE_CONFIG_DIR"),codeRoot=codeOverride?codeOverride:join(nativeHome,".claude");
 // This fixture may inspect only its scratch paths and never writes to the source Code folder.
 if(strncmp(utf8(codeRoot),prefix,sizeof(prefix)-1)!=0)return 5;
 put(result,"codeRoot",codeRoot);put(result,"codeSourceSentinelPresent",boolean(send<bool>(fm,"fileExistsAtPath:",join(codeRoot,"source-sentinel.txt"))));
 Obj codeMarker=join(codeRoot,"fixture-code-first-run.txt");bool codeFirstRun=!send<bool>(fm,"fileExistsAtPath:",codeMarker);put(result,"codeFirstRun",boolean(codeFirstRun));
 if(codeOverride){Obj codeError=nullptr;
  if(codeFirstRun&&!send<bool>(str(userData),"writeToFile:atomically:encoding:error:",codeMarker,true,(UInt)4,&codeError))return 6;
  put(result,"codeMarkerValue",send(cls("NSString"),"stringWithContentsOfFile:encoding:error:",codeMarker,(UInt)4,&codeError));
 }
 Obj error=nullptr;if(!send<bool>(str("Only this fixture wrote this profile marker."),"writeToFile:atomically:encoding:error:",marker,true,(UInt)4,&error))return 3;
 if(!send<bool>(result,"writeToFile:atomically:",join(str(userData),"fixture-observation.plist"),true))return 4;
 send<void>(app,"run");return 0;
}
