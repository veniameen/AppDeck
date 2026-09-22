// Pure dictionary projection; tested with an in-memory Foundation-compatible test adapter.
// Codex Desktop keeps its sidebar project list in .codex-global-state.json. Older builds used
// `electron-saved-workspace-roots`; current builds use the `local-projects` registry
// (id -> {id,name,rootPaths,createdAt,updatedAt}) ordered by `project-order`. These keys are not a
// public API: every value is shape-checked and anything unknown is dropped.
// Never projected: cookies, account keys, thread mappings, messages, remote hosts, UI atom stores,
// migration bookkeeping, and `cloud:`/remote projects, which belong to one signed-in account.
Obj sanitizeLocalProject(Obj key,Obj entry){
 if(!isClass(key,"NSString")||!deck::projectId(utf8(key))||!isClass(entry,"NSDictionary"))return nullptr;
 Obj id=get(entry,"id"),name=get(entry,"name"),paths=get(entry,"rootPaths");
 if(!isClass(id,"NSString")||!deck::equal(utf8(id),utf8(key)))return nullptr;
 if(!isClass(name,"NSString")||!send<UInt>(name,"length")||send<UInt>(name,"length")>256)return nullptr;
 if(!isClass(paths,"NSArray")||!count(paths)||count(paths)>16)return nullptr;
 Obj cleanPaths=array();
 for(UInt i=0;i<count(paths);++i){Obj p=at(paths,i);if(!isClass(p,"NSString")||send<UInt>(p,"length")>4096||!deck::absoluteLocalPath(utf8(p)))return nullptr;add(cleanPaths,p);}
 Obj clean=dict();put(clean,"id",id);put(clean,"name",name);put(clean,"rootPaths",cleanPaths);
 for(const char* stamp:{"createdAt","updatedAt"}){Obj n=get(entry,stamp);if(isClass(n,"NSNumber"))put(clean,stamp,n);}
 return clean;
}
Obj sanitizeWorkspaceSnapshot(Obj d){
 if(!isClass(d,"NSDictionary"))return nullptr;
 Obj roots=get(d,"electron-saved-workspace-roots"),cleanRoots=array();
 if(roots&&(!isClass(roots,"NSArray")||count(roots)>4096))return nullptr;
 for(UInt i=0;i<count(roots);++i){Obj p=at(roots,i);if(!isClass(p,"NSString")||send<UInt>(p,"length")>4096||!deck::absoluteLocalPath(utf8(p)))continue;
  if(!send<bool>(cleanRoots,"containsObject:",p))add(cleanRoots,p);
 }
 Obj projects=get(d,"local-projects"),cleanProjects=dict(),projectIds=array();
 if(projects&&!isClass(projects,"NSDictionary"))return nullptr;
 if(projects){Obj keys=send<Obj>(projects,"allKeys");if(count(keys)>4096)return nullptr;
  for(UInt i=0;i<count(keys);++i){Obj key=at(keys,i),clean=sanitizeLocalProject(key,get(projects,utf8(key)));if(clean){put(cleanProjects,utf8(key),clean);add(projectIds,key);}}
 }
 if(!count(cleanRoots)&&!count(projectIds))return nullptr; // unknown schema: fail closed
 Obj result=dict();put(result,"electron-saved-workspace-roots",cleanRoots);
 Obj labels=get(d,"electron-workspace-root-labels"),cleanLabels=dict();
 if(isClass(labels,"NSDictionary"))for(UInt i=0;i<count(cleanRoots);++i){Obj p=at(cleanRoots,i),l=get(labels,utf8(p));
  if(isClass(l,"NSString")&&send<UInt>(l,"length")<=256)put(cleanLabels,utf8(p),l);
 }
 put(result,"electron-workspace-root-labels",cleanLabels);
 if(count(projectIds))put(result,"local-projects",cleanProjects);
 // Order entries are project ids (current builds) or root paths (older builds). Anything else,
 // including account-bound `cloud:` ids, is dropped; known items missing from the order are appended.
 Obj order=get(d,"project-order"),cleanOrder=array();
 if(isClass(order,"NSArray")&&count(order)<=8192)for(UInt i=0;i<count(order);++i){Obj p=at(order,i);if(!isClass(p,"NSString"))continue;
  bool known=send<bool>(projectIds,"containsObject:",p)||send<bool>(cleanRoots,"containsObject:",p);
  if(known&&!send<bool>(cleanOrder,"containsObject:",p))add(cleanOrder,p);
 }
 for(UInt i=0;i<count(projectIds);++i)if(!send<bool>(cleanOrder,"containsObject:",at(projectIds,i)))add(cleanOrder,at(projectIds,i));
 // Path entries are only meaningful to builds without a registry.
 if(!count(projectIds))for(UInt i=0;i<count(cleanRoots);++i)if(!send<bool>(cleanOrder,"containsObject:",at(cleanRoots,i)))add(cleanOrder,at(cleanRoots,i));
 put(result,"project-order",cleanOrder);return result;
}
bool workspaceSamePaths(Obj a,Obj b){
 if(!isClass(a,"NSArray")||!isClass(b,"NSArray")||count(a)!=count(b))return false;
 for(UInt i=0;i<count(a);++i)if(!isClass(at(a,i),"NSString")||!isClass(at(b,i),"NSString")||!deck::equal(utf8(at(a,i)),utf8(at(b,i))))return false;
 return true;
}
bool workspaceSameProject(Obj old,Obj entry){
 if(!isClass(old,"NSDictionary")||!isClass(get(old,"name"),"NSString"))return false;
 return deck::equal(utf8(get(old,"name")),utf8(get(entry,"name")))&&workspaceSamePaths(get(old,"rootPaths"),get(entry,"rootPaths"));
}
// Base registry -> a copy's existing state. Union, never deletion: projects that exist only in the
// copy stay (after the base's order). Returns true when `target` was changed.
bool mergeWorkspaceSnapshot(Obj target,Obj snapshot){
 if(!isClass(target,"NSDictionary")||!isClass(snapshot,"NSDictionary"))return false;bool changed=false;
 Obj theirs=get(snapshot,"local-projects");
 if(isClass(theirs,"NSDictionary")){Obj mine=get(target,"local-projects");if(!isClass(mine,"NSDictionary")){mine=dict();put(target,"local-projects",mine);changed=true;}
  Obj keys=send<Obj>(theirs,"allKeys");for(UInt i=0;i<count(keys);++i){Obj key=at(keys,i),entry=get(theirs,utf8(key)),old=get(mine,utf8(key));
   if(!workspaceSameProject(old,entry)){put(mine,utf8(key),entry);changed=true;}}
 }
 Obj order=get(snapshot,"project-order");
 if(isClass(order,"NSArray")){Obj current=get(target,"project-order"),merged=array();
  for(UInt i=0;i<count(order);++i)add(merged,at(order,i));
  if(isClass(current,"NSArray"))for(UInt i=0;i<count(current);++i){Obj p=at(current,i);if(isClass(p,"NSString")&&!send<bool>(merged,"containsObject:",p))add(merged,p);}
  bool sameOrder=isClass(current,"NSArray")&&count(current)==count(merged);
  for(UInt i=0;sameOrder&&i<count(merged);++i)sameOrder=isClass(at(current,i),"NSString")&&deck::equal(utf8(at(current,i)),utf8(at(merged,i)));
  if(!sameOrder){put(target,"project-order",merged);changed=true;}
 }
 Obj roots=get(snapshot,"electron-saved-workspace-roots");
 if(isClass(roots,"NSArray")&&count(roots)){Obj current=get(target,"electron-saved-workspace-roots"),merged=array();
  for(UInt i=0;i<count(roots);++i)add(merged,at(roots,i));
  if(isClass(current,"NSArray"))for(UInt i=0;i<count(current);++i){Obj p=at(current,i);if(isClass(p,"NSString")&&!send<bool>(merged,"containsObject:",p))add(merged,p);}
  if(!isClass(current,"NSArray")||count(current)!=count(merged)){put(target,"electron-saved-workspace-roots",merged);changed=true;}
 }
 return changed;
}
