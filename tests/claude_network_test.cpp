// Native fixture apps and the real loopback bridge exercise production launch wiring offline.
#define main appdeckApplicationMain
#include "../src/main.cpp"
#undef main
extern "C" int dprintf(int,const char*,...);
extern "C" int setenv(const char*,const char*,int);
extern "C" int unsetenv(const char*);
namespace {
unsigned checks=0;Obj scratch=nullptr,children=nullptr,bridges=nullptr;
constexpr const char* fixtureUser="network-fixture-login-4ab71";
constexpr const char* fixturePass="network-fixture-secret-79d42";
void requireNetwork(bool ok,const char* detail){
 ++checks;if(ok)return;
 for(UInt i=0;i<count(children);++i)if(!send<bool>(at(children,i),"isTerminated"))send<bool>(at(children,i),"terminate");
 for(UInt i=0;i<count(bridges);++i)if(send<bool>(at(bridges,i),"isRunning"))send<void>(at(bridges,i),"terminate");
 dprintf(2,"FAIL: %s\nFixture retained: %s\n",detail,utf8(scratch));exit(1);
}
void pumpNetwork(){send<void>(send(cls("NSRunLoop"),"currentRunLoop"),"runUntilDate:",send(cls("NSDate"),"dateWithTimeIntervalSinceNow:",.025));}
bool hasArgument(Obj observation,Obj expected){Obj args=get(observation,"arguments");for(UInt i=0;i<count(args);++i)if(same(at(args,i),expected))return true;return false;}
bool containsFixtureCredential(Obj value){
 if(isClass(value,"NSString"))return strstr(utf8(value),fixtureUser)||strstr(utf8(value),fixturePass);
 if(isClass(value,"NSDictionary"))return containsFixtureCredential(send(value,"allKeys"))||containsFixtureCredential(send(value,"allValues"));
 if(isClass(value,"NSArray"))for(UInt i=0;i<count(value);++i)if(containsFixtureCredential(at(value,i)))return true;
 return false;
}
Obj networkBundle(Obj binary,const char* leaf,const char* bundlePrefix){
 Obj path=join(scratch,leaf),macos=join(path,"Contents/MacOS");requireNetwork(mkdirPrivate(macos),"create disposable native app bundle");
 Obj info=dict();put(info,"CFBundleExecutable",str("Fixture"));put(info,"CFBundleIdentifier",cat(str(bundlePrefix),uuid()));
 put(info,"CFBundleName",str(leaf));put(info,"CFBundlePackageType",str("APPL"));put(info,"LSUIElement",boolean(true));Obj error=nullptr;
 requireNetwork(send<bool>(info,"writeToFile:atomically:",join(path,"Contents/Info.plist"),true),"write fixture-only app metadata");
 requireNetwork(send<bool>(fileManager,"copyItemAtPath:toPath:error:",binary,join(macos,"Fixture"),&error),"copy only the native stand-in executable");
 Obj signing=make("NSTask"),args=array();for(const char* value:{"--force","--sign","-"})add(args,str(value));add(args,path);
 send<void>(signing,"setExecutableURL:",url(str("/usr/bin/codesign")));send<void>(signing,"setArguments:",args);
 send<void>(signing,"setStandardOutput:",send(cls("NSFileHandle"),"fileHandleWithNullDevice"));send<void>(signing,"setStandardError:",send(cls("NSFileHandle"),"fileHandleWithNullDevice"));
 requireNetwork(send<bool>(signing,"launchAndReturnError:",&error),"start system signing for fixture");send<void>(signing,"waitUntilExit");
 requireNetwork(send<int>(signing,"terminationStatus")==0,"ad-hoc sign fixture bundle");drop(signing);return path;
}
Obj networkProfile(Obj a,const char* name){
 Obj p=newProfile(a,str(name),0,false);for(const char* key:{"claudeClean","claudeCodeIsolated","claudeReduceTraffic","proxyRequired","proxyHardened"})put(p,key,boolean(true));return p;
}
Obj networkObservationPath(Obj p){return join(profileRoot(p),"user-data/fixture-observation.plist");}
Obj launchNetworkFixture(Obj p){
 UInt before=count(proxyTasks);previewMode=true;bool ok=launch(p,false);previewMode=false;
 // Retain every actual helper before asserting, so a failed launch can clean up its own processes.
 for(UInt i=before;i<count(proxyTasks);++i)add(bridges,at(proxyTasks,i));Obj child=running(p);if(child)add(children,child);
 requireNetwork(ok&&child,"production launcher starts the exact fixture profile");
 requireNetwork(count(proxyTasks)==before+1,"each launched fixture receives one real proxy bridge");Obj task=at(proxyTasks,before);
 requireNetwork(send<bool>(task,"isRunning")&&send<int>(task,"processIdentifier")>0,"the real loopback bridge remains alive while its fixture runs");
 requireNetwork(!containsFixtureCredential(send(task,"arguments"))&&!containsFixtureCredential(send(task,"environment")),"bridge credentials are absent from helper argv and environment");
 double deadline=uptimeNow()+12;while(!exists(networkObservationPath(p))&&uptimeNow()<deadline)pumpNetwork();Obj observation=mutablePlist(networkObservationPath(p));
 requireNetwork(isClass(observation,"NSDictionary"),"fixture records its effective launch configuration");
 requireNetwork(integer(get(observation,"pid"))==integer(get(p,"pid")),"observation belongs to the exact managed child");return observation;
}
void checkNetworkLaunch(Obj p,Obj observation,bool browser){
 Obj env=get(observation,"environment"),address=proxyLocalAddress(p);
 requireNetwork(address&&deck::prefix(utf8(address),"http://127.0.0.1:")&&!strchr(utf8(address),'@'),"managed routing exposes only a credential-free loopback URL");
 requireNetwork(hasArgument(observation,cat(str("--user-data-dir="),join(profileRoot(p),"user-data"))),"each process receives its own durable user-data directory");
 requireNetwork(hasArgument(observation,cat(str("--proxy-server="),address)),"Chromium traffic uses the live bridge assigned to this child");
 requireNetwork(hasArgument(observation,cat(str("--proxy-bypass-list="),str(deck::proxyBypassChromium))),"only the established loopback bypass is supplied");
 requireNetwork(hasArgument(observation,str("--disable-quic"))&&hasArgument(observation,str("--force-webrtc-ip-handling-policy=disable_non_proxied_udp")),"QUIC and non-proxied WebRTC UDP bypass remain disabled");
 // Claude Desktop rejects Chromium host-resolver overrides at its vendor startup gate.
 // DNS hardening therefore belongs only to the linked Chromium browser.
 requireNetwork(browser?hasArgument(observation,str(deck::proxyResolverArgument)):!hasArgument(observation,str(deck::proxyResolverArgument)),"DNS hardening reaches linked Chrome and never triggers Claude Desktop's vendor guard");
 for(const char* key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"})requireNetwork(same(get(env,key),address),"upper- and lowercase proxy environment routes to this child's bridge");
 requireNetwork(same(get(env,"NO_PROXY"),str(deck::proxyBypassEnv))&&same(get(env,"no_proxy"),str(deck::proxyBypassEnv))&&same(get(env,"NODE_USE_ENV_PROXY"),str("1")),"Node proxy support and loopback exceptions are consistent");
 requireNetwork(!containsFixtureCredential(observation),"proxy credentials never reach fixture arguments or observed environment");
 requireNetwork(same(get(env,"HOME"),home),"native HOME remains the isolated fixture source home");
 if(browser){
  requireNetwork(hasArgument(observation,str("https://claude.ai/")),"linked browser opens the Claude web entry point");
  requireNetwork(hasArgument(observation,str("--no-first-run"))&&hasArgument(observation,str("--no-default-browser-check")),"linked browser suppresses first-run and default-browser prompts");
  requireNetwork(!get(env,"CLAUDE_CONFIG_DIR"),"linked Chromium does not inherit the Claude profile's private Code root");
 }else{
  for(const char* key:{"CLAUDE_CODE_HTTP_PROXY","CLAUDE_CODE_HTTPS_PROXY","grpc_proxy"})requireNetwork(same(get(env,key),address),"Claude helper proxy aliases use the same credential-free bridge");
  requireNetwork(same(get(env,"CLAUDE_CODE_PROXY_RESOLVES_HOSTS"),str("1")),"Claude Code delegates target hostname resolution to its proxy");
  requireNetwork(same(get(env,"CLAUDE_CONFIG_DIR"),claudeCodeRoot(p)),"network configuration belongs to the selected private Code root");
  Obj settings=readJSON(join(claudeCodeRoot(p),"settings.json")),settingsEnv=get(settings,"env");
  requireNetwork(isClass(settings,"NSDictionary")&&isClass(settingsEnv,"NSDictionary"),"launch writes valid private Code settings for host-managed sessions");
  for(const char* key:claudeSettingsKeys)requireNetwork(same(get(settingsEnv,key),get(env,key)),"private Code settings mirror each supported launch proxy and traffic switch");
  for(const char* key:{"CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC","DISABLE_TELEMETRY","DISABLE_ERROR_REPORTING","DISABLE_GROWTHBOOK","DO_NOT_TRACK"})requireNetwork(same(get(env,key),str("1")),"selected optional-traffic restrictions reach the real child environment");
  requireNetwork(!containsFixtureCredential(settings),"private Code settings contain no upstream proxy credentials");
 }
}
void stopNetworkFixture(Obj p,Obj bridge){
 Obj child=running(p);requireNetwork(child!=nullptr,"the selected child still owns its profile before quitting");double deadline=uptimeNow()+10;
 while(!send<bool>(child,"isFinishedLaunching")&&uptimeNow()<deadline)pumpNetwork();
 requireNetwork(send<bool>(child,"isFinishedLaunching")&&send<bool>(child,"terminate"),"send ordinary quit only after AppKit launch registration finishes");deadline=uptimeNow()+10;
 while(!send<bool>(child,"isTerminated")&&uptimeNow()<deadline)pumpNetwork();requireNetwork(send<bool>(child,"isTerminated"),"native fixture exits normally");deadline=uptimeNow()+10;
 while(send<bool>(bridge,"isRunning")&&uptimeNow()<deadline)pumpNetwork();requireNetwork(!send<bool>(bridge,"isRunning"),"the real proxy bridge exits after its own watched child");
}
void checkBlockedLaunch(Obj p,bool beforePrepare){
 UInt taskCount=count(proxyTasks),claudeCount=count(claudeLaunchTasks);previewMode=true;bool ok=launch(p,false);previewMode=false;
 requireNetwork(!ok&&!running(p)&&!ownedProcess(p)&&!get(p,"pid"),"unusable required routing never falls back to a direct child");
 requireNetwork(count(proxyTasks)==taskCount&&count(claudeLaunchTasks)==claudeCount&&!exists(networkObservationPath(p)),"a rejected launch creates neither a bridge nor an observed fixture process");
 if(beforePrepare)requireNetwork(!exists(profileRoot(p)),"a missing required proxy blocks before creating any profile data");
}
void removeNetworkArtifact(Obj path){Obj error=nullptr;requireNetwork(send<bool>(fileManager,"removeItemAtPath:error:",path,&error),"remove only an isolated fixture artifact");}
}
int main(int argc,char** argv){
 if(argc!=2){dprintf(2,"Usage: claude_network_test /path/to/claude_profile_fixture\n");return 2;}
 char rootBytes[]="/private/tmp/appdeck-claude-profile-test-network-XXXXXX";if(!mkdtemp(rootBytes))return 3;
 char homeBytes[512],dataBytes[512];snprintf(homeBytes,sizeof homeBytes,"%s/home",rootBytes);snprintf(dataBytes,sizeof dataBytes,"%s/manager",rootBytes);
 if(setenv("HOME",homeBytes,1)||setenv("APPDECK_DATA_ROOT",dataBytes,1)||setenv("CFFIXED_USER_HOME",homeBytes,1))return 3;
 // LaunchServices merges its environment. Sanitize the test process before Foundation reads it,
 // so the browser stand-in can never record an inherited terminal identity or proxy credential.
 for(const char* key:{"CLAUDE_USER_DATA_DIR","CLAUDE_CONFIG_DIR","CLAUDE_SECURESTORAGE_CONFIG_DIR","CLAUDE_CODE_TMPDIR","CLAUDECODE","ANTHROPIC_API_KEY","ANTHROPIC_AUTH_TOKEN","ANTHROPIC_BASE_URL","CLAUDE_CODE_OAUTH_TOKEN","HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy","NO_PROXY","no_proxy","grpc_proxy","GRPC_PROXY","no_grpc_proxy","NO_GRPC_PROXY","NODE_USE_ENV_PROXY","CLAUDE_CODE_HTTP_PROXY","CLAUDE_CODE_HTTPS_PROXY","CLAUDE_CODE_PROXY_RESOLVES_HOSTS","CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC","DISABLE_TELEMETRY","DISABLE_ERROR_REPORTING","DISABLE_GROWTHBOOK","DO_NOT_TRACK"})if(unsetenv(key))return 3;
 umask(0077);appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);if(!appKit)return 10;Pool pool;
 app=send(cls("NSApplication"),"sharedApplication");send<bool>(app,"setActivationPolicy:",(Int)1);fileManager=send(cls("NSFileManager"),"defaultManager");workspace=send(cls("NSWorkspace"),"sharedWorkspace");
 scratch=keep(str(rootBytes));children=keep(array());bridges=keep(array());home=keep(str(homeBytes));dataRoot=keep(str(dataBytes));
 requireNetwork(mkdirPrivate(join(home,".claude"))&&mkdirPrivate(join(dataRoot,"Profiles"))&&mkdirPrivate(join(dataRoot,"Shared")),"all source and managed data live under the disposable root");
 requireNetwork(writeText(join(home,".claude/source-sentinel.txt"),str("Original synthetic Code settings.")),"seed only a fake source Code settings marker");
 state=keep(dict());apps=array();profiles=array();projects=array();events=keep(array());put(state,"apps",apps);put(state,"profiles",profiles);put(state,"projects",projects);put(state,"profileOrder",num(1));proxyLoad();
 Obj executable=canonical(str(argv[1])),claudePath=networkBundle(executable,"FixtureClaude.app","local.appdeck.claude-network-fixture."),chromePath=networkBundle(executable,"FixtureChrome.app","com.google.Chrome.appdeck-fixture.");
 Obj a=registerApplication(claudePath,deck::Adapter::Claude,false),chrome=registerApplication(chromePath,deck::Adapter::Chromium,false);
 requireNetwork(a&&chrome&&count(apps)==2,"register only the two disposable fixture applications");selected=keep(get(a,"id"));
 requireNetwork(same(canonical(claudeChromePath()),canonical(chromePath)),"linked-browser discovery selects registered fixture Chrome without a real-browser fallback");
 Obj helper=proxyHelper();requireNetwork(helper&&same(canonical(helper),canonical(join(send(send(cls("NSBundle"),"mainBundle"),"bundlePath"),"Contents/MacOS/appdeck-proxy"))),"production helper discovery resolves the runner's own compiled bridge");
 Obj proxy=dict(),lines=array();put(proxy,"id",uuid());put(proxy,"name",str("Offline network fixture"));put(proxy,"scheme",str("http"));add(lines,str("127.0.0.1:9"));put(proxy,"lines",lines);put(proxy,"user",str(fixtureUser));put(proxy,"pass",str(fixturePass));add(proxies,proxy);
 requireNetwork(proxyValid(proxy)&&proxySave(),"save the synthetic proxy through the production private store");struct stat st{};
 requireNetwork(lstat(utf8(proxyPath()),&st)==0&&(st.st_mode&0777)==0600,"synthetic upstream credentials are stored only in private proxies.plist");
 Obj p=networkProfile(a,"Network profile");put(p,"proxy",get(proxy,"id"));requireNetwork(claudeEnsureWeb(p),"production setup creates a linked browser profile");Obj web=claudeWebProfile(p),webId=keep(get(web,"id"));
 requireNetwork(web&&!isMaster(web)&&truth(get(web,"claudeWeb"))&&same(appFor(web),chrome),"linked profile uses the registered Chromium adapter");
 requireNetwork(!same(profileRoot(web),profileRoot(p))&&!get(web,"claudeClean")&&!get(web,"claudeCodeIsolated"),"linked browser owns separate storage without acquiring Claude-specific modes");
 for(const char* key:{"proxy","proxyRequired","proxyHardened"})requireNetwork(same(get(web,key),get(p,key)),"linked browser initially inherits the selected network settings");
 Obj sentinel=join(profileRoot(web),"user-data/browser-data-sentinel.txt");requireNetwork(mkdirPrivate(join(profileRoot(web),"user-data"))&&writeText(sentinel,str("Existing synthetic browser data.")),"seed an existing linked-browser data marker");UInt profileCount=count(profiles);
 put(p,"proxyRequired",boolean(false));put(p,"proxyHardened",boolean(false));put(p,"proxy",str("none"));requireNetwork(claudeEnsureWeb(p),"updating network choices reuses the linked browser");
 requireNetwork(count(profiles)==profileCount&&same(get(claudeWebProfile(p),"id"),webId)&&!truth(get(web,"proxyRequired"))&&!truth(get(web,"proxyHardened"))&&same(get(web,"proxy"),str("none")),"linked browser retains its identity while all network settings synchronize");
 put(p,"proxyRequired",boolean(true));put(p,"proxyHardened",boolean(true));put(p,"proxy",get(proxy,"id"));requireNetwork(claudeEnsureWeb(p)&&same(readText(sentinel),str("Existing synthetic browser data.")),"restoring network policy preserves existing linked-browser data");
 Obj observed=launchNetworkFixture(p),webObserved=launchNetworkFixture(web);checkNetworkLaunch(p,observed,false);checkNetworkLaunch(web,webObserved,true);
 requireNetwork(running(p)&&running(web)&&integer(get(p,"pid"))!=integer(get(web,"pid"))&&integer(get(p,"proxyPort"))!=integer(get(web,"proxyPort")),"Claude and linked browser coexist with distinct process identities and bridges");
 requireNetwork(same(readText(sentinel),str("Existing synthetic browser data.")),"launching the linked browser preserves its existing local data");
 requireNetwork(save()&&!containsFixtureCredential(mutablePlist(join(dataRoot,"state.plist"))),"persisted profile state contains no upstream login or password");
 load();cardRefs=nullptr;p=at(profiles,0);web=claudeWebProfile(p);
 requireNetwork(claudeCodeIsolated(p)&&truth(get(p,"claudeReduceTraffic"))&&same(get(web,"id"),webId)&&running(p)&&running(web),"reload preserves clean Code mode, traffic choice, linked browser ID and both running processes");
 for(Obj item:{p,web})requireNetwork(truth(get(item,"proxyRequired"))&&truth(get(item,"proxyHardened"))&&same(get(item,"proxy"),get(proxy,"id")),"reload preserves explicit required routing and DNS policy for each profile");
 stopNetworkFixture(p,at(bridges,0));requireNetwork(running(web)&&send<bool>(at(bridges,1),"isRunning"),"closing Claude leaves the linked browser and its own bridge running");stopNetworkFixture(web,at(bridges,1));
 claudeReapLaunches();proxyReap();requireNetwork(!count(claudeLaunchTasks)&&!count(proxyTasks),"normal termination reaps every owned child and bridge");
 Obj rejected=networkProfile(currentApp(),"Rejected routing fixture");put(rejected,"proxy",str("none"));checkBlockedLaunch(rejected,true);
 put(rejected,"proxy",str("90000000-0000-4000-8000-000000000001"));checkBlockedLaunch(rejected,true);
 put(rejected,"proxy",get(proxy,"id"));Obj badLines=array();add(badLines,str("invalid endpoint without a port"));put(proxy,"lines",badLines);checkBlockedLaunch(rejected,false);
 requireNetwork(!exists(join(claudeCodeRoot(rejected),"settings.json")),"an invalid upstream cannot leave settings pointing at an unstarted bridge");
 Obj error=nullptr;requireNetwork(same(readText(join(home,".claude/source-sentinel.txt")),str("Original synthetic Code settings."))&&count(send(fileManager,"contentsOfDirectoryAtPath:error:",join(home,".claude"),&error))==1,"network setup and launches never modify the source Code folder");
 for(Obj item:{p,rejected})if(exists(claudeProfileTemp(item)))removeNetworkArtifact(claudeProfileTemp(item));removeNetworkArtifact(scratch);drop(webId);
 dprintf(1,"%u Claude network assertions passed with native fixtures and real loopback bridges; no vendor app or upstream request.\n",checks);return 0;
}
