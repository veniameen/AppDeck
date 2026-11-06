// Production clean-profile launch checks using only an isolated native fixture application.
#define main appdeckApplicationMain
#include "../src/main.cpp"
#undef main
extern "C" int dprintf(int,const char*,...);
extern "C" int setenv(const char*,const char*,int);
namespace {
unsigned assertions=0;Obj fixtureRoot=nullptr,fixtureProcesses=nullptr;
void checkProfile(bool pass,const char* detail){
 ++assertions;if(pass)return;
 for(UInt i=0;i<count(fixtureProcesses);++i){Obj r=at(fixtureProcesses,i);if(!send<bool>(r,"isTerminated"))send<bool>(r,"terminate");}
 dprintf(2,"FAIL: %s\nFixture retained: %s\n",detail,utf8(fixtureRoot));exit(1);
}
void pumpProfile(){send<void>(send(cls("NSRunLoop"),"currentRunLoop"),"runUntilDate:",send(cls("NSDate"),"dateWithTimeIntervalSinceNow:",.025));}
void removeFixture(Obj path){Obj error=nullptr;checkProfile(send<bool>(fileManager,"removeItemAtPath:error:",path,&error),"remove only an isolated test artifact");}
Obj makeProfileBundle(Obj binary){
 Obj path=join(fixtureRoot,"FixtureClaude.app"),macos=join(path,"Contents/MacOS");checkProfile(mkdirPrivate(macos),"create isolated fixture bundle");
 Obj info=dict();put(info,"CFBundleExecutable",str("Fixture"));put(info,"CFBundleIdentifier",cat(str("local.appdeck.claude-profile-fixture-"),uuid()));
 put(info,"CFBundleName",str("Claude profile fixture"));put(info,"CFBundlePackageType",str("APPL"));put(info,"LSUIElement",boolean(true));
 checkProfile(send<bool>(info,"writeToFile:atomically:",join(path,"Contents/Info.plist"),true),"write fixture bundle metadata");Obj error=nullptr;
 checkProfile(send<bool>(fileManager,"copyItemAtPath:toPath:error:",binary,join(macos,"Fixture"),&error),"copy only the native fixture executable");
 Obj signing=make("NSTask"),args=array();for(const char* value:{"--force","--sign","-"})add(args,str(value));add(args,path);
 send<void>(signing,"setExecutableURL:",url(str("/usr/bin/codesign")));send<void>(signing,"setArguments:",args);
 send<void>(signing,"setStandardOutput:",send(cls("NSFileHandle"),"fileHandleWithNullDevice"));send<void>(signing,"setStandardError:",send(cls("NSFileHandle"),"fileHandleWithNullDevice"));
 checkProfile(send<bool>(signing,"launchAndReturnError:",&error),"start system signing for fixture");send<void>(signing,"waitUntilExit");
 checkProfile(send<int>(signing,"terminationStatus")==0,"ad-hoc sign native fixture");drop(signing);return path;
}
Obj observationPath(Obj p){return join(join(profileRoot(p),"user-data"),"fixture-observation.plist");}
Obj launchProfileFixture(Obj p){
 previewMode=true;bool ok=launch(p,false);previewMode=false;
 checkProfile(ok,"production launch accepts a managed Claude profile");Obj r=running(p);checkProfile(r!=nullptr,"production launch returns an independently tracked process");add(fixtureProcesses,r);
 double deadline=uptimeNow()+12;while(!exists(observationPath(p))&&uptimeNow()<deadline)pumpProfile();
 Obj result=mutablePlist(observationPath(p));checkProfile(isClass(result,"NSDictionary"),"fixture reports its effective launch environment");
 checkProfile(integer(get(result,"pid"))==integer(get(p,"pid")),"observation belongs to the exact launched profile");return result;
}
void stopProfileFixture(Obj p){
 Obj r=running(p);checkProfile(r!=nullptr,"fixture still belongs to the selected profile before quitting");double deadline=uptimeNow()+10;
 // The fixture writes its observation immediately before entering NSApplication.run. Wait for
 // AppKit's launch handshake before sending the ordinary quit Apple event to a fresh child.
 while(!send<bool>(r,"isFinishedLaunching")&&uptimeNow()<deadline)pumpProfile();
 checkProfile(send<bool>(r,"isFinishedLaunching")&&send<bool>(r,"terminate"),"fixture receives an ordinary quit request after AppKit finishes launching");deadline=uptimeNow()+10;
 while(!send<bool>(r,"isTerminated")&&uptimeNow()<deadline)pumpProfile();checkProfile(send<bool>(r,"isTerminated"),"fixture exits normally");
}
void checkPrivateDir(Obj path){struct stat s{};checkProfile(lstat(utf8(path),&s)==0&&S_ISDIR(s.st_mode)&&(s.st_mode&0777)==0700,"profile directories are private real directories");}
void checkEnvironment(Obj p,Obj observation){
 Obj env=get(observation,"environment"),ph=claudeProfileHome(p),tmp=claudeProfileTemp(p);
 checkProfile(same(get(env,"HOME"),home),"clean Desktop preserves the native HOME required by macOS Security");
 checkProfile(same(get(observation,"workingDirectory"),ph),"clean profile starts in its private work folder, outside the manager's project directory");
 checkProfile(same(get(env,"CLAUDE_USER_DATA_DIR"),join(profileRoot(p),"user-data")),"legacy Desktop override still names only this profile");
 checkProfile(same(canonical(get(env,"TMPDIR")),canonical(tmp))&&same(canonical(get(env,"MAC_CHROMIUM_TMPDIR")),canonical(tmp)),"Node and Chromium receive the same private temporary directory");
 checkProfile(same(get(env,"XDG_CONFIG_HOME"),join(ph,".config"))&&same(get(env,"XDG_CACHE_HOME"),join(ph,".cache")),"config and cache defaults stay private");
 checkProfile(same(get(env,"XDG_DATA_HOME"),join(ph,".local/share"))&&same(get(env,"XDG_STATE_HOME"),join(ph,".local/state")),"data and state defaults stay private");
 for(const char* key:{"CFFIXED_USER_HOME","CLAUDE_SECURESTORAGE_CONFIG_DIR","CLAUDE_CODE_TMPDIR","CLAUDECODE","ANTHROPIC_API_KEY","ANTHROPIC_AUTH_TOKEN","ANTHROPIC_BASE_URL","CLAUDE_CODE_OAUTH_TOKEN"})checkProfile(!get(env,key),"inherited Claude paths and identity overrides are truly absent, including nullish-fallback configuration");
 for(const char* key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy","NO_PROXY","no_proxy","grpc_proxy","GRPC_PROXY","no_grpc_proxy","NO_GRPC_PROXY","NODE_USE_ENV_PROXY","CLAUDE_CODE_HTTP_PROXY","CLAUDE_CODE_HTTPS_PROXY","CLAUDE_CODE_PROXY_RESOLVES_HOSTS"})checkProfile(!get(env,key),"a direct profile launch never inherits proxy routing or helper proxy aliases");
 for(const char* key:{"CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC","DISABLE_TELEMETRY","DISABLE_ERROR_REPORTING","DISABLE_GROWTHBOOK","DO_NOT_TRACK"})checkProfile(truth(get(p,"claudeReduceTraffic"))?same(get(env,key),str("1")):!get(env,key),"optional traffic switches follow only the selected profile setting");
 if(claudeCodeIsolated(p)){
  checkProfile(same(get(env,"CLAUDE_CONFIG_DIR"),claudeCodeRoot(p))&&same(get(observation,"codeRoot"),claudeCodeRoot(p)),"new isolated profile uses only its private effective Code configuration root");
  checkProfile(!truth(get(observation,"codeSourceSentinelPresent")),"effective private Code root contains none of the source settings");
  checkProfile(same(get(observation,"codeMarkerValue"),join(profileRoot(p),"user-data")),"private Code state belongs to the same profile as its Desktop state");checkPrivateDir(claudeCodeRoot(p));
 }else{
  checkProfile(!get(env,"CLAUDE_CONFIG_DIR")&&same(get(observation,"codeRoot"),join(home,".claude")),"existing Desktop-only profile retains its original effective Code root without an override");
  checkProfile(truth(get(observation,"codeSourceSentinelPresent")),"existing Desktop-only profile retains its source Code settings");
 }
 bool marker=false;Obj args=get(observation,"arguments");for(UInt i=0;i<count(args);++i)if(same(at(args,i),cat(str("--user-data-dir="),join(profileRoot(p),"user-data"))))marker=true;
 checkProfile(marker,"durable user-data argument identifies this profile");checkProfile(truth(get(observation,"sourceSentinelPresent")),"native HOME is preserved independently from the selected Code configuration root");
 checkPrivateDir(ph);checkPrivateDir(tmp);checkPrivateDir(join(profileRoot(p),"user-data"));
}
Obj makeCleanProfile(Obj a,const char* name,bool isolated=true){Obj p=newProfile(a,str(name),0,false);put(p,"claudeClean",boolean(true));if(isolated){put(p,"claudeCodeIsolated",boolean(true));put(p,"claudeReduceTraffic",boolean(true));}put(p,"proxy",str("none"));return p;}
void addDescriptor(Obj p,const char* id){
 Obj dir=join(join(join(join(profileRoot(p),"user-data"),"claude-code-sessions"),"10000000-0000-4000-8000-000000000001"),"20000000-0000-4000-8000-000000000001");
 checkProfile(mkdirPrivate(dir),"create isolated synthetic session descriptor directory");Obj record=dict();put(record,"sessionId",cat(str("local_"),str(id)));put(record,"cliSessionId",str(id));
 put(record,"cwd",join(fixtureRoot,"project"));put(record,"title",str("Fixture conversation"));put(record,"isArchived",boolean(false));
 checkProfile(writeJSON(join(dir,cat(cat(str("local_"),str(id)),str(".json"))),record),"write only synthetic native session metadata");
}
Obj fixtureBytes(Obj path){return send(cls("NSData"),"dataWithContentsOfFile:",path);}
void checkPrivateSettings(Obj a,Obj legacy){
 Obj p=makeCleanProfile(a,"Settings fixture");checkProfile(!prepare(p),"prepare isolated Code settings fixture");
 Obj root=claudeCodeRoot(p),path=join(root,"settings.json"),backup=join(root,"settings.json.appdeck-previous"),source=join(home,".claude/source-sentinel.txt");
 Obj original=dict(),originalEnv=dict(),permissions=dict();put(original,"model",str("fixture-model"));put(permissions,"defaultMode",str("default"));put(original,"permissions",permissions);
 put(originalEnv,"CUSTOM_SETTING",str("keep this user setting"));put(originalEnv,"HTTP_PROXY",str("stale proxy"));put(original,"env",originalEnv);
 checkProfile(writeJSON(path,original),"seed private settings with unrelated user configuration");Obj before=fixtureBytes(path);
 Obj env=cleanEnvironment();applyClaudeEnvironment(p,env);proxyApplyEnv(env,32123);
 for(const char* key:{"CLAUDE_CODE_HTTP_PROXY","CLAUDE_CODE_HTTPS_PROXY","grpc_proxy"})put(env,key,proxyURL(32123));put(env,"CLAUDE_CODE_PROXY_RESOLVES_HOSTS",str("1"));
 checkProfile(!claudeConfigureSettings(p,env),"merge only allowlisted proxy and optional-traffic settings");Obj merged=readJSON(path),mergedEnv=get(merged,"env");
 checkProfile(same(get(merged,"model"),get(original,"model"))&&same(get(merged,"permissions"),permissions)&&same(get(mergedEnv,"CUSTOM_SETTING"),str("keep this user setting")),"merging preserves unrelated root keys and user environment settings");
 for(const char* key:claudeSettingsKeys)checkProfile(same(get(mergedEnv,key),get(env,key)),"settings carry exactly the selected credential-free launch routing and opt-outs");
 checkProfile(!get(mergedEnv,"CLAUDE_CONFIG_DIR")&&!get(mergedEnv,"ANTHROPIC_API_KEY"),"private identity and authentication overrides are not copied into settings");
 checkProfile(same(fixtureBytes(backup),before),"previous private settings are preserved byte-for-byte");struct stat mode{};
 checkProfile(lstat(utf8(path),&mode)==0&&(mode.st_mode&0777)==0600&&lstat(utf8(backup),&mode)==0&&(mode.st_mode&0777)==0600,"settings and their previous version are private 0600 files");
 before=fixtureBytes(path);put(p,"claudeReduceTraffic",boolean(false));Obj direct=cleanEnvironment();applyClaudeEnvironment(p,direct);
 checkProfile(!claudeConfigureSettings(p,direct),"switching to direct routing removes only AppDeck-owned keys");merged=readJSON(path);mergedEnv=get(merged,"env");
 for(const char* key:claudeSettingsKeys)checkProfile(!get(mergedEnv,key),"disabled routing and traffic keys do not persist in Code settings");
 checkProfile(count(mergedEnv)==1&&same(get(mergedEnv,"CUSTOM_SETTING"),str("keep this user setting"))&&same(fixtureBytes(backup),before),"clearing managed keys retains user settings and backs up the replaced version");
 int fd=claudeSettingsDirectory(root);ClaudeSettingsSnapshot unchanged,previous;checkProfile(fd>=0&&claudeSettingsRead(fd,"settings.json",unchanged)&&claudeSettingsRead(fd,"settings.json.appdeck-previous",previous),"capture stable private settings generation");
 checkProfile(!claudeConfigureSettings(p,direct)&&claudeSettingsStillMatches(fd,"settings.json",unchanged)&&claudeSettingsStillMatches(fd,"settings.json.appdeck-previous",previous),"an identical merge rewrites neither settings nor their backup");close(fd);
 checkProfile(!claudeConfigureSettings(legacy,env)&&!exists(claudeCodeRoot(legacy)),"settings synchronization never creates or changes a legacy Code root");
 Obj invalidEnv=autoRelease(send(env,"mutableCopy"));put(invalidEnv,"HTTP_PROXY",num(7));before=fixtureBytes(path);
 checkProfile(claudeConfigureSettings(p,invalidEnv)&&same(fixtureBytes(path),before),"non-string managed environment values fail without rewriting settings");
 for(Obj malformed:{str("{"),str("[]"),str("{\"env\":[]}")}){
  checkProfile(writeText(path,malformed),"write only a malformed private settings fixture");before=fixtureBytes(path);Obj oldBackup=fixtureBytes(backup);
  checkProfile(claudeConfigureSettings(p,env)&&same(fixtureBytes(path),before)&&same(fixtureBytes(backup),oldBackup),"malformed settings and env objects fail closed and preserve both files");
 }
 removeFixture(path);Obj error=nullptr;
 checkProfile(send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",path,source,&error),"create a private settings symlink fixture");
 checkProfile(claudeConfigureSettings(p,env)&&symlink(path),"settings symlinks are rejected without replacement");removeFixture(path);
 checkProfile(link(utf8(source),utf8(path))==0,"create a multiply-linked settings fixture");checkProfile(claudeConfigureSettings(p,env)!=nullptr,"multiply-linked settings are rejected");removeFixture(path);
 checkProfile(mkdirPrivate(path),"create a nonregular settings fixture");checkProfile(claudeConfigureSettings(p,env)!=nullptr,"settings directories are rejected");removeFixture(path);
 int large=open(utf8(path),O_WRONLY|O_CREAT|O_EXCL,0600);checkProfile(large>=0&&ftruncate(large,claudeSettingsMaxBytes+1)==0,"create an oversized private settings fixture");close(large);
 checkProfile(claudeConfigureSettings(p,env)&&integer(get(attrs(path),"NSFileSize"))==(Int)claudeSettingsMaxBytes+1,"oversized settings fail without truncation");removeFixture(path);
 checkProfile(writeJSON(path,original),"restore valid private settings");before=fixtureBytes(path);removeFixture(backup);
 checkProfile(send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",backup,source,&error),"create a previous-settings symlink fixture");
 checkProfile(claudeConfigureSettings(p,env)&&same(fixtureBytes(path),before)&&symlink(backup),"a linked backup blocks replacement of either file");removeFixture(backup);
 checkProfile(link(utf8(source),utf8(backup))==0,"create a multiply-linked backup fixture");checkProfile(claudeConfigureSettings(p,env)&&same(fixtureBytes(path),before),"multiply-linked backups block replacement");removeFixture(backup);
 checkProfile(mkdirPrivate(backup),"create a nonregular backup fixture");checkProfile(claudeConfigureSettings(p,env)&&same(fixtureBytes(path),before),"nonregular backups block replacement");removeFixture(backup);
 fd=claudeSettingsDirectory(root);checkProfile(fd>=0&&claudeSettingsRead(fd,"settings.json",unchanged)&&claudeSettingsRead(fd,"settings.json.appdeck-previous",previous),"capture input before an external settings change");
 checkProfile(writeText(path,str("{\"external\":true}")),"simulate an external private settings edit");before=fixtureBytes(path);
 checkProfile(!claudeSettingsApply(fd,original,unchanged,previous)&&same(fixtureBytes(path),before)&&!exists(backup),"an unchanged-input recheck rejects stale settings without saving a stale backup");
 checkProfile(claudeSettingsRead(fd,"settings.json",unchanged)&&claudeSettingsRead(fd,"settings.json.appdeck-previous",previous)&&writeText(backup,str("external backup")),"simulate a backup appearing after input capture");
 checkProfile(!claudeSettingsApply(fd,original,unchanged,previous)&&same(fixtureBytes(path),before)&&same(readText(backup),str("external backup")),"a changed backup is never overwritten by a stale settings update");close(fd);
 if(exists(claudeProfileTemp(p)))removeFixture(claudeProfileTemp(p));
}
}
int main(int argc,char** argv){
 if(argc!=2){dprintf(2,"Usage: claude_profile_test /path/to/claude_profile_fixture\n");return 2;}
 char rootBytes[]="/private/tmp/appdeck-claude-profile-test-XXXXXX";if(!mkdtemp(rootBytes))return 3;
 char homeBytes[512],dataBytes[512];snprintf(homeBytes,sizeof homeBytes,"%s/home",rootBytes);snprintf(dataBytes,sizeof dataBytes,"%s/manager",rootBytes);
 if(setenv("HOME",homeBytes,1)||setenv("APPDECK_DATA_ROOT",dataBytes,1)||setenv("CFFIXED_USER_HOME",homeBytes,1))return 3;
 for(const char* key:{"CLAUDE_USER_DATA_DIR","CLAUDE_CONFIG_DIR","CLAUDE_SECURESTORAGE_CONFIG_DIR","CLAUDE_CODE_TMPDIR","CLAUDECODE","ANTHROPIC_API_KEY","ANTHROPIC_AUTH_TOKEN","ANTHROPIC_BASE_URL","CLAUDE_CODE_OAUTH_TOKEN"})if(setenv(key,"synthetic-inherited-value",1))return 3;
 for(const char* key:{"XDG_CONFIG_HOME","XDG_CACHE_HOME","XDG_DATA_HOME","XDG_STATE_HOME"})if(setenv(key,homeBytes,1))return 3;
 for(const char* key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy","NO_PROXY","no_proxy","grpc_proxy","GRPC_PROXY","no_grpc_proxy","NO_GRPC_PROXY","NODE_USE_ENV_PROXY","CLAUDE_CODE_HTTP_PROXY","CLAUDE_CODE_HTTPS_PROXY","CLAUDE_CODE_PROXY_RESOLVES_HOSTS","CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC","DISABLE_TELEMETRY","DISABLE_ERROR_REPORTING","DISABLE_GROWTHBOOK","DO_NOT_TRACK"})if(setenv(key,"synthetic-inherited-value",1))return 3;
 umask(0077);appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);if(!appKit)return 10;Pool pool;
 app=send(cls("NSApplication"),"sharedApplication");send<bool>(app,"setActivationPolicy:",(Int)1);fileManager=send(cls("NSFileManager"),"defaultManager");workspace=send(cls("NSWorkspace"),"sharedWorkspace");
 fixtureRoot=keep(str(rootBytes));fixtureProcesses=keep(array());home=keep(str(homeBytes));dataRoot=keep(str(dataBytes));
 checkProfile(mkdirPrivate(home)&&mkdirPrivate(dataRoot)&&mkdirPrivate(join(dataRoot,"Profiles"))&&mkdirPrivate(join(dataRoot,"Shared")),"isolate the manager, source and profile directories");
 checkProfile(mkdirPrivate(join(home,".claude"))&&writeText(join(home,".claude/source-sentinel.txt"),str("Original settings stay here.")),"create a synthetic original Code home");
 state=keep(dict());apps=array();profiles=array();projects=array();events=keep(array());put(state,"apps",apps);put(state,"profiles",profiles);put(state,"projects",projects);put(state,"profileOrder",num(1));proxyLoad();
 Obj bundle=makeProfileBundle(canonical(str(argv[1]))),a=registerApplication(bundle,deck::Adapter::Claude,false);checkProfile(a!=nullptr,"register fixture with production Claude adapter");selected=keep(get(a,"id"));
 Obj legacy=newProfile(a,str("Legacy environment"),0,false),first=makeCleanProfile(a,"Clean one"),second=makeCleanProfile(a,"Clean two"),desktopOnly=makeCleanProfile(a,"Existing Desktop-only profile",false);
 put(second,"claudeReduceTraffic",boolean(false));
 Obj required=makeCleanProfile(a,"Required proxy fixture");put(required,"proxyRequired",boolean(true));put(required,"proxyHardened",boolean(true));
 UInt launchedBefore=count(claudeLaunchTasks),bridgesBefore=count(proxyTasks);previewMode=true;bool admitted=launch(required,false);previewMode=false;
 checkProfile(!admitted&&!exists(profileRoot(required))&&!running(required)&&count(claudeLaunchTasks)==launchedBefore&&count(proxyTasks)==bridgesBefore,"required proxy with a direct selection blocks before any profile folder, bridge or child is created");
 put(required,"proxy",str("90000000-0000-4000-8000-000000000001"));previewMode=true;admitted=launch(required,false);previewMode=false;
 checkProfile(!admitted&&!exists(profileRoot(required))&&!running(required)&&count(claudeLaunchTasks)==launchedBefore&&count(proxyTasks)==bridgesBefore,"a removed required proxy never falls back to a direct profile launch");
 checkProfile(!claudeCleanProfile(legacy)&&!get(legacy,"claudeClean"),"old profiles keep their existing environment without migration");
 checkProfile(claudeCodeIsolated(first)&&claudeCodeIsolated(second),"new clean profiles retain their separately versioned Code isolation mode");
 checkProfile(claudeCleanProfile(desktopOnly)&&!claudeCodeIsolated(desktopOnly)&&!get(desktopOnly,"claudeCodeIsolated"),"existing Desktop-only profiles are never silently migrated to private Code storage");
 checkProfile(!prepare(first)&&!prepare(second),"production preparation creates independent empty private folders");
 checkProfile(!exists(join(claudeProfileHome(first),".claude"))&&!exists(join(claudeProfileHome(second),".claude")),"private work folders do not replace or clone the usual Code configuration");
 checkProfile(!same(claudeProfileHome(first),claudeProfileHome(second))&&!same(claudeProfileTemp(first),claudeProfileTemp(second)),"two new profiles never share work or temporary paths");
 checkProfile(!same(claudeCodeRoot(first),claudeCodeRoot(second)),"new profiles never share a Code configuration root");Obj listingError=nullptr;
 checkProfile(count(send(fileManager,"contentsOfDirectoryAtPath:error:",claudeCodeRoot(first),&listingError))==0&&count(send(fileManager,"contentsOfDirectoryAtPath:error:",claudeCodeRoot(second),&listingError))==0,"both private Code roots are empty before their first launches");
 Obj firstObserved=launchProfileFixture(first);checkEnvironment(first,firstObserved);checkProfile(truth(get(firstObserved,"firstRun")),"first clean instance starts without prior Desktop data");
 Obj secondObserved=launchProfileFixture(second);checkEnvironment(second,secondObserved);checkProfile(truth(get(secondObserved,"firstRun")),"another clean Desktop instance starts fresh while the first remains running");
 checkProfile(truth(get(firstObserved,"codeFirstRun"))&&truth(get(secondObserved,"codeFirstRun")),"both new profiles also start with independent empty Code state");
 checkProfile(running(first)&&running(second)&&integer(get(first,"pid"))!=integer(get(second,"pid")),"two clean instances coexist with distinct process identities");
 put(legacy,"claudeReduceTraffic",boolean(true));Obj legacyObserved=launchProfileFixture(legacy);
 checkProfile(same(get(get(legacyObserved,"environment"),"HOME"),home)&&same(get(legacyObserved,"workingDirectory"),home),"legacy profile retains the source home and uses it as its startup directory");
 checkProfile(!get(get(legacyObserved,"environment"),"CLAUDE_CONFIG_DIR")&&truth(get(legacyObserved,"sourceSentinelPresent")),"legacy mode preserves default Code settings without inheriting a configuration override");
 checkProfile(same(get(legacyObserved,"codeRoot"),join(home,".claude"))&&truth(get(legacyObserved,"codeSourceSentinelPresent")),"legacy Code resolves its original settings independently of new isolated profiles");
 for(const char* key:{"CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC","DISABLE_TELEMETRY","DISABLE_ERROR_REPORTING","DISABLE_GROWTHBOOK","DO_NOT_TRACK"})checkProfile(same(get(get(legacyObserved,"environment"),key),str("1")),"an explicit traffic preference also applies to a legacy managed Desktop child without changing its Code root");
 stopProfileFixture(legacy);
 Obj desktopObserved=launchProfileFixture(desktopOnly);checkEnvironment(desktopOnly,desktopObserved);stopProfileFixture(desktopOnly);
 checkProfile(!exists(claudeCodeRoot(legacy))&&!exists(claudeCodeRoot(desktopOnly)),"old profiles never gain a private Code directory as a launch side effect");
 claudeReapLaunches();checkProfile(count(claudeLaunchTasks)==2&&running(first)&&running(second),"reaping an exited child preserves both live profile processes");
 Int firstPid=integer(get(first,"pid")),secondPid=integer(get(second,"pid"));erase(first,"pid");erase(first,"started");
 checkProfile(adoptRunning()&&integer(get(first,"pid"))==firstPid&&integer(get(second,"pid"))==secondPid,"manager rediscovers each copy by its own user-data marker");
 checkProfile(save(),"persist selected clean mode and process identities");load();cardRefs=nullptr;a=currentApp();legacy=at(profiles,0);first=at(profiles,1);second=at(profiles,2);desktopOnly=at(profiles,3);required=at(profiles,4);
 checkProfile(!claudeCleanProfile(legacy)&&claudeCodeIsolated(first)&&claudeCodeIsolated(second)&&claudeCleanProfile(desktopOnly)&&!claudeCodeIsolated(desktopOnly)&&running(first)&&running(second),"reload preserves legacy, old Desktop-only, and new private Code modes without migrating them");
 checkProfile(truth(get(required,"proxyRequired"))&&truth(get(required,"proxyHardened"))&&same(get(required,"proxy"),str("90000000-0000-4000-8000-000000000001"))&&truth(get(first,"claudeReduceTraffic"))&&!truth(get(second,"claudeReduceTraffic")),"reload preserves explicit proxy requirements, routing policy and per-profile optional traffic choices");
 previewMode=true;bool reused=launch(first,false);previewMode=false;checkProfile(reused&&integer(get(first,"pid"))==firstPid,"repeated launch raises the same profile instead of creating a twin");
 stopProfileFixture(first);removeFixture(observationPath(first));firstObserved=launchProfileFixture(first);checkEnvironment(first,firstObserved);
 checkProfile(!truth(get(firstObserved,"firstRun")),"relaunch retains this clean profile's own data instead of resetting it");
 checkProfile(!truth(get(firstObserved,"codeFirstRun")),"relaunch also retains the profile's own private Code data");
 checkProfile(running(second)&&integer(get(second,"pid"))==secondPid,"restarting one profile does not replace the other instance");
 stopProfileFixture(first);stopProfileFixture(second);
 claudeReapLaunches();checkProfile(count(claudeLaunchTasks)==0,"all exited Claude launch tasks are reaped after normal termination");
 addDescriptor(legacy,"30000000-0000-4000-8000-000000000001");addDescriptor(first,"30000000-0000-4000-8000-000000000002");put(a,"claudeSessionBridge",boolean(true));
 Obj catalogue=claudeLoadCatalogue(a);checkProfile(count(catalogue)==1&&same(get(at(catalogue,0),"id"),str("30000000-0000-4000-8000-000000000001")),"shared catalogue never reads clean profiles even when group sharing is enabled");
 put(a,"claudeBridgeLeaseRecorded",boolean(true));checkProfile(claudeLaunchGate(legacy)!=nullptr&&!claudeLaunchGate(first),"clean Desktop profiles are excluded from AppDeck's session-bridge reservation");erase(a,"claudeBridgeLeaseRecorded");
 Obj unsafe=makeCleanProfile(a,"Unsafe work folder"),outside=join(fixtureRoot,"outside");checkProfile(mkdirPrivate(profileRoot(unsafe))&&mkdirPrivate(outside),"create only a local symlink-refusal fixture");
 Obj error=nullptr;checkProfile(send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",claudeProfileHome(unsafe),outside,&error),"create fixture work-folder link");
 checkProfile(prepare(unsafe)!=nullptr,"preparation rejects a linked work folder instead of writing through it");
 checkProfile(count(send(fileManager,"contentsOfDirectoryAtPath:error:",outside,&error))==0,"rejected preparation preserves the link target");
 Obj unsafeTemp=makeCleanProfile(a,"Unsafe temporary folder");
 checkProfile(send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",claudeProfileTemp(unsafeTemp),outside,&error),"create fixture temporary-folder link");
 checkProfile(prepare(unsafeTemp)!=nullptr,"preparation rejects a linked temporary folder");
 checkProfile(count(send(fileManager,"contentsOfDirectoryAtPath:error:",outside,&error))==0,"temporary-folder rejection preserves the link target");removeFixture(claudeProfileTemp(unsafeTemp));
 Obj unsafeCode=makeCleanProfile(a,"Unsafe Code directory");checkProfile(mkdirPrivate(profileRoot(unsafeCode)),"create only a local Code-link fixture");
 checkProfile(send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",claudeCodeRoot(unsafeCode),outside,&error),"create fixture Code-root link");
 checkProfile(prepare(unsafeCode)!=nullptr&&count(send(fileManager,"contentsOfDirectoryAtPath:error:",outside,&error))==0,"private Code preparation rejects links without changing their target");
 checkPrivateSettings(a,legacy);
 checkProfile(same(readText(join(home,".claude/source-sentinel.txt")),str("Original settings stay here.")),"source Code settings remain byte-identical");
 checkProfile(count(send(fileManager,"contentsOfDirectoryAtPath:error:",join(home,".claude"),&error))==1,"creating and launching isolated profiles adds nothing to the source Code directory");
 for(Obj p:{first,second,desktopOnly,unsafe,unsafeTemp,unsafeCode})if(exists(claudeProfileTemp(p)))removeFixture(claudeProfileTemp(p));removeFixture(fixtureRoot);
 dprintf(1,"%u clean Claude profile assertions passed with native fixtures; no Claude or real profile was launched.\n",assertions);return 0;
}
