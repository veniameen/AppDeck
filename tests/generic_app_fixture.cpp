// A native application used only by generic_app_test; it never makes network requests.
#include "../src/mac.hpp"
using namespace mac;
int main(int argc,char** argv){
 const char* output=getenv("APPDECK_FIXTURE_OUTPUT");
 if(!output||strncmp(output,"/private/tmp/appdeck-generic-test-",34)!=0)return 2;
 umask(0077);
 if(!dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8))return 10;
 Pool pool;Obj app=send(cls("NSApplication"),"sharedApplication");Int policy=1;
 for(int i=1;i<argc;++i){if(strcmp(argv[i],"--fixture-service")==0)policy=2;else if(strcmp(argv[i],"--fixture-regular")==0)policy=0;}
 send<bool>(app,"setActivationPolicy:",policy);
 Obj result=dict(),arguments=array(),environment=dict();for(int i=0;i<argc;++i)add(arguments,str(argv[i]));
 put(result,"arguments",arguments);put(result,"environment",environment);
 Obj source=send(send(cls("NSProcessInfo"),"processInfo"),"environment");
 for(const char* key:{"HOME","HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy","NO_PROXY","no_proxy","NODE_USE_ENV_PROXY","CODEX_HOME","CODEX_SQLITE_HOME","CODEX_ELECTRON_USER_DATA_PATH","CLAUDE_USER_DATA_DIR"})put(environment,key,get(source,key));
 put(result,"pid",num(send<int>(send(cls("NSProcessInfo"),"processInfo"),"processIdentifier")));
 put(result,"activationPolicy",num(send<Int>(app,"activationPolicy")));
 put(result,"launchDateAvailable",boolean(send(send(cls("NSRunningApplication"),"currentApplication"),"launchDate")!=nullptr));
 if(!send<bool>(result,"writeToFile:atomically:",str(output),true))return 3;
 send<void>(app,"run");return 0;
}
