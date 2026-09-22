// Included after usage_probe.hpp, with mac:: in scope. The caller owns the stopped-profile
// check, group reconciliation and Desktop JSON commit. Only Codex's own project/metadata API
// is used here: no database, credentials, thread contents or model turns are opened by AppDeck.
// request: {binary, scratch, environment:{CODEX_HOME, CODEX_SQLITE_HOME, ...}}
// desiredRegistry: the reconciled local-projects dictionary; knownLegacyToServer: mappings
// collected from this sharing group's Desktop registries; deletedLegacyIds: explicit tombstones.
// result: {mapping, removedThreadIds, unresolvedDeletedIds, missingLegacyIds, error?, detail?, method?}.
// missingLegacyIds is populated only after a complete native list and before any native writes:
// these desired legacy IDs have known mappings whose native records are absent. The caller can
// journal that confirmed observation and reconcile again; this adapter never invents a new ID.
// An error may follow a partial apply. Imports use stable idempotency keys, updates are absolute,
// and only explicitly mapped tombstones are deleted, so the same plan can be retried safely.
bool projectServerString(Obj value,UInt limit=4096){return usageKind(value,"NSString")&&send<UInt>(value,"length")>0&&send<UInt>(value,"length")<=limit;}

struct ProjectServerRPC{
 Obj task=nullptr,writer=nullptr,result=nullptr;int input=-1,output=-1;char* buffer=nullptr;
 Size used=0;Int serial=0;double deadline=0;static constexpr Size capacity=1024*1024+1;
 explicit ProjectServerRPC(Obj reply):result(reply){}
 ~ProjectServerRPC(){if(writer)send<void>(writer,"closeFile");if(task)usageReap(task);free(buffer);}
 void fail(const char* error,const char* detail){if(!get(result,"error")){put(result,"error",str(error));put(result,"detail",str(detail));}}
 bool say(const char* bytes,Size size){
  while(size){double left=deadline-usageUptime();if(left<=0){fail("timeout","Codex project synchronization timed out.");return false;}
   PollFd fd{input,4,0};int ready=poll(&fd,1,left>.25?250:(int)(left*1000)+1);if(ready<0)continue;if(!ready)continue;
   if(fd.revents&(8|16|32)){fail("transport","Codex closed the project synchronization connection.");return false;}
   long sent=write(input,bytes,size);if(sent<=0)continue;bytes+=sent;size-=(Size)sent;
  }return true;
 }
 Obj call(const char* method,Obj params){
  if(get(result,"error"))return nullptr;
  Obj message=dict();put(message,"id",num(++serial));put(message,"method",str(method));put(message,"params",params);
  Obj error=nullptr,data=send(cls("NSJSONSerialization"),"dataWithJSONObject:options:error:",message,(UInt)0,&error);
  if(!data||send<UInt>(data,"length")>capacity-2){fail("schema","Codex project request is not a bounded JSON object.");return nullptr;}
  if(!say((const char*)send<void*>(data,"bytes"),send<UInt>(data,"length"))||!say("\n",1))return nullptr;
  for(;;){bool more=true;while(more){Obj reply=usageLine(buffer,used,more);if(!reply)continue;
    if(get(reply,"method")){if(get(reply,"id")){fail("unsupported","Codex requested an interactive operation during project synchronization.");return nullptr;}continue;}
    if(!usageKind(get(reply,"id"),"NSNumber")||integer(get(reply,"id"))!=serial)continue;
    Obj problem=get(reply,"error");if(problem){put(result,"method",str(method));
     // Do not expose arbitrary helper messages (they can include account or local configuration data).
     fail(integer(get(problem,"code"))==-32601?"unsupported":"server","Codex rejected the project synchronization operation.");return nullptr;}
    Obj value=get(reply,"result");if(!usageKind(value,"NSDictionary")){fail("schema","Codex returned an unsupported project response.");return nullptr;}return value;
   }
   if(!usageFill(output,buffer,capacity,used,deadline)){fail(usageUptime()>=deadline?"timeout":"transport","Codex did not complete the project synchronization response.");return nullptr;}
  }
 }
 bool start(Obj request){
  Obj environment=usageKind(request,"NSDictionary")?get(request,"environment"):nullptr;
  if(!usageKind(environment,"NSDictionary")||!projectServerString(get(environment,"CODEX_HOME"))||!projectServerString(get(environment,"CODEX_SQLITE_HOME"))||!projectServerString(get(request,"binary"))||!projectServerString(get(request,"scratch"))||utf8(get(environment,"CODEX_HOME"))[0]!='/'||utf8(get(environment,"CODEX_SQLITE_HOME"))[0]!='/'||utf8(get(request,"binary"))[0]!='/'||utf8(get(request,"scratch"))[0]!='/'){fail("configuration","Explicit absolute Codex profile and database locations are required for project synchronization.");return false;}
  buffer=(char*)calloc(capacity,1);if(!buffer){fail("memory","Could not allocate the Codex project response buffer.");return false;}
  Obj in=send(cls("NSPipe"),"pipe"),out=send(cls("NSPipe"),"pipe"),args=array();add(args,str("app-server"));
  task=in&&out?usageTask(request,args,in,out):nullptr;if(!task){fail("spawn","Could not start Codex's project synchronization helper.");return false;}
  writer=send(in,"fileHandleForWriting");input=send<int>(writer,"fileDescriptor");output=send<int>(send(out,"fileHandleForReading"),"fileDescriptor");
  if(fcntl(input,73,1)<0||fcntl(input,4,4)<0){fail("transport","Could not configure the bounded Codex project connection.");return false;} // macOS F_SETNOSIGPIPE, F_SETFL/O_NONBLOCK
  deadline=usageUptime()+30;
  Obj params=dict(),client=dict(),capabilities=dict();put(client,"name",str("appdeck"));put(client,"title",str("AppDeck project synchronization"));put(client,"version",str("1"));put(params,"clientInfo",client);put(capabilities,"experimentalApi",boolean(true));put(params,"capabilities",capabilities);
  if(!call("initialize",params))return false;
  const char* initialized="{\"method\":\"initialized\"}\n";return say(initialized,strlen(initialized));
 }
};

bool projectServerValidRegistry(Obj desired,Obj mapping,Obj deleted){
 if(!usageKind(desired,"NSDictionary")||!usageKind(mapping,"NSDictionary")||!usageKind(deleted,"NSArray")||count(desired)>4096||count(mapping)>16384||count(deleted)>16384)return false;
 Obj keys=send(desired,"allKeys");for(UInt i=0;i<count(keys);++i){Obj key=at(keys,i);if(!projectServerString(key,200))return false;Obj project=get(desired,utf8(key));
  if(!usageKind(project,"NSDictionary")||!same(get(project,"id"),key)||!projectServerString(get(project,"name"),1024))return false;
  Obj roots=get(project,"rootPaths");if(!usageKind(roots,"NSArray")||count(roots)>64)return false;
  for(UInt j=0;j<count(roots);++j)if(!projectServerString(at(roots,j),8192)||utf8(at(roots,j))[0]!='/')return false;
 }
 keys=send(mapping,"allKeys");for(UInt i=0;i<count(keys);++i)if(!projectServerString(at(keys,i),200)||!projectServerString(get(mapping,utf8(at(keys,i))),200))return false;
 for(UInt i=0;i<count(deleted);++i)if(!projectServerString(at(deleted,i),200)||get(desired,utf8(at(deleted,i))))return false;
 return true;
}

Obj projectServerList(ProjectServerRPC& rpc){
 Obj projects=dict(),cursor=nullptr,seen=dict();for(UInt page=0;page<100;++page){Obj params=dict();put(params,"limit",num(100));put(params,"cursor",cursor);
  Obj reply=rpc.call("project/list",params);if(!reply)return nullptr;Obj data=get(reply,"data");if(!usageKind(data,"NSArray")){rpc.fail("schema","Codex returned an invalid project list.");return nullptr;}
  for(UInt i=0;i<count(data);++i){Obj project=at(data,i);if(!usageKind(project,"NSDictionary")||!projectServerString(get(project,"id"),200)||!projectServerString(get(project,"name"),1024)||!usageKind(get(project,"roots"),"NSArray")){rpc.fail("schema","Codex returned an invalid project record.");return nullptr;}put(projects,utf8(get(project,"id")),project);}
  cursor=get(reply,"nextCursor");if(!cursor||usageKind(cursor,"NSNull"))return projects;
  if(!projectServerString(cursor)||get(seen,utf8(cursor))){rpc.fail("schema","Codex returned an invalid project-list cursor.");return nullptr;}put(seen,utf8(cursor),boolean(true));
 }rpc.fail("limit","Codex project list exceeded the synchronization limit.");return nullptr;
}

Obj projectServerMemberIds(ProjectServerRPC& rpc,Obj projectId){
 Obj ids=array(),seenIds=dict(),sources=array();for(const char* source:{"cli","vscode","exec","appServer","subAgent","subAgentReview","subAgentCompact","subAgentThreadSpawn","subAgentOther","unknown"})add(sources,str(source));
 // Fetch IDs before changing memberships so paginated result sets do not move beneath the cursor.
 for(bool archived:{false,true}){Obj cursor=nullptr,seenCursors=dict();bool finished=false;
  for(UInt page=0;page<1000;++page){Obj params=dict();put(params,"projectId",projectId);put(params,"archived",boolean(archived));put(params,"useStateDbOnly",boolean(true));put(params,"modelProviders",array());put(params,"sourceKinds",sources);put(params,"limit",num(100));put(params,"cursor",cursor);
   Obj reply=rpc.call("thread/list",params);if(!reply)return nullptr;Obj data=get(reply,"data");if(!usageKind(data,"NSArray")){rpc.fail("schema","Codex returned an invalid project membership list.");return nullptr;}
   for(UInt i=0;i<count(data);++i){Obj thread=at(data,i);if(!usageKind(thread,"NSDictionary")||!projectServerString(get(thread,"id"),200)||!same(get(thread,"projectId"),projectId)){rpc.fail("schema","Codex returned an unrelated project membership.");return nullptr;}
    Obj id=get(thread,"id");if(!get(seenIds,utf8(id))){put(seenIds,utf8(id),boolean(true));add(ids,id);}}
   cursor=get(reply,"nextCursor");if(!cursor||usageKind(cursor,"NSNull")){finished=true;break;}
   if(!projectServerString(cursor)||get(seenCursors,utf8(cursor))){rpc.fail("schema","Codex returned an invalid membership cursor.");return nullptr;}put(seenCursors,utf8(cursor),boolean(true));
  }if(!finished){rpc.fail("limit","Codex project membership list exceeded the synchronization limit.");return nullptr;}
 }return ids;
}

Obj syncProjectsWithAppServer(Obj request,Obj desiredRegistry,Obj knownLegacyToServer,Obj deletedLegacyIds){
 Obj result=dict(),mapping=dict(),removed=array(),unresolved=array(),missing=array();put(result,"mapping",mapping);put(result,"removedThreadIds",removed);put(result,"unresolvedDeletedIds",unresolved);put(result,"missingLegacyIds",missing);
 ProjectServerRPC rpc(result);if(!projectServerValidRegistry(desiredRegistry,knownLegacyToServer,deletedLegacyIds)){rpc.fail("schema","Project reconciliation contains an unsupported registry or mapping.");return result;}
 send<void>(mapping,"addEntriesFromDictionary:",knownLegacyToServer);
 if(!rpc.start(request))return result;Obj native=projectServerList(rpc);if(!native)return result;
 Obj desiredKeys=send(desiredRegistry,"allKeys");
 // Preflight known mappings before any writes. A disappeared mapped project must not be resurrected
 // under another idempotency key. Reconciliation must account for the original deletion first.
 for(UInt i=0;i<count(desiredKeys);++i){Obj key=at(desiredKeys,i),id=get(mapping,utf8(key));if(!id&&get(native,utf8(key))){id=key;put(mapping,utf8(key),id);}
  if(id&&!get(native,utf8(id)))add(missing,key);}
 if(count(missing)){rpc.fail("missing-project","Reconciled projects no longer exist in Codex; record the native deletion before retrying.");return result;}
 for(UInt i=0;i<count(deletedLegacyIds);++i){Obj key=at(deletedLegacyIds,i),id=get(mapping,utf8(key));if(!id&&get(native,utf8(key))){id=key;put(mapping,utf8(key),id);}
  if(!id){add(unresolved,key);continue;}
  for(UInt j=0;j<count(desiredKeys);++j)if(same(id,get(mapping,utf8(at(desiredKeys,j))))){rpc.fail("conflict","A deleted and a retained project refer to the same Codex project.");return result;}}
 for(UInt i=0;i<count(desiredKeys);++i){Obj key=at(desiredKeys,i),project=get(desiredRegistry,utf8(key)),id=get(mapping,utf8(key)),roots=array(),paths=get(project,"rootPaths");
  for(UInt j=0;j<count(paths);++j){Obj root=dict();put(root,"path",at(paths,j));add(roots,root);}
  Obj previous=id?get(native,utf8(id)):nullptr;if(previous&&same(get(previous,"name"),get(project,"name"))&&same(get(previous,"roots"),roots))continue;
  Obj params=dict();put(params,"name",get(project,"name"));put(params,"roots",roots);put(params,id?"projectId":"idempotencyKey",id?id:key);
  Obj reply=rpc.call(id?"project/update":"project/import",params);if(!reply)return result;Obj written=get(reply,"project");
  if(!usageKind(written,"NSDictionary")||!projectServerString(get(written,"id"),200)||(id&&!same(id,get(written,"id")))){rpc.fail("schema","Codex returned an invalid updated project identity.");return result;}
  put(mapping,utf8(key),get(written,"id"));put(native,utf8(get(written,"id")),written);
  // Import can recover an earlier successful attempt whose mapping was not committed. Its
  // idempotency reply preserves the earlier content; apply the current desired edit explicitly.
  if(!id&&(!same(get(written,"name"),get(project,"name"))||!same(get(written,"roots"),roots))){erase(params,"idempotencyKey");put(params,"projectId",get(written,"id"));reply=rpc.call("project/update",params);if(!reply)return result;written=get(reply,"project");}
  if(!usageKind(written,"NSDictionary")||!same(get(written,"id"),get(mapping,utf8(key)))||!same(get(written,"name"),get(project,"name"))||!same(get(written,"roots"),roots)){rpc.fail("schema","Codex did not confirm the requested project content.");return result;}put(native,utf8(get(written,"id")),written);
 }
 for(UInt i=0;i<count(deletedLegacyIds);++i){Obj id=get(mapping,utf8(at(deletedLegacyIds,i)));if(!id||!get(native,utf8(id)))continue;
  Obj members=projectServerMemberIds(rpc,id);if(!members)return result;
  for(UInt j=0;j<count(members);++j){Obj params=dict();put(params,"threadId",at(members,j));put(params,"projectId",str(""));if(!rpc.call("thread/metadata/update",params))return result;add(removed,at(members,j));}
  Obj params=dict();put(params,"projectId",id);if(!rpc.call("project/delete",params))return result;erase(native,utf8(id));
 }
 return result;
}
