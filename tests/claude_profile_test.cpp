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
 for(const char* key:{"CFFIXED_USER_HOME","CLAUDE_CONFIG_DIR","CLAUDE_SECURESTORAGE_CONFIG_DIR","CLAUDE_CODE_TMPDIR","CLAUDECODE","ANTHROPIC_API_KEY","ANTHROPIC_AUTH_TOKEN","ANTHROPIC_BASE_URL","CLAUDE_CODE_OAUTH_TOKEN"})checkProfile(!get(env,key),"inherited Claude paths and identity overrides are truly absent, including nullish-fallback configuration");
 bool marker=false;Obj args=get(observation,"arguments");for(UInt i=0;i<count(args);++i)if(same(at(args,i),cat(str("--user-data-dir="),join(profileRoot(p),"user-data"))))marker=true;
 checkProfile(marker,"durable user-data argument identifies this profile");checkProfile(truth(get(observation,"sourceSentinelPresent")),"clean Desktop retains access to the usual Code settings through native HOME");
 checkPrivateDir(ph);checkPrivateDir(tmp);checkPrivateDir(join(profileRoot(p),"user-data"));
}
Obj makeCleanProfile(Obj a,const char* name){Obj p=newProfile(a,str(name),0,false);put(p,"claudeClean",boolean(true));put(p,"proxy",str("none"));return p;}
void addDescriptor(Obj p,const char* id){
 Obj dir=join(join(join(join(profileRoot(p),"user-data"),"claude-code-sessions"),"10000000-0000-4000-8000-000000000001"),"20000000-0000-4000-8000-000000000001");
 checkProfile(mkdirPrivate(dir),"create isolated synthetic session descriptor directory");Obj record=dict();put(record,"sessionId",cat(str("local_"),str(id)));put(record,"cliSessionId",str(id));
 put(record,"cwd",join(fixtureRoot,"project"));put(record,"title",str("Fixture conversation"));put(record,"isArchived",boolean(false));
 checkProfile(writeJSON(join(dir,cat(cat(str("local_"),str(id)),str(".json"))),record),"write only synthetic native session metadata");
}
}
int main(int argc,char** argv){
 if(argc!=2){dprintf(2,"Usage: claude_profile_test /path/to/claude_profile_fixture\n");return 2;}
 char rootBytes[]="/private/tmp/appdeck-claude-profile-test-XXXXXX";if(!mkdtemp(rootBytes))return 3;
 char homeBytes[512],dataBytes[512];snprintf(homeBytes,sizeof homeBytes,"%s/home",rootBytes);snprintf(dataBytes,sizeof dataBytes,"%s/manager",rootBytes);
 if(setenv("HOME",homeBytes,1)||setenv("APPDECK_DATA_ROOT",dataBytes,1)||setenv("CFFIXED_USER_HOME",homeBytes,1))return 3;
 for(const char* key:{"CLAUDE_USER_DATA_DIR","CLAUDE_CONFIG_DIR","CLAUDE_SECURESTORAGE_CONFIG_DIR","CLAUDE_CODE_TMPDIR","CLAUDECODE","ANTHROPIC_API_KEY","ANTHROPIC_AUTH_TOKEN","ANTHROPIC_BASE_URL","CLAUDE_CODE_OAUTH_TOKEN"})if(setenv(key,"synthetic-inherited-value",1))return 3;
 for(const char* key:{"XDG_CONFIG_HOME","XDG_CACHE_HOME","XDG_DATA_HOME","XDG_STATE_HOME"})if(setenv(key,homeBytes,1))return 3;
 umask(0077);appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);if(!appKit)return 10;Pool pool;
 app=send(cls("NSApplication"),"sharedApplication");send<bool>(app,"setActivationPolicy:",(Int)1);fileManager=send(cls("NSFileManager"),"defaultManager");workspace=send(cls("NSWorkspace"),"sharedWorkspace");
 fixtureRoot=keep(str(rootBytes));fixtureProcesses=keep(array());home=keep(str(homeBytes));dataRoot=keep(str(dataBytes));
 checkProfile(mkdirPrivate(home)&&mkdirPrivate(dataRoot)&&mkdirPrivate(join(dataRoot,"Profiles"))&&mkdirPrivate(join(dataRoot,"Shared")),"isolate the manager, source and profile directories");
 checkProfile(mkdirPrivate(join(home,".claude"))&&writeText(join(home,".claude/source-sentinel.txt"),str("Original settings stay here.")),"create a synthetic original Code home");
 state=keep(dict());apps=array();profiles=array();projects=array();events=keep(array());put(state,"apps",apps);put(state,"profiles",profiles);put(state,"projects",projects);put(state,"profileOrder",num(1));proxyLoad();
 Obj bundle=makeProfileBundle(canonical(str(argv[1]))),a=registerApplication(bundle,deck::Adapter::Claude,false);checkProfile(a!=nullptr,"register fixture with production Claude adapter");selected=keep(get(a,"id"));
 Obj legacy=newProfile(a,str("Legacy environment"),0,false),first=makeCleanProfile(a,"Clean one"),second=makeCleanProfile(a,"Clean two");
 checkProfile(!claudeCleanProfile(legacy)&&!get(legacy,"claudeClean"),"old profiles keep their existing environment without migration");
 checkProfile(claudeCleanProfile(first)&&claudeCleanProfile(second),"new clean profiles retain their selected mode");
 checkProfile(!prepare(first)&&!prepare(second),"production preparation creates independent empty private folders");
 checkProfile(!exists(join(claudeProfileHome(first),".claude"))&&!exists(join(claudeProfileHome(second),".claude")),"private work folders do not replace or clone the usual Code configuration");
 checkProfile(!same(claudeProfileHome(first),claudeProfileHome(second))&&!same(claudeProfileTemp(first),claudeProfileTemp(second)),"two new profiles never share work or temporary paths");
 Obj firstObserved=launchProfileFixture(first);checkEnvironment(first,firstObserved);checkProfile(truth(get(firstObserved,"firstRun")),"first clean instance starts without prior Desktop data");
 Obj secondObserved=launchProfileFixture(second);checkEnvironment(second,secondObserved);checkProfile(truth(get(secondObserved,"firstRun")),"another clean Desktop instance starts fresh while the first remains running");
 checkProfile(running(first)&&running(second)&&integer(get(first,"pid"))!=integer(get(second,"pid")),"two clean instances coexist with distinct process identities");
 Obj legacyObserved=launchProfileFixture(legacy);
 checkProfile(same(get(get(legacyObserved,"environment"),"HOME"),home)&&same(get(legacyObserved,"workingDirectory"),home),"legacy profile retains the source home and uses it as its startup directory");
 checkProfile(!get(get(legacyObserved,"environment"),"CLAUDE_CONFIG_DIR")&&truth(get(legacyObserved,"sourceSentinelPresent")),"legacy mode preserves default Code settings without inheriting a configuration override");
 stopProfileFixture(legacy);
 claudeReapLaunches();checkProfile(count(claudeLaunchTasks)==2&&running(first)&&running(second),"reaping an exited child preserves both live profile processes");
 Int firstPid=integer(get(first,"pid")),secondPid=integer(get(second,"pid"));erase(first,"pid");erase(first,"started");
 checkProfile(adoptRunning()&&integer(get(first,"pid"))==firstPid&&integer(get(second,"pid"))==secondPid,"manager rediscovers each copy by its own user-data marker");
 checkProfile(save(),"persist selected clean mode and process identities");load();cardRefs=nullptr;a=currentApp();legacy=at(profiles,0);first=at(profiles,1);second=at(profiles,2);
 checkProfile(!claudeCleanProfile(legacy)&&claudeCleanProfile(first)&&claudeCleanProfile(second)&&running(first)&&running(second),"reload preserves legacy mode, clean mode and independent running identities");
 previewMode=true;bool reused=launch(first,false);previewMode=false;checkProfile(reused&&integer(get(first,"pid"))==firstPid,"repeated launch raises the same profile instead of creating a twin");
 stopProfileFixture(first);removeFixture(observationPath(first));firstObserved=launchProfileFixture(first);checkEnvironment(first,firstObserved);
 checkProfile(!truth(get(firstObserved,"firstRun")),"relaunch retains this clean profile's own data instead of resetting it");
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
 checkProfile(same(readText(join(home,".claude/source-sentinel.txt")),str("Original settings stay here.")),"source Code settings remain byte-identical");
 removeFixture(claudeProfileTemp(first));removeFixture(claudeProfileTemp(second));removeFixture(fixtureRoot);
 dprintf(1,"%u clean Claude profile assertions passed with native fixtures; no Claude or real profile was launched.\n",assertions);return 0;
}
