#include "../src/mac.hpp"
using namespace mac;
extern "C" int puts(const char*);
#include "../src/usage_probe.hpp"
#include "../src/project_server_sync.hpp"

int main(int argc,char** argv){
 if((argc!=3&&argc!=4)||strncmp(argv[2],"/private/tmp/appdeck-project-adapter-",37)){puts("Usage: project_server_sync_test <vendor-codex-binary> /private/tmp/appdeck-project-adapter-<unique> [--protocol-fixture]");return 2;}
 if(!dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8))return 3;Pool pool;
 Obj fm=send(cls("NSFileManager"),"defaultManager"),root=str(argv[2]);
 // Refuse an existing root: this acceptance test can only create new scratch profiles/databases.
 if(send<bool>(fm,"fileExistsAtPath:",root)){puts("FAIL: scratch root already exists");return 4;}
 for(const char* leaf:{"home-a","home-b","database","workspace"}){Obj error=nullptr,path=send(root,"stringByAppendingPathComponent:",str(leaf));
  if(!send<bool>(fm,"createDirectoryAtPath:withIntermediateDirectories:attributes:error:",path,true,nullptr,&error))return 5;}
 auto request=[&](const char* home){Obj r=dict(),env=dict();put(env,"HOME",root);put(env,"PATH",str("/usr/bin:/bin:/usr/sbin:/sbin"));put(env,"CODEX_HOME",send(root,"stringByAppendingPathComponent:",str(home)));put(env,"CODEX_SQLITE_HOME",send(root,"stringByAppendingPathComponent:",str("database")));put(r,"binary",str(argv[1]));put(r,"scratch",send(root,"stringByAppendingPathComponent:",str("workspace")));put(r,"environment",env);return r;};
 Obj a=request("home-a"),b=request("home-b"),desired=dict(),project=dict(),roots=array(),legacy=send(send(cls("NSUUID"),"UUID"),"UUIDString");
 add(roots,send(root,"stringByAppendingPathComponent:",str("workspace")));put(project,"id",legacy);put(project,"name",str("Scratch project"));put(project,"rootPaths",roots);put(desired,utf8(legacy),project);
 auto clean=[](Obj result){if(get(result,"error")){puts(utf8(get(result,"error")));puts(utf8(get(result,"detail")));return false;}return true;};
 if(argc==4){if(strcmp(argv[3],"--protocol-fixture"))return 2;
  Obj mapping=dict(),deleted=array();put(mapping,"legacy-project",str("native-project"));add(deleted,str("legacy-project"));
  Obj removed=syncProjectsWithAppServer(a,dict(),mapping,deleted);if(!clean(removed)||count(get(removed,"removedThreadIds"))!=3){puts("FAIL: paginated active and archived project membership cleanup");return 20;}
  put(get(a,"environment"),"APPDECK_PROJECT_FIXTURE_UNRELATED",str("1"));Obj unrelated=syncProjectsWithAppServer(a,dict(),mapping,deleted);
  if(!same(get(unrelated,"error"),str("schema"))||count(get(unrelated,"removedThreadIds"))){puts("FAIL: unrelated thread accepted for deletion");return 21;}
  puts("PASS: offline protocol fixture: active/archived pagination, explicit projectless assignment, unrelated-membership rejection.");return 0;
 }
 Obj first=syncProjectsWithAppServer(a,desired,dict(),array());if(!clean(first)||!get(get(first,"mapping"),utf8(legacy))){puts("FAIL: native import");return 10;}
 Obj nativeId=get(get(first,"mapping"),utf8(legacy));
 Obj second=syncProjectsWithAppServer(b,desired,dict(),array());if(!clean(second)||!same(get(get(second,"mapping"),utf8(legacy)),nativeId)){puts("FAIL: shared database idempotent import across CODEX_HOME");return 11;}
 put(project,"name",str("Rename after an uncommitted mapping"));Obj recovered=syncProjectsWithAppServer(b,desired,dict(),array());if(!clean(recovered)||!same(get(get(recovered,"mapping"),utf8(legacy)),nativeId))return 22;
 put(project,"name",str("Renamed scratch project"));Obj renamed=syncProjectsWithAppServer(a,desired,get(first,"mapping"),array());if(!clean(renamed))return 12;
 {Obj result=dict();ProjectServerRPC rpc(result);if(!rpc.start(b))return 13;Obj all=projectServerList(rpc);if(!all||!same(get(get(all,utf8(nativeId)),"name"),get(project,"name"))){puts("FAIL: native update");return 13;}}
 Obj deleted=array();add(deleted,legacy);Obj removed=syncProjectsWithAppServer(b,dict(),get(first,"mapping"),deleted);if(!clean(removed))return 14;
 if(!clean(syncProjectsWithAppServer(a,dict(),get(first,"mapping"),deleted))){puts("FAIL: retry after deletion");return 15;}
 {Obj result=dict();ProjectServerRPC rpc(result);if(!rpc.start(a))return 16;Obj all=projectServerList(rpc);if(!all||count(all)){puts("FAIL: native project remained after deletion");return 16;}}
 Obj resurrect=syncProjectsWithAppServer(a,desired,get(first,"mapping"),array());Obj missing=get(resurrect,"missingLegacyIds");
 if(!same(get(resurrect,"error"),str("missing-project"))||count(missing)!=1||!same(at(missing,0),legacy)){puts("FAIL: deleted mapping was resurrected or missing identity was lost");return 17;}
 // A complete absence report must include every known missing mapping, exclude unmapped/new
 // and still-live projects, and happen before an unrelated import or rename can be applied.
 Obj extra=dict(),live=dict(),lost=dict(),liveId=send(send(cls("NSUUID"),"UUID"),"UUIDString"),lostId=send(send(cls("NSUUID"),"UUID"),"UUIDString");
 for(Obj entry:{live,lost}){put(entry,"rootPaths",roots);put(entry,"id",entry==live?liveId:lostId);put(entry,"name",str(entry==live?"Still live":"Second deletion"));put(extra,utf8(get(entry,"id")),entry);}
 Obj created=syncProjectsWithAppServer(a,extra,dict(),array());if(!clean(created))return 23;Obj allMappings=dict();send<void>(allMappings,"addEntriesFromDictionary:",get(first,"mapping"));send<void>(allMappings,"addEntriesFromDictionary:",get(created,"mapping"));
 Obj lostTombstone=array();add(lostTombstone,lostId);erase(extra,utf8(lostId));if(!clean(syncProjectsWithAppServer(a,extra,allMappings,lostTombstone)))return 24;
 Obj newEntry=dict(),newId=send(send(cls("NSUUID"),"UUID"),"UUIDString");put(newEntry,"id",newId);put(newEntry,"name",str("Must not import"));put(newEntry,"rootPaths",roots);put(extra,utf8(newId),newEntry);put(extra,utf8(legacy),project);put(extra,utf8(lostId),lost);put(live,"name",str("Must not rename"));
 Obj absent=syncProjectsWithAppServer(a,extra,allMappings,array());missing=get(absent,"missingLegacyIds");
 if(!same(get(absent,"error"),str("missing-project"))||count(missing)!=2||!send<bool>(missing,"containsObject:",legacy)||!send<bool>(missing,"containsObject:",lostId)||send<bool>(missing,"containsObject:",liveId)||send<bool>(missing,"containsObject:",newId)){puts("FAIL: incomplete or unrelated native-deletion identities");return 25;}
 {Obj result=dict();ProjectServerRPC rpc(result);if(!rpc.start(a))return 26;Obj all=projectServerList(rpc),survivor=all?get(all,utf8(get(allMappings,utf8(liveId)))):nullptr;
  if(!all||count(all)!=1||!same(get(survivor,"name"),str("Still live"))){puts("FAIL: native mutation occurred before missing-project report");return 26;}}
 Obj conflict=syncProjectsWithAppServer(a,desired,get(first,"mapping"),deleted);if(!same(get(conflict,"error"),str("schema"))){puts("FAIL: conflicting plan accepted");return 18;}
 Obj incomplete=dict();put(incomplete,"binary",str(argv[1]));Obj bad=syncProjectsWithAppServer(incomplete,dict(),dict(),array());if(!same(get(bad,"error"),str("configuration"))){puts("FAIL: implicit real profile accepted");return 19;}
 puts("PASS: native project import/update/delete, shared CODEX_SQLITE_HOME, stable idempotency, retry, and fail-closed deleted/invalid plans. Scratch profiles only; no authentication or model turn.");return 0;
}
