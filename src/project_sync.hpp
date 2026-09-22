#pragma once
// In-memory reconciliation only: no files, processes, database or authentication access.
// Include after workspace_filter.hpp; uses the same Obj/Foundation-compatible helpers.
//
// Persist the complete state atomically, separately from every Codex profile. `records`
// retains tombstones; `profiles` retains the last observed snapshot and acknowledged
// record revisions. Never reset either just because a profile is stopped/unavailable.
// Observations are [{profileId: string, snapshot: RAW Desktop dictionary|null}].
// A complete, explicit local-projects dictionary (including {}) is required. Do not
// pass a lossy projection that has silently dropped malformed local entries.
//
// Before applying projectSyncSnapshot(), first observe the destination's current
// state. Write through a supported API, or only while the destination is stopped,
// preserving non-project state and checking that the input has not changed. Call
// projectSyncMarkApplied() ONLY after that exact snapshot was successfully applied.
// Bootstrap unions observed projects once. Later observations contribute deltas
// against their own baseline, so an offline profile cannot resurrect a deletion.

inline int projectSyncCompare(Obj a,Obj b){
 const unsigned char* x=(const unsigned char*)utf8(a),*y=(const unsigned char*)utf8(b);
 while(*x&&*x==*y){++x;++y;}return *x<*y?-1:*x>*y?1:0;
}
inline bool projectSyncProfileId(Obj id){
 if(!isClass(id,"NSString")||!send<UInt>(id,"length")||send<UInt>(id,"length")>512)return false;
 for(const char* p=utf8(id);*p;++p)if((unsigned char)*p<32||*p==127)return false;return true;
}
inline Obj projectSyncKeys(Obj d){return send<Obj>(d,"allKeys");}
inline Obj projectSyncSortedKeys(Obj d){
 Obj unsorted=projectSyncKeys(d),out=array();
 // Bounded insertion sort; avoid Foundation selector objects and SDK dependencies.
 for(UInt n=0;n<count(unsorted);++n){Obj key=at(unsorted,n);UInt index=0;
  while(index<count(out)&&projectSyncCompare(at(out,index),key)<0)++index;
  send<void>(out,"insertObject:atIndex:",key,index);
 }return out;
}
inline bool projectSyncRevision(Obj n,Int maximum){return isClass(n,"NSNumber")&&integer(n)>=0&&integer(n)<=maximum&&send<double>(n,"doubleValue")==static_cast<double>(integer(n));}
inline bool projectSyncFlag(Obj n){return projectSyncRevision(n,1);}
inline Obj projectSyncCleanSnapshot(Obj raw){
 if(!isClass(raw,"NSDictionary"))return nullptr;Obj source=get(raw,"local-projects");
 if(!isClass(source,"NSDictionary")||count(source)>4096)return nullptr;
 Obj registry=dict(),keys=projectSyncKeys(source);
 for(UInt i=0;i<count(keys);++i){Obj key=at(keys,i);
  if(!isClass(key,"NSString"))return nullptr;
  if(deck::prefix(utf8(key),"cloud:"))continue; // Account-specific entries are outside the shared registry.
  Obj entry=sanitizeLocalProject(key,get(source,utf8(key)));if(!entry)return nullptr;
  put(registry,utf8(key),entry);
 }
 Obj snapshot=dict(),order=array(),sourceOrder=get(raw,"project-order");
 if(isClass(sourceOrder,"NSArray")&&count(sourceOrder)<=8192)for(UInt i=0;i<count(sourceOrder);++i){Obj id=at(sourceOrder,i);
  if(isClass(id,"NSString")&&get(registry,utf8(id))&&!send<bool>(order,"containsObject:",id))add(order,id);
 }
 Obj sorted=projectSyncSortedKeys(registry);for(UInt i=0;i<count(sorted);++i)if(!send<bool>(order,"containsObject:",at(sorted,i)))add(order,at(sorted,i));
 put(snapshot,"local-projects",registry);put(snapshot,"project-order",order);return snapshot;
}
inline bool projectSyncSameRegistry(Obj a,Obj b){
 if(!isClass(a,"NSDictionary")||!isClass(b,"NSDictionary")||count(a)!=count(b))return false;
 Obj keys=projectSyncKeys(a);for(UInt i=0;i<count(keys);++i){const char* id=utf8(at(keys,i));if(!workspaceSameProject(get(a,id),get(b,id)))return false;}return true;
}
inline bool projectSyncStateValid(Obj state){
 if(!isClass(state,"NSDictionary"))return false;if(!count(state))return true;
 if(!projectSyncRevision(get(state,"version"),1)||integer(get(state,"version"))!=1||
    !projectSyncRevision(get(state,"revision"),9007199254740990L)||!projectSyncFlag(get(state,"initialized")))return false;
 Int revision=integer(get(state,"revision"));Obj records=get(state,"records"),profiles=get(state,"profiles"),order=get(state,"order");
 if(!isClass(records,"NSDictionary")||count(records)>20000||!isClass(profiles,"NSDictionary")||count(profiles)>256||!isClass(order,"NSArray"))return false;
 if(!truth(get(state,"initialized"))&&(revision||count(records)||count(profiles)||count(order)))return false;
 Obj ids=projectSyncKeys(records);for(UInt i=0;i<count(ids);++i){Obj id=at(ids,i),r=get(records,utf8(id));
  if(!isClass(r,"NSDictionary")||!sanitizeLocalProject(id,get(r,"project"))||!projectSyncFlag(get(r,"deleted"))||
     !projectSyncRevision(get(r,"revision"),revision)||integer(get(r,"revision"))<1||
     !projectSyncRevision(get(r,"baseRevision"),integer(get(r,"revision"))-1)||
     !projectSyncRevision(get(r,"generation"),integer(get(r,"revision")))||integer(get(r,"generation"))<1||!projectSyncProfileId(get(r,"writer")))return false;
 }
 Obj ordered=dict();UInt liveCount=0;for(UInt i=0;i<count(ids);++i)if(!truth(get(get(records,utf8(at(ids,i))),"deleted")))++liveCount;
 if(liveCount>4096||count(order)!=liveCount)return false;
 for(UInt i=0;i<count(order);++i){Obj id=at(order,i);if(!isClass(id,"NSString")||get(ordered,utf8(id)))return false;
  Obj record=get(records,utf8(id));if(!record||truth(get(record,"deleted")))return false;put(ordered,utf8(id),boolean(true));
 }
 Obj owners=projectSyncKeys(profiles);for(UInt i=0;i<count(owners);++i){Obj id=at(owners,i),p=get(profiles,utf8(id));
  if(!projectSyncProfileId(id)||!isClass(p,"NSDictionary")||!projectSyncCleanSnapshot(get(p,"observed"))||
     !projectSyncRevision(get(p,"appliedRevision"),revision))return false;
  if(get(p,"applied")&&!projectSyncCleanSnapshot(get(p,"applied")))return false;
  Obj seen=get(p,"seen");if(!isClass(seen,"NSDictionary")||count(seen)>20000)return false;
  Obj seenIds=projectSyncKeys(seen);for(UInt j=0;j<count(seenIds);++j){Obj sid=at(seenIds,j);
   if(!isClass(sid,"NSString")||!get(records,utf8(sid))||!projectSyncRevision(get(seen,utf8(sid)),integer(get(get(records,utf8(sid)),"revision"))))return false;
  }
 }
 return true;
}
inline void projectSyncInitialize(Obj state){
 if(count(state))return;put(state,"version",num(1));put(state,"revision",num(0));put(state,"initialized",boolean(false));
 put(state,"records",dict());put(state,"profiles",dict());put(state,"order",array());
}
inline void projectSyncConflict(Obj conflicts,Obj id,Obj profile,const char* reason){
 Obj conflict=dict();put(conflict,"projectId",id);put(conflict,"profileId",profile);put(conflict,"reason",str(reason));add(conflicts,conflict);
}
inline void projectSyncCandidate(Obj pending,Obj id,Obj project,Obj profile,Int base,bool deleted,bool restore){
 Obj list=get(pending,utf8(id));if(!list){list=array();put(pending,utf8(id),list);}
 Obj e=dict();put(e,"project",project);put(e,"writer",profile);put(e,"baseRevision",num(base));
 put(e,"deleted",boolean(deleted));put(e,"restore",boolean(restore));add(list,e);
}
inline bool projectSyncHigherRank(Obj a,Obj b){
 Int ar=integer(get(a,"baseRevision")),br=integer(get(b,"baseRevision"));
 return ar!=br?ar>br:projectSyncCompare(get(a,"writer"),get(b,"writer"))>0;
}
// Result: {ok,changed,canonicalChanged,conflicts:[...],ignored:[profileId,...]}.
// changed includes baseline/acknowledgement changes and therefore means "persist".
inline Obj projectSyncReconcile(Obj state,Obj observations){
 Obj result=dict(),conflicts=array(),ignored=array();put(result,"ok",boolean(false));put(result,"changed",boolean(false));
 put(result,"canonicalChanged",boolean(false));put(result,"conflicts",conflicts);put(result,"ignored",ignored);
 if(!projectSyncStateValid(state)||!isClass(observations,"NSArray")||count(observations)>256)return result;
 if(count(state)&&integer(get(state,"revision"))>=9007199254740990L)return result;
 Obj valid=dict(),duplicates=dict();
 for(UInt i=0;i<count(observations);++i){Obj observation=at(observations,i);
  if(!isClass(observation,"NSDictionary"))continue;Obj id=get(observation,"profileId");if(!projectSyncProfileId(id))continue;
  if(get(duplicates,utf8(id))){erase(valid,utf8(id));if(!send<bool>(ignored,"containsObject:",id))add(ignored,id);continue;}
  put(duplicates,utf8(id),boolean(true));Obj snapshot=projectSyncCleanSnapshot(get(observation,"snapshot"));
  if(snapshot)put(valid,utf8(id),snapshot);else add(ignored,id);
 }
 if(count(valid)){
  Obj potentialIds=dict(),potentialActive=dict(),potentialOwners=dict(),existingRecords=count(state)?get(state,"records"):dict(),existingProfiles=count(state)?get(state,"profiles"):dict();
  Obj keys=projectSyncKeys(existingRecords);for(UInt i=0;i<count(keys);++i){Obj id=at(keys,i);put(potentialIds,utf8(id),boolean(true));if(!truth(get(get(existingRecords,utf8(id)),"deleted")))put(potentialActive,utf8(id),boolean(true));}
  keys=projectSyncKeys(existingProfiles);for(UInt i=0;i<count(keys);++i)put(potentialOwners,utf8(at(keys,i)),boolean(true));
  keys=projectSyncKeys(valid);for(UInt i=0;i<count(keys);++i){Obj id=at(keys,i);put(potentialOwners,utf8(id),boolean(true));Obj entries=projectSyncKeys(get(get(valid,utf8(id)),"local-projects"));
   for(UInt j=0;j<count(entries);++j){put(potentialIds,utf8(at(entries,j)),boolean(true));put(potentialActive,utf8(at(entries,j)),boolean(true));}
  }
  if(count(potentialIds)>20000||count(potentialActive)>4096||count(potentialOwners)>256)return result;
 }
 put(result,"ok",boolean(true));if(!count(valid))return result;
 bool changed=!count(state);projectSyncInitialize(state);Obj records=get(state,"records"),profiles=get(state,"profiles");
 bool bootstrap=!truth(get(state,"initialized"));Obj pending=dict(),owners=projectSyncSortedKeys(valid);
 for(UInt i=0;i<count(owners);++i){Obj owner=at(owners,i),snapshot=get(valid,utf8(owner)),current=get(snapshot,"local-projects"),baseline=get(profiles,utf8(owner));
  Obj previous=baseline?get(get(baseline,"observed"),"local-projects"):nullptr,seen=baseline?get(baseline,"seen"):nullptr;
  Obj ids=projectSyncKeys(current);for(UInt j=0;j<count(ids);++j){Obj id=at(ids,j),project=get(current,utf8(id)),old=previous?get(previous,utf8(id)):nullptr,r=get(records,utf8(id));
   // Joining an existing group never overwrites known IDs or their tombstones.
   if(!baseline&&!bootstrap&&r)continue;
   if(old&&workspaceSameProject(old,project))continue;
   projectSyncCandidate(pending,id,project,owner,integer(seen?get(seen,utf8(id)):nullptr),false,baseline&&!old);
  }
  if(previous){ids=projectSyncKeys(previous);for(UInt j=0;j<count(ids);++j){Obj id=at(ids,j);if(!get(current,utf8(id)))
   projectSyncCandidate(pending,id,get(previous,utf8(id)),owner,integer(get(seen,utf8(id))),true,false);
  }}
 }
 Int nextRevision=integer(get(state,"revision"))+1;bool canonicalChanged=false;Obj ids=projectSyncSortedKeys(pending);
 for(UInt i=0;i<count(ids);++i){Obj id=at(ids,i),events=get(pending,utf8(id)),old=get(records,utf8(id)),winner=nullptr;
  for(UInt j=0;j<count(events);++j){Obj e=at(events,j);bool deleted=truth(get(e,"deleted"));Int base=integer(get(e,"baseRevision"));
   if(old){
    if(truth(get(old,"deleted"))){
     if(deleted)continue; // This deletion is already recorded, not a new competing operation.
     if(!truth(get(e,"restore"))||base<integer(get(old,"revision"))){projectSyncConflict(conflicts,id,get(e,"writer"),"stale-resurrection");continue;}
    }else if(base<integer(get(old,"generation"))){projectSyncConflict(conflicts,id,get(e,"writer"),"unacknowledged-generation");continue;}
   }
   if(!winner||(deleted&&!truth(get(winner,"deleted")))||
      (deleted==truth(get(winner,"deleted"))&&projectSyncHigherRank(e,winner)))winner=e;
  }
  if(!winner)continue;bool deleted=truth(get(winner,"deleted"));
  // A concurrent edit arriving in a later batch uses the same deterministic rank.
  // A deletion takes precedence over every edit to that observed generation.
  if(old&&!truth(get(old,"deleted"))&&!deleted&&integer(get(winner,"baseRevision"))<integer(get(old,"revision"))&&
     !projectSyncHigherRank(winner,old)){
   if(!workspaceSameProject(get(winner,"project"),get(old,"project")))projectSyncConflict(conflicts,id,get(winner,"writer"),"concurrent-edit");
   continue;
  }
  for(UInt j=0;j<count(events);++j){Obj e=at(events,j);if(e!=winner&&
   (truth(get(e,"deleted"))!=deleted||!workspaceSameProject(get(e,"project"),get(winner,"project"))))
    projectSyncConflict(conflicts,id,get(e,"writer"),deleted?"delete-wins":"concurrent-edit");
  }
  if(old&&truth(get(old,"deleted"))==deleted&&workspaceSameProject(get(old,"project"),get(winner,"project")))continue;
  Obj record=dict();put(record,"project",deleted&&old?get(old,"project"):get(winner,"project"));put(record,"deleted",boolean(deleted));
  put(record,"revision",num(nextRevision));put(record,"baseRevision",get(winner,"baseRevision"));put(record,"writer",get(winner,"writer"));
  put(record,"generation",num(!old||(!deleted&&truth(get(old,"deleted")))?nextRevision:integer(get(old,"generation"))));
  put(records,utf8(id),record);canonicalChanged=true;
 }
 if(canonicalChanged){put(state,"revision",num(nextRevision));changed=true;}
 if(bootstrap){put(state,"initialized",boolean(true));put(state,"order",get(get(valid,utf8(at(owners,0))),"project-order"));changed=true;}
 // Preserve established ordering; appending new IDs is deterministic. Reorder-only
 // sidebar changes are deliberately not inferred as project-content changes.
 Obj order=array(),oldOrder=get(state,"order"),live=dict();ids=projectSyncKeys(records);
 for(UInt i=0;i<count(ids);++i){Obj id=at(ids,i);if(!truth(get(get(records,utf8(id)),"deleted")))put(live,utf8(id),boolean(true));}
 for(UInt i=0;i<count(oldOrder);++i){Obj id=at(oldOrder,i);if(isClass(id,"NSString")&&get(live,utf8(id))&&!send<bool>(order,"containsObject:",id))add(order,id);}
 ids=projectSyncSortedKeys(live);for(UInt i=0;i<count(ids);++i)if(!send<bool>(order,"containsObject:",at(ids,i)))add(order,at(ids,i));
 if(!workspaceSamePaths(order,oldOrder)){put(state,"order",order);changed=true;}
 for(UInt i=0;i<count(owners);++i){Obj owner=at(owners,i),snapshot=get(valid,utf8(owner)),p=get(profiles,utf8(owner));
  if(!p){p=dict();put(p,"seen",dict());put(p,"appliedRevision",num(0));put(profiles,utf8(owner),p);changed=true;}
  if(!get(p,"observed")||!projectSyncSameRegistry(get(get(p,"observed"),"local-projects"),get(snapshot,"local-projects"))){put(p,"observed",snapshot);changed=true;}
  Obj seen=get(p,"seen"),current=get(snapshot,"local-projects");ids=projectSyncKeys(records);
  for(UInt j=0;j<count(ids);++j){Obj id=at(ids,j),record=get(records,utf8(id)),present=get(current,utf8(id));
   // An observed absence acknowledges a tombstone. A later transition from that
   // absence to a present entry is an explicit re-add, not a stale resurrection.
   bool matches=truth(get(record,"deleted"))?!present:present&&workspaceSameProject(present,get(record,"project"));
   if(matches&&integer(get(seen,utf8(id)))!=integer(get(record,"revision"))){put(seen,utf8(id),get(record,"revision"));changed=true;}
  }
 }
 put(result,"changed",boolean(changed));put(result,"canonicalChanged",boolean(canonicalChanged));return result;
}
inline Obj projectSyncSnapshot(Obj state){
 if(!projectSyncStateValid(state)||!count(state)||!truth(get(state,"initialized")))return nullptr;
 Obj result=dict(),registry=dict(),order=array(),roots=array(),labels=dict(),records=get(state,"records");
 Obj ids=get(state,"order");for(UInt i=0;i<count(ids);++i){Obj id=at(ids,i),r=get(records,utf8(id));if(!r||truth(get(r,"deleted")))continue;
  Obj project=sanitizeLocalProject(id,get(r,"project"));put(registry,utf8(id),project);add(order,id);
  Obj paths=get(project,"rootPaths");for(UInt j=0;j<count(paths);++j){Obj path=at(paths,j);if(!send<bool>(roots,"containsObject:",path))add(roots,path);put(labels,utf8(path),get(project,"name"));}
 }
 put(result,"local-projects",registry);put(result,"project-order",order);put(result,"electron-saved-workspace-roots",roots);put(result,"electron-workspace-root-labels",labels);return result;
}
inline bool projectSyncMarkApplied(Obj state,Obj profileId,Obj snapshot){
 if(!projectSyncProfileId(profileId))return false;Obj canonical=projectSyncSnapshot(state),clean=projectSyncCleanSnapshot(snapshot);
 if(!canonical||!clean||!projectSyncSameRegistry(get(canonical,"local-projects"),get(clean,"local-projects")))return false;
 Obj profiles=get(state,"profiles"),p=get(profiles,utf8(profileId));if(!p){if(count(profiles)>=256)return false;p=dict();put(profiles,utf8(profileId),p);}
 Obj seen=dict(),records=get(state,"records"),ids=projectSyncKeys(records);for(UInt i=0;i<count(ids);++i){Obj id=at(ids,i);put(seen,utf8(id),get(get(records,utf8(id)),"revision"));}
 put(p,"seen",seen);put(p,"observed",clean);put(p,"applied",projectSyncCleanSnapshot(clean));put(p,"appliedRevision",get(state,"revision"));return true;
}
