// macOS-only production integration, confined to a fresh /private/tmp fixture.
// Usage: group_sync_test /path/to/vendor/Codex.app/Contents/Resources/codex
// The vendor app-server receives isolated HOME/CODEX_HOME/CODEX_SQLITE_HOME values.
// It performs local project metadata operations only; all automation fixtures are PAUSED.
#define main appdeckApplicationMain
#include "../src/main.cpp"
#undef main
extern "C" int dprintf(int,const char*,...);
extern "C" int setenv(const char*,const char*,int);

namespace {
Obj fixtureRoot=nullptr;
unsigned assertions=0;
void integrationCheck(bool value,const char* detail){
 ++assertions;if(value)return;dprintf(2,"FAIL: %s\nFixture retained: %s\n",detail,utf8(fixtureRoot));exit(1);
}
Obj fixtureProject(const char* id,const char* name,const char* path){
 Obj project=dict(),roots=array();add(roots,join(fixtureRoot,path));put(project,"id",str(id));put(project,"name",str(name));put(project,"rootPaths",roots);
 put(project,"createdAt",num(100));put(project,"updatedAt",num(200));return project;
}
Obj fixtureDesktop(const char* sentinel,Obj p=nullptr){
 Obj current=dict(),registry=dict(),order=array();if(p){put(registry,utf8(get(p,"id")),p);add(order,get(p,"id"));}
 put(current,"local-projects",registry);put(current,"project-order",order);
 Obj window=dict();put(window,"fixtureOnly",str(sentinel));put(window,"width",num(987));put(current,"electron-persisted-atom-state",window);
 put(current,"synthetic-account-label",str(sentinel));return current;
}
Obj fixtureStatePath(Obj codexHome){return join(codexHome,".codex-global-state.json");}
void fixtureWrite(Obj codexHome,Obj desktop){integrationCheck(writeJSON(fixtureStatePath(codexHome),desktop,0),"write fixture Desktop state");}
Obj fixtureRead(Obj codexHome){Obj desktop=readJSON(fixtureStatePath(codexHome));integrationCheck(desktop!=nullptr,"read fixture Desktop state");return desktop;}
void fixtureProjectCount(Obj codexHome,UInt expected,const char* detail){integrationCheck(count(get(fixtureRead(codexHome),"local-projects"))==expected,detail);}
void fixturePreserved(Obj codexHome,const char* sentinel){
 Obj current=fixtureRead(codexHome);integrationCheck(same(get(current,"synthetic-account-label"),str(sentinel)),"profile-local synthetic account label is preserved");
 integrationCheck(same(get(get(current,"electron-persisted-atom-state"),"fixtureOnly"),str(sentinel))&&integer(get(get(current,"electron-persisted-atom-state"),"width"))==987,"profile-local window state is preserved");
}
void fixtureSync(Obj a,const char* phase){
 Obj failure=syncGroup(a,true);if(failure)dprintf(2,"Sync phase %s: %s\n",phase,utf8(failure));integrationCheck(!failure,phase);
}
Obj fixtureNativeProjects(Obj a){
 Obj members=groupSyncParticipants(a),request=groupProjectRequest(a,at(members,0)),result=dict();ProjectServerRPC rpc(result);
 integrationCheck(rpc.start(request),"start isolated native project inspection");Obj native=projectServerList(rpc);
 integrationCheck(native&&!get(result,"error"),"read isolated native project registry");return native;
}
}

int main(int argc,char** argv){
 if(argc!=2){dprintf(2,"Usage: group_sync_test /path/to/vendor/codex\n");return 2;}
 umask(0077);appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);if(!appKit)return 10;
 Pool pool;fileManager=send(cls("NSFileManager"),"defaultManager");workspace=send(cls("NSWorkspace"),"sharedWorkspace");
 fixtureRoot=keep(join(str("/private/tmp"),cat(str("appdeck-group-sync-test-"),uuid())));
 integrationCheck(!exists(fixtureRoot)&&mkdirPrivate(fixtureRoot),"create fresh isolated fixture");
 dataRoot=keep(join(fixtureRoot,"manager"));home=keep(join(fixtureRoot,"home"));
 integrationCheck(mkdirPrivate(dataRoot)&&mkdirPrivate(home)&&mkdirPrivate(join(dataRoot,"Profiles"))&&mkdirPrivate(join(dataRoot,"Shared")),"create isolated manager directories");
 integrationCheck(setenv("HOME",utf8(home),1)==0,"isolate helper HOME");
 state=keep(dict());apps=array();profiles=array();projects=array();events=keep(array());put(state,"apps",apps);put(state,"profiles",profiles);put(state,"projects",projects);
 Obj a=dict(),base=join(fixtureRoot,"base"),fakeBundle=join(fixtureRoot,"FixtureCodex.app");
 put(a,"id",str("00000000-0000-4000-8000-000000000001"));put(a,"name",str("Isolated fixture"));put(a,"adapter",str("codex"));
 put(a,"baseSource",base);put(a,"sharedHistory",boolean(true));put(a,"path",fakeBundle);put(a,"bundleId",str("local.appdeck.group-sync-fixture"));add(apps,a);selected=get(a,"id");
 integrationCheck(mkdirPrivate(base)&&mkdirPrivate(join(fakeBundle,"Contents/Resources")),"create isolated base and fake bundle");
 Obj error=nullptr;integrationCheck(send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",join(fakeBundle,"Contents/Resources/codex"),str(argv[1]),&error),"link only the vendor helper executable into fake bundle");
 integrationCheck(usageBinary(a)!=nullptr&&!groupBaseRunning(a),"fixture helper available and base is stopped");
 Obj first=dict(),second=dict();put(first,"id",str("00000000-0000-4000-8000-000000000011"));put(second,"id",str("00000000-0000-4000-8000-000000000012"));
 for(Obj p:{first,second}){put(p,"appId",get(a,"id"));put(p,"name",get(p,"id"));put(p,"share",boolean(true));add(profiles,p);integrationCheck(mkdirPrivate(join(profileRoot(p),"codex")),"create isolated copy CODEX_HOME");}
 Obj firstHome=join(profileRoot(first),"codex"),secondHome=join(profileRoot(second),"codex");
 for(const char* path:{"workspace/base","workspace/copy","workspace/new"})integrationCheck(mkdirPrivate(join(fixtureRoot,path)),"create synthetic project directory");
 fixtureWrite(base,fixtureDesktop("base-window",fixtureProject("local-base","Base project","workspace/base")));
 fixtureWrite(firstHome,fixtureDesktop("first-window",fixtureProject("local-copy","Copy project","workspace/copy")));
 fixtureWrite(secondHome,fixtureDesktop("second-window"));
 Obj automation=cat(str("version = 1\nid = \"fixture-paused\"\nkind = \"cron\"\nname = \"Fixture paused schedule\"\nprompt = \"Synthetic local verification; do not run.\"\nstatus = \"PAUSED\"\nrrule = \"FREQ=HOURLY;INTERVAL=1\"\ncwds = [\""),cat(join(fixtureRoot,"workspace/base"),str("\"]\ncreated_at = 1\nupdated_at = 2\n")));
 integrationCheck(automationDefinition(automation)!=nullptr,"paused automation fixture matches recognized schema");
 Obj automationDir=join(base,"automations/fixture-paused");integrationCheck(mkdirPrivate(automationDir)&&writeText(join(automationDir,"automation.toml"),automation),"seed paused automation definition");
 fixtureSync(a,"startup union");
 for(Obj codexHome:{base,firstHome,secondHome})fixtureProjectCount(codexHome,2,"startup union reaches every stopped profile");
 integrationCheck(count(fixtureNativeProjects(a))==2,"native project registry receives startup union without duplicates");
 for(Obj codexHome:{base,firstHome,secondHome}){
  Obj text=readText(join(codexHome,"automations/fixture-paused/automation.toml"));integrationCheck(text&&same(get(automationDefinition(text),"status"),str("PAUSED")),"automation definition is present and paused in every profile");
 }
 fixturePreserved(base,"base-window");fixturePreserved(firstHome,"first-window");fixturePreserved(secondHome,"second-window");
 integrationCheck(exists(cat(fixtureStatePath(base),str(".appdeck-previous")))&&groupSyncLoad(a)!=nullptr,"previous Desktop state and durable group journal exist");
 Obj beforeJournal=groupSyncLoad(a);fixtureSync(a,"idempotent second synchronization");integrationCheck(same(beforeJournal,groupSyncLoad(a)),"unchanged synchronization leaves durable journal semantically identical");

 // An established member losing its entire registry file is unavailable, not an
 // explicit empty list. Keep its durable baseline and recover from the same file.
 Obj missingStateBackup=join(fixtureRoot,"temporarily-missing-global-state.json"),missingError=nullptr;
 integrationCheck(send<bool>(fileManager,"moveItemAtPath:toPath:error:",fixtureStatePath(firstHome),missingStateBackup,&missingError),"temporarily remove established global state without destroying it");
 integrationCheck(syncGroup(a,true)!=nullptr,"missing established global state defers launch");
 integrationCheck(!exists(fixtureStatePath(firstHome)),"missing established registry is not synthesized as an empty state");
 fixtureProjectCount(base,2,"missing member cannot delete projects from base");fixtureProjectCount(secondHome,2,"missing member cannot delete projects from other copy");
 integrationCheck(count(get(projectSyncSnapshot(get(groupSyncLoad(a),"projects")),"local-projects"))==2,"missing member cannot tombstone canonical projects");
 integrationCheck(send<bool>(fileManager,"moveItemAtPath:toPath:error:",missingStateBackup,fixtureStatePath(firstHome),&missingError),"restore the exact missing global state");
 fixtureSync(a,"missing established registry restored");fixtureProjectCount(firstHome,2,"restored member recovers without losing projects");

 // A disappeared automation directory is likewise incomplete, not removal of
 // every previously observed definition. Other members must keep their files.
 Obj missingAutomationBackup=join(fixtureRoot,"temporarily-missing-automations"),automationFolder=join(firstHome,"automations");
 integrationCheck(send<bool>(fileManager,"moveItemAtPath:toPath:error:",automationFolder,missingAutomationBackup,&missingError),"temporarily remove established automation directory");
 // Returning a pending/unavailable error is permitted; global deletion is not.
 syncGroup(a,true);Obj survivingAutomation=get(get(get(groupSyncLoad(a),"automations"),"records"),"fixture-paused");
 integrationCheck(survivingAutomation&&!truth(get(survivingAutomation,"deleted")),"missing automation directory cannot tombstone the shared definition");
 for(Obj codexHome:{base,secondHome})integrationCheck(same(get(automationDefinition(readText(join(codexHome,"automations/fixture-paused/automation.toml"))),"status"),str("PAUSED")),"other profiles retain paused automation while member directory is unavailable");
 integrationCheck(!exists(automationFolder),"incomplete automation directory is not silently replaced");
 integrationCheck(send<bool>(fileManager,"moveItemAtPath:toPath:error:",missingAutomationBackup,automationFolder,&missingError),"restore the exact missing automation directory");
 fixtureSync(a,"missing automation directory restored");

 // A project created in a copy reaches the base and another stopped copy.
 Obj firstState=fixtureRead(firstHome),newProject=fixtureProject("local-new","Created in first copy","workspace/new");
 put(get(firstState,"local-projects"),"local-new",newProject);add(get(firstState,"project-order"),str("local-new"));fixtureWrite(firstHome,firstState);
 fixtureSync(a,"copy-created project");for(Obj codexHome:{base,firstHome,secondHome})fixtureProjectCount(codexHome,3,"copy-created project reaches every profile");
 integrationCheck(count(fixtureNativeProjects(a))==3,"native registry receives copy-created project without duplicates");

 // Observe deletion but deliberately leave external states stale, emulating a
 // restart between journal commit and application. A second sync must not undo it.
 Obj secondState=fixtureRead(secondHome);erase(get(secondState,"local-projects"),"local-base");fixtureWrite(secondHome,secondState);
 integrationCheck(syncGroup(a,false)==nullptr,"persist deletion without applying it");
 Obj pendingJournal=groupSyncLoad(a),pendingRecord=get(get(get(pendingJournal,"projects"),"records"),"local-base");
 integrationCheck(truth(get(pendingRecord,"deleted")),"deletion is durably recorded before applying destinations");
 fixtureProjectCount(base,3,"base deliberately remains stale before application");
 fixtureSync(a,"saved tombstone survives stale profiles");for(Obj codexHome:{base,firstHome,secondHome})fixtureProjectCount(codexHome,2,"stale profiles do not resurrect deleted project");
 integrationCheck(count(fixtureNativeProjects(a))==2,"native registry removes the mapped deleted project");

 // Unknown schema must remain byte-for-meaning intact, while healthy members sync.
 Obj unknown=dict();put(unknown,"future-project-schema",array());put(unknown,"synthetic-account-label",str("unknown-preserved"));fixtureWrite(secondHome,unknown);
 firstState=fixtureRead(firstHome);erase(get(firstState,"local-projects"),"local-copy");fixtureWrite(firstHome,firstState);
 integrationCheck(syncGroup(a,true)!=nullptr,"unknown schema defers launch and reports the unsupported member");integrationCheck(same(unknown,fixtureRead(secondHome)),"unknown profile state is not overwritten");
 fixtureProjectCount(base,1,"healthy base still receives deletion with unknown member");fixtureProjectCount(firstHome,1,"healthy copy still receives deletion with unknown member");
 // Restore the previously recognized member from the current canonical projection.
 Obj restored=fixtureDesktop("second-window");Obj canonical=projectSyncSnapshot(get(groupSyncLoad(a),"projects"));
 put(restored,"local-projects",get(canonical,"local-projects"));put(restored,"project-order",get(canonical,"project-order"));
 // Keep its native mapping: replacing fixture metadata must not emulate an unrelated
 // legacy import when the purpose of this phase is rejoining after an unreadable file.
 put(restored,"app-server-project-id-by-legacy-project-id-by-host",get(secondState,"app-server-project-id-by-legacy-project-id-by-host"));fixtureWrite(secondHome,restored);
 fixtureSync(a,"recognized member rejoins");

 // An explicit empty list removes the last project everywhere, without touching
 // any unrelated window state or activating an automation.
 firstState=fixtureRead(firstHome);put(firstState,"local-projects",dict());put(firstState,"project-order",array());fixtureWrite(firstHome,firstState);
 fixtureSync(a,"delete final project");for(Obj codexHome:{base,firstHome,secondHome})fixtureProjectCount(codexHome,0,"last-project deletion reaches valid empty state everywhere");
 integrationCheck(count(fixtureNativeProjects(a))==0,"native registry has no residual projects after final deletion");

 // Exercise AppDeck's own edit entry point. Its applied baseline must not alias
 // the mutable snapshot which the add/remove operation edits immediately after it.
 Obj managerProjectPath=join(fixtureRoot,"workspace/manager-created");integrationCheck(mkdirPrivate(managerProjectPath),"create project for AppDeck edit integration");
 integrationCheck(groupSyncEditProject(a,managerProjectPath,nullptr)==nullptr,"AppDeck add-project action reconciles and applies successfully");
 canonical=projectSyncSnapshot(get(groupSyncLoad(a),"projects"));Obj managerRegistry=get(canonical,"local-projects"),managerIds=send(managerRegistry,"allKeys");
 integrationCheck(count(managerIds)==1,"AppDeck add changes the durable canonical registry independently of its baseline");
 Obj managerProjectId=at(managerIds,0);integrationCheck(deck::uuid(utf8(managerProjectId))&&groupContains(get(get(managerRegistry,utf8(managerProjectId)),"rootPaths"),managerProjectPath),"AppDeck add preserves new native project ID and root path");
 for(Obj codexHome:{base,firstHome,secondHome})fixtureProjectCount(codexHome,1,"AppDeck add reaches every stopped profile");
 integrationCheck(count(fixtureNativeProjects(a))==1,"AppDeck add reaches native project registry");
 integrationCheck(groupSyncEditProject(a,nullptr,managerProjectId)==nullptr,"AppDeck remove-project action reconciles and applies successfully");
 canonical=projectSyncSnapshot(get(groupSyncLoad(a),"projects"));integrationCheck(count(get(canonical,"local-projects"))==0,"AppDeck remove changes canonical registry rather than mutating its own baseline");
 for(Obj codexHome:{base,firstHome,secondHome})fixtureProjectCount(codexHome,0,"AppDeck remove reaches every stopped profile");
 integrationCheck(count(fixtureNativeProjects(a))==0,"AppDeck remove reaches native project registry");

 // Codex can delete a native project while a migrated Desktop registry retains
 // its legacy cache entry. A confirmed missing mapped native ID is authoritative:
 // record a tombstone and remove the cached ghost without importing it again.
 Obj ghostPath=join(fixtureRoot,"workspace/native-deleted");integrationCheck(mkdirPrivate(ghostPath),"create mapped native-deletion fixture project");
 integrationCheck(groupSyncEditProject(a,ghostPath,nullptr)==nullptr,"seed a mapped project before native-only deletion");
 Obj ghostCanonical=projectSyncSnapshot(get(groupSyncLoad(a),"projects")),ghostKeys=send(get(ghostCanonical,"local-projects"),"allKeys");
 integrationCheck(count(ghostKeys)==1,"native-deletion fixture has one canonical project");
 Obj ghostId=at(ghostKeys,0),ghostMembers=groupSyncParticipants(a),ghostBaseEntry=at(ghostMembers,0);
 Obj ghostNativeId=get(groupProjectMapping(fixtureRead(base),ghostBaseEntry),utf8(ghostId));integrationCheck(ghostNativeId!=nullptr,"ghost fixture has a confirmed native project mapping");
 {
  Obj deletionResult=dict();ProjectServerRPC nativeDeletion(deletionResult);
  integrationCheck(nativeDeletion.start(groupProjectRequest(a,ghostBaseEntry)),"start isolated native-only deletion request");
  Obj members=projectServerMemberIds(nativeDeletion,ghostNativeId);integrationCheck(members&&count(members)==0,"native-only deletion fixture contains no threads");
  Obj parameters=dict();put(parameters,"projectId",ghostNativeId);integrationCheck(nativeDeletion.call("project/delete",parameters)&&!get(deletionResult,"error"),"delete mapped project through Codex API without editing Desktop caches");
 }
 integrationCheck(count(fixtureNativeProjects(a))==0,"native-only deletion is committed before group reconciliation");
 for(Obj codexHome:{base,firstHome,secondHome})fixtureProjectCount(codexHome,1,"every Desktop registry deliberately retains the stale mapped ghost");
 fixtureSync(a,"authoritative native deletion reconciles stale Desktop ghost");
 Obj ghostJournal=groupSyncLoad(a),ghostRecord=get(get(get(ghostJournal,"projects"),"records"),utf8(ghostId));
 integrationCheck(ghostRecord&&truth(get(ghostRecord,"deleted")),"confirmed native absence becomes a durable canonical tombstone");
 integrationCheck(count(get(projectSyncSnapshot(get(ghostJournal,"projects")),"local-projects"))==0,"canonical projection excludes the native-deleted ghost");
 for(Obj codexHome:{base,firstHome,secondHome})fixtureProjectCount(codexHome,0,"all stopped Desktop caches remove the native-deleted ghost");
 integrationCheck(count(fixtureNativeProjects(a))==0,"stale legacy registry never resurrects the native-deleted project");
 fixtureSync(a,"native deletion remains stable after repeat synchronization");
 integrationCheck(count(fixtureNativeProjects(a))==0&&truth(get(get(get(get(groupSyncLoad(a),"projects"),"records"),utf8(ghostId)),"deleted")),"native-deletion tombstone survives repeat synchronization");

 // The assigned execution owner cannot be detached while the definition exists;
 // transferring ownership releases the old owner without activating the schedule.
 Obj ownerJournal=groupSyncLoad(a),automationState=get(ownerJournal,"automations");
 integrationCheck(automationSyncSetOwner(automationState,str("fixture-paused"),get(first,"id"))&&groupSyncSave(a,ownerJournal),"assign fixture automation to first copy");
 fixtureSync(a,"paused automation owner assigned");
 integrationCheck(groupOwnerRemovalError(first)!=nullptr&&groupOwnerRemovalError(second)==nullptr,"owner lifecycle guard blocks only the assigned copy");
 ownerJournal=groupSyncLoad(a);automationState=get(ownerJournal,"automations");
 integrationCheck(automationSyncSetOwner(automationState,str("fixture-paused"),get(second,"id"))&&groupSyncSave(a,ownerJournal),"transfer fixture automation to second copy");
 fixtureSync(a,"paused automation ownership transferred");
 integrationCheck(groupOwnerRemovalError(first)==nullptr&&groupOwnerRemovalError(second)!=nullptr,"ownership transfer releases old owner and protects new owner");
 fixturePreserved(base,"base-window");fixturePreserved(firstHome,"first-window");fixturePreserved(secondHome,"second-window");
 for(Obj codexHome:{base,firstHome,secondHome})integrationCheck(same(get(automationDefinition(readText(join(codexHome,"automations/fixture-paused/automation.toml"))),"status"),str("PAUSED")),"project synchronization never activates paused automations");
 integrationCheck(groupSyncLoad(a)!=nullptr,"final persisted journal validates after restart");
 dprintf(1,"%u production group-sync assertions passed (native app-server, isolated fixture).\n",assertions);
 Obj cleanupError=nullptr;integrationCheck(send<bool>(fileManager,"removeItemAtPath:error:",fixtureRoot,&cleanupError),"remove only the freshly created isolated fixture");return 0;
}
