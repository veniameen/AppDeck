// Native registration, LaunchServices and process-adoption coverage. All data and apps are scratch.
#define main appdeckApplicationMain
#include "../src/main.cpp"
#undef main
extern "C" int dprintf(int,const char*,...);
extern "C" int setenv(const char*,const char*,int);
extern "C" char* mkdtemp(char*);

namespace {
Obj fixtureRoot=nullptr,fixtureProcess=nullptr,fixtureTasks=nullptr;
unsigned assertions=0;
void checkGeneric(bool value,const char* detail){
 ++assertions;if(value)return;
 if(fixtureProcess&&!send<bool>(fixtureProcess,"isTerminated"))send<bool>(fixtureProcess,"terminate");
 for(UInt i=0;i<count(fixtureTasks);++i)if(send<bool>(at(fixtureTasks,i),"isRunning"))send<void>(at(fixtureTasks,i),"terminate");
 dprintf(2,"FAIL: %s\nFixture retained: %s\n",detail,utf8(fixtureRoot));exit(1);
}
void pumpGeneric(){send<void>(send(cls("NSRunLoop"),"currentRunLoop"),"runUntilDate:",send(cls("NSDate"),"dateWithTimeIntervalSinceNow:",.025));}
void waitGenericRegistration(Obj r){
 // The fixture writes its observation before NSApplication.run, and a direct pid lookup can
 // succeed before NSWorkspace receives its launch notification. Await those raw AppKit facts,
 // never the production candidate filter whose result the tests below are meant to verify.
 int pid=send<int>(r,"processIdentifier");bool listed=false,ready=false;double deadline=uptimeNow()+10;
 do{Obj all=send(workspace,"runningApplications");listed=false;
  for(UInt i=0;i<count(all);++i)if(send<int>(at(all,i),"processIdentifier")==pid){listed=true;break;}
  ready=listed&&send<bool>(r,"isFinishedLaunching");if(ready)break;pumpGeneric();
 }while(uptimeNow()<deadline);
 checkGeneric(ready,"fixture finishes AppKit launch and appears in the workspace registry before discovery checks");
}
bool launchGeneric(Obj p){
 // A launch regression must fail the check rather than leave a modal alert open.
 previewMode=true;bool ok=launch(p,false);previewMode=false;if(ok){waitGenericRegistration(running(p));save();}return ok;
}
Obj observationGeneric(Obj path){
 double deadline=uptimeNow()+10;while(!exists(path)&&uptimeNow()<deadline)pumpGeneric();
 Obj result=mutablePlist(path);checkGeneric(isClass(result,"NSDictionary"),"native fixture wrote its observation in scratch data");return result;
}
void stopGeneric(Obj p){
 Obj r=running(p);checkGeneric(r!=nullptr,"fixture process still belongs to its launch entry");
 checkGeneric(send<bool>(r,"terminate"),"fixture accepts a normal macOS quit request");
 double deadline=uptimeNow()+10;while(!send<bool>(r,"isTerminated")&&uptimeNow()<deadline)pumpGeneric();
 checkGeneric(send<bool>(r,"isTerminated"),"fixture exits after a normal quit request");fixtureProcess=nullptr;
}
Obj makeGenericBundle(Obj binary,const char* leaf){
 Obj path=join(fixtureRoot,leaf),macos=join(path,"Contents/MacOS");checkGeneric(mkdirPrivate(macos),"create native fixture bundle");
 Obj info=dict();put(info,"CFBundleExecutable",str("Fixture"));put(info,"CFBundleIdentifier",cat(str("local.appdeck.generic-fixture-"),uuid()));
 put(info,"CFBundleName",str("Generic fixture"));put(info,"CFBundlePackageType",str("APPL"));put(info,"LSUIElement",boolean(true));
 checkGeneric(send<bool>(info,"writeToFile:atomically:",join(path,"Contents/Info.plist"),true),"write fixture Info.plist");
 Obj error=nullptr;checkGeneric(send<bool>(fileManager,"copyItemAtPath:toPath:error:",binary,join(macos,"Fixture"),&error),"copy only the native fixture executable");
 Obj signing=make("NSTask"),args=array();for(const char* value:{"--force","--sign","-"})add(args,str(value));add(args,path);
 send<void>(signing,"setExecutableURL:",url(str("/usr/bin/codesign")));send<void>(signing,"setArguments:",args);
 send<void>(signing,"setStandardOutput:",send(cls("NSFileHandle"),"fileHandleWithNullDevice"));send<void>(signing,"setStandardError:",send(cls("NSFileHandle"),"fileHandleWithNullDevice"));
 checkGeneric(send<bool>(signing,"launchAndReturnError:",&error),"start system codesign for the scratch fixture");send<void>(signing,"waitUntilExit");
 checkGeneric(send<int>(signing,"terminationStatus")==0,"ad-hoc sign the native fixture bundle");drop(signing);return path;
}
void checkGenericArguments(Obj observation){
 Obj arguments=get(observation,"arguments");checkGeneric(count(arguments)>=1,"fixture receives an executable argument");
 for(UInt i=0;i<count(arguments);++i){const char* arg=utf8(at(arguments,i));
  checkGeneric(!deck::prefix(arg,"--user-data-dir")&&!deck::prefix(arg,"--proxy-server")&&!deck::prefix(arg,"--proxy-bypass-list")&&!deck::equal(arg,"--disable-quic")&&!deck::prefix(arg,"--force-webrtc-ip-handling-policy"),"native launch receives no isolation or Chromium switches");
 }
 Obj environment=get(observation,"environment");for(const char* key:{"CODEX_HOME","CODEX_SQLITE_HOME","CODEX_ELECTRON_USER_DATA_PATH","CLAUDE_USER_DATA_DIR"})checkGeneric(!get(environment,key)||!send<UInt>(get(environment,key),"length"),"native launch does not inherit managed profile paths");
 checkGeneric(same(get(environment,"HOME"),home),"native fixture uses only the isolated HOME");
}
void checkGenericNoFolders(Obj a,Obj p){
 checkGeneric(!exists(profileRoot(p)),"standalone launch creates no profile directory");
 checkGeneric(!exists(sharedRoot(a)),"standalone registration creates no shared settings directory");
}
Obj startGenericTask(Obj bundle,Obj output,const char* role){
 Obj task=make("NSTask"),env=cleanEnvironment(),args=array();add(args,str(role));put(env,"APPDECK_FIXTURE_OUTPUT",output);
 send<void>(task,"setExecutableURL:",url(join(bundle,"Contents/MacOS/Fixture")));send<void>(task,"setArguments:",args);send<void>(task,"setEnvironment:",env);
 send<void>(task,"setStandardOutput:",send(cls("NSFileHandle"),"fileHandleWithNullDevice"));send<void>(task,"setStandardError:",send(cls("NSFileHandle"),"fileHandleWithNullDevice"));
 Obj error=nullptr;checkGeneric(send<bool>(task,"launchAndReturnError:",&error),"start a separate same-bundle native fixture process");add(fixtureTasks,task);drop(task);
 Obj record=observationGeneric(output);Int pid=integer(get(record,"pid"));Obj r=nullptr;double deadline=uptimeNow()+10;
 Int policy=strcmp(role,"--fixture-service")==0?2:strcmp(role,"--fixture-regular")==0?0:1;
 while(uptimeNow()<deadline){r=send(cls("NSRunningApplication"),"runningApplicationWithProcessIdentifier:",(int)pid);if(r&&send(r,"bundleURL")&&send<Int>(r,"activationPolicy")==policy)break;pumpGeneric();}
 checkGeneric(r&&send(r,"bundleURL")&&send<Int>(r,"activationPolicy")==policy,"direct native fixture is registered with its expected activation policy");waitGenericRegistration(r);return task;
}
Obj runningGenericTask(Obj task){return send(cls("NSRunningApplication"),"runningApplicationWithProcessIdentifier:",send<int>(task,"processIdentifier"));}
void stopGenericTask(Obj task){
 if(send<bool>(task,"isRunning"))send<void>(task,"terminate");
 double deadline=uptimeNow()+10;while(send<bool>(task,"isRunning")&&uptimeNow()<deadline)pumpGeneric();
 checkGeneric(!send<bool>(task,"isRunning"),"same-bundle fixture worker exits during cleanup");
}
}

int main(int argc,char** argv){
 if(argc!=2){dprintf(2,"Usage: generic_app_test /path/to/generic_app_fixture\n");return 2;}
 char rootBytes[]="/private/tmp/appdeck-generic-test-XXXXXX";if(!mkdtemp(rootBytes))return 3;
 char homeBytes[512],dataBytes[512],outputBytes[512];snprintf(homeBytes,sizeof homeBytes,"%s/home",rootBytes);snprintf(dataBytes,sizeof dataBytes,"%s/manager",rootBytes);snprintf(outputBytes,sizeof outputBytes,"%s/observation.plist",rootBytes);
 if(setenv("HOME",homeBytes,1)||setenv("APPDECK_DATA_ROOT",dataBytes,1)||setenv("APPDECK_FIXTURE_OUTPUT",outputBytes,1))return 3;
 for(const char* key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"})if(setenv(key,"http://127.0.0.1:9",1))return 3;
 for(const char* key:{"CODEX_HOME","CODEX_SQLITE_HOME","CODEX_ELECTRON_USER_DATA_PATH","CLAUDE_USER_DATA_DIR"})if(setenv(key,homeBytes,1))return 3;
 umask(0077);appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);if(!appKit)return 10;
 Pool pool;app=send(cls("NSApplication"),"sharedApplication");send<bool>(app,"setActivationPolicy:",(Int)1);
 fileManager=send(cls("NSFileManager"),"defaultManager");workspace=send(cls("NSWorkspace"),"sharedWorkspace");
 fixtureRoot=keep(str(rootBytes));home=keep(str(homeBytes));dataRoot=keep(str(dataBytes));Obj observationPath=str(outputBytes);
 checkGeneric(mkdirPrivate(home)&&mkdirPrivate(dataRoot)&&mkdirPrivate(join(dataRoot,"Profiles"))&&mkdirPrivate(join(dataRoot,"Shared")),"create isolated manager folders");
 state=keep(dict());apps=array();profiles=array();projects=array();events=keep(array());fixtureTasks=keep(array());put(state,"apps",apps);put(state,"profiles",profiles);put(state,"projects",projects);put(state,"profileOrder",num(1));proxyLoad();
 Obj bundle=makeGenericBundle(canonical(str(argv[1])),"Unknown native.app");
 put(state,"defaultProxy",str("00000000-0000-4000-8000-000000000099"));
 Obj a=registerApplication(bundle,deck::Adapter::Generic,true);checkGeneric(a&&isStandalone(a),"an unknown native application can be registered");
 checkGeneric(count(apps)==1&&count(profiles)==1,"registration adds exactly one launch entry");Obj p=at(profiles,0);selected=keep(get(a,"id"));
 checkGeneric(isMaster(p)&&!truth(get(p,"share"))&&same(get(p,"proxy"),str("none")),"standalone entry uses original data and starts without the default proxy");
 checkGeneric(registerApplication(bundle,deck::Adapter::Generic,false)==a&&count(apps)==1,"registering the same bundle again is idempotent");
 checkGeneric(newProfile(a,str("No additional account"),3,true)==p&&count(profiles)==1,"a standalone application cannot gain an isolated profile");
 Obj sender=make("NSMenuItem");send<void>(sender,"setTag:",(Int)0);duplicateAction(nullptr,nullptr,sender);newProfileAction(nullptr,nullptr,nullptr);drop(sender);
 checkGeneric(count(profiles)==1,"profile menu actions cannot duplicate a standalone application");checkGenericNoFolders(a,p);
 checkGeneric(!registerApplication(join(fixtureRoot,"missing.app"),deck::Adapter::Generic,false),"registration rejects a missing bundle");
 Obj serviceOutput=join(fixtureRoot,"service.plist"),serviceTask=startGenericTask(bundle,serviceOutput,"--fixture-service");
 Obj serviceRecord=observationGeneric(serviceOutput);checkGeneric(integer(get(serviceRecord,"activationPolicy"))==2,"same-bundle worker has Prohibited activation policy");
 checkGeneric(!originalProcess(p),"a background service is never adopted as the application's window");
 checkGeneric(!adoptRunning(),"status discovery ignores a same-bundle background service");Obj serviceRunning=runningGenericTask(serviceTask);
 double serviceStarted=processStarted(serviceRunning);checkGeneric(serviceStarted>0,"a direct native worker has a verifiable public process birth time");
 put(p,"pid",get(serviceRecord,"pid"));put(p,"started",real(serviceStarted));
 checkGeneric(!running(p)&&!adopt(p,serviceRunning),"a persisted service PID is never treated as the primary application's window");
 checkGeneric(launchGeneric(p),"LaunchServices opens a standalone native application");fixtureProcess=keep(running(p));
 Obj observed=observationGeneric(observationPath);checkGenericArguments(observed);
 checkGeneric(integer(get(p,"pid"))!=integer(get(serviceRecord,"pid"))&&send<Int>(running(p),"activationPolicy")==1,"launch opens the accessory GUI beside the existing prohibited worker");
 checkGeneric(count(originalCandidates(a,p))==1&&send<int>(originalProcess(p),"processIdentifier")==send<int>(running(p),"processIdentifier"),"candidate discovery selects the GUI and ignores the same-bundle worker");
 for(const char* key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy","NO_PROXY","no_proxy","NODE_USE_ENV_PROXY"})checkGeneric(!get(get(observed,"environment"),key)||!send<UInt>(get(get(observed,"environment"),key),"length"),"ordinary launch removes inherited proxy settings");
 Int pid=integer(get(p,"pid"));checkGeneric(pid>0&&integer(get(observed,"pid"))==pid,"manager tracks the actual native process identity");
 checkGeneric(launchGeneric(p)&&integer(get(p,"pid"))==pid,"repeated launch reuses the same running instance");
 put(p,"proxyUsed",str("stale fixture proxy"));put(p,"proxyPort",num(54321));erase(p,"pid");erase(p,"started");
 checkGeneric(adoptRunning()&&integer(get(p,"pid"))==pid,"running native application is recovered without an isolation marker");
 checkGeneric(!get(p,"proxyUsed")&&!get(p,"proxyPort"),"adopting an unknown process identity clears stale proxy metadata");
 save();load();cardRefs=nullptr;a=currentApp();p=at(profiles,0);
 checkGeneric(isStandalone(a)&&count(profiles)==1&&running(p),"standalone registration and running identity survive state reload");
 checkGeneric(launchGeneric(p)&&integer(get(p,"pid"))==pid,"a reloaded manager reuses the existing native process");checkGenericNoFolders(a,p);stopGeneric(p);
 checkGeneric(!proxyLocalAddress(p),"a stopped application exposes no local proxy address");
 Obj error=nullptr;checkGeneric(send<bool>(fileManager,"removeItemAtPath:error:",observationPath,&error),"clear the first scratch observation");

 checkGeneric(proxyHelper()!=nullptr,"the native test bundle contains the real proxy bridge");
 Obj proxy=dict(),lines=array();add(lines,str("127.0.0.1:9"));put(proxy,"id",uuid());put(proxy,"name",str("Fixture proxy"));put(proxy,"scheme",str("http"));put(proxy,"lines",lines);
 put(proxy,"user",str("synthetic-fixture-login"));put(proxy,"pass",str("synthetic-fixture-password"));add(proxies,proxy);put(p,"proxy",get(proxy,"id"));
 checkGeneric(proxySave(),"synthetic proxy configuration is saved separately");
 checkGeneric(launchGeneric(p),"native application launches with a real local proxy bridge");fixtureProcess=keep(running(p));
 observed=observationGeneric(observationPath);checkGenericArguments(observed);Obj local=proxyLocalAddress(p),environment=get(observed,"environment");
 checkGeneric(local&&deck::prefix(utf8(local),"http://127.0.0.1:")&&integer(get(p,"proxyPort"))>0,"running application exposes its loopback proxy endpoint");
 for(const char* key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"})checkGeneric(same(get(environment,key),local),"generic proxy launch passes only its local bridge URL");
 checkGeneric(same(get(environment,"NO_PROXY"),str(deck::proxyBypassEnv))&&same(get(environment,"no_proxy"),str(deck::proxyBypassEnv)),"generic proxy launch retains the loopback bypass policy");
 checkGeneric(same(get(environment,"NODE_USE_ENV_PROXY"),str("1")),"Node-based helpers can honor the generic proxy environment");
 Obj savedState=readText(join(dataRoot,"state.plist"));checkGeneric(savedState&&!strstr(utf8(savedState),"synthetic-fixture-login")&&!strstr(utf8(savedState),"synthetic-fixture-password"),"state.plist never contains upstream credentials");
 checkGeneric(!strstr(utf8(send(observed,"description")),"synthetic-fixture-login")&&!strstr(utf8(send(observed,"description")),"synthetic-fixture-password"),"native argv and proxy environment contain no upstream credentials");
 Obj bridgeTask=at(proxyTasks,count(proxyTasks)-1);save();load();cardRefs=nullptr;a=currentApp();p=at(profiles,0);
 checkGeneric(same(proxyLocalAddress(p),local)&&same(get(p,"proxyUsed"),str("Fixture proxy")),"state reload preserves the bridge address for the exact running identity");
 checkGeneric(launchGeneric(p)&&same(proxyLocalAddress(p),local),"manager relaunch reuses the running proxy application and its bridge");
 put(p,"proxy",str("none"));
 checkGeneric(same(proxyLocalAddress(p),local),"editing the next launch keeps the running bridge address intact");
 checkGeneric(adopt(p,running(p))&&same(proxyLocalAddress(p),local),"adoption of the same launch identity retains its proxy metadata");
 checkGenericNoFolders(a,p);stopGeneric(p);
 double deadline=uptimeNow()+10;while(send<bool>(bridgeTask,"isRunning")&&uptimeNow()<deadline)pumpGeneric();
 checkGeneric(!send<bool>(bridgeTask,"isRunning"),"local proxy bridge exits with its watched native process");
 checkGeneric(!proxyLocalAddress(p),"stale bridge ports are hidden after process exit");

 // An application relaunched independently has a new identity and cannot inherit
 // the previous process's proxy badge or manually configured loopback address.
 Obj externalConfig=dict(),externalEnv=cleanEnvironment();
 for(const char* key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy","NO_PROXY","no_proxy","NODE_USE_ENV_PROXY","CODEX_HOME","CODEX_SQLITE_HOME","CODEX_ELECTRON_USER_DATA_PATH","CLAUDE_USER_DATA_DIR"})put(externalEnv,key,str(""));
 send<void>(externalConfig,"setObject:forKey:",externalEnv,publicConstant(appKit,"NSWorkspaceLaunchConfigurationEnvironment"));
 fixtureProcess=keep(send(workspace,"launchApplicationAtURL:options:configuration:error:",url(bundle),(UInt)(1UL<<9),externalConfig,&error));
 checkGeneric(fixtureProcess!=nullptr,"relaunch the scratch app independently of the manager");
 waitGenericRegistration(fixtureProcess);
 checkGeneric(adoptRunning()&&integer(get(p,"pid"))==send<int>(fixtureProcess,"processIdentifier"),"status discovery adopts an externally relaunched native application");
 checkGeneric(!get(p,"proxyUsed")&&!get(p,"proxyPort")&&!proxyLocalAddress(p),"external relaunch clears the prior process's proxy name and endpoint");stopGeneric(p);

 // Native apps can spawn their UI directly rather than through LaunchServices.
 // Such processes may have no launchDate; identity still needs a public birth time.
 Obj accessoryOutput=join(fixtureRoot,"direct-accessory.plist"),accessoryTask=startGenericTask(bundle,accessoryOutput,"--fixture-accessory"),accessoryRunning=runningGenericTask(accessoryTask);
 checkGeneric(!send(accessoryRunning,"launchDate"),"a direct accessory fixture exercises missing LaunchServices launchDate");
 checkGeneric(adoptRunning()&&integer(get(p,"pid"))==send<int>(accessoryRunning,"processIdentifier")&&running(p),"a direct accessory GUI is adopted through its public process birth time");
 double accessoryStarted=send<double>(get(p,"started"),"doubleValue");checkGeneric(accessoryStarted>0,"direct GUI identity stores a nonzero birth timestamp");
 put(p,"started",real(accessoryStarted-2));checkGeneric(!running(p),"a mismatched birth timestamp rejects a potentially reused PID");put(p,"started",real(accessoryStarted));
 save();load();cardRefs=nullptr;a=currentApp();p=at(profiles,0);
 checkGeneric(running(p)&&launchGeneric(p)&&integer(get(p,"pid"))==send<int>(accessoryRunning,"processIdentifier"),"direct GUI identity survives manager reload without creating another instance");
 Obj regularOutput=join(fixtureRoot,"direct-regular.plist"),regularTask=startGenericTask(bundle,regularOutput,"--fixture-regular"),regularRunning=runningGenericTask(regularTask);
 put(a,"adapter",str("chromium"));
 checkGeneric(count(originalCandidates(a,p))==2&&!originalProcess(p),"known adapters preserve account ambiguity across regular and accessory processes");
 put(a,"adapter",str("generic"));
 checkGeneric(count(originalCandidates(a,p))==1&&send<int>(originalProcess(p),"processIdentifier")==send<int>(regularRunning,"processIdentifier"),"a regular GUI is preferred when a tray UI and background service also exist");
 erase(p,"pid");erase(p,"started");checkGeneric(adoptRunning()&&integer(get(p,"pid"))==send<int>(regularRunning,"processIdentifier")&&running(p),"fresh status discovery adopts the regular GUI instead of the tray UI");
 Obj secondRegularOutput=join(fixtureRoot,"second-regular.plist"),secondRegularTask=startGenericTask(bundle,secondRegularOutput,"--fixture-regular");
 erase(p,"pid");erase(p,"started");
 checkGeneric(count(originalCandidates(a,p))==2&&!originalProcess(p)&&!launchGeneric(p),"two regular GUI processes remain ambiguous without selecting a random PID");
 stopGenericTask(secondRegularTask);stopGenericTask(regularTask);
 checkGeneric(adoptRunning()&&integer(get(p,"pid"))==send<int>(accessoryRunning,"processIdentifier"),"the accessory GUI remains supported after regular windows exit");
 fixtureProcess=keep(running(p));stopGeneric(p);stopGenericTask(accessoryTask);
 checkGeneric(!originalProcess(p)&&!adoptRunning(),"a remaining service alone never changes the application to running");stopGenericTask(serviceTask);

 Obj knownBundle=makeGenericBundle(canonical(str(argv[1])),"Known adapter.app"),known=registerApplication(knownBundle,deck::Adapter::Chromium,true);
 checkGeneric(known&&!isStandalone(known)&&count(profiles)==2&&isMaster(at(profiles,1)),"known adapters retain their primary-profile behavior");
 Obj extra=newProfile(known,str("Separate account"),1,false);checkGeneric(extra&&!isMaster(extra)&&count(profiles)==3,"known adapters can still add isolated profiles");
 checkGeneric(send<bool>(fileManager,"removeItemAtPath:error:",fixtureRoot,&error),"remove the isolated native fixture");
 dprintf(1,"PASS: %u native generic-app assertions (registration, launch, adoption, proxy environment and lifecycle).\n",assertions);return 0;
}
