// Group-level synchronization. Only allowlisted project fields and automation definitions
// cross account boundaries. The durable journal belongs to AppDeck, never to Codex's databases.
// Included after usage_limits.hpp and the pure synchronization engines.
bool groupSyncBusy=false;
double groupSyncNext=0;
bool groupSyncEnabled(Obj a){return a&&deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Codex&&baseSource(a)&&truth(get(a,"sharedHistory"));}
Obj groupSyncPath(Obj a){return join(sharedRoot(a),"group-sync-v1.json");}
Obj groupSyncLoad(Obj a){
 Obj path=groupSyncPath(a);if(!exists(path))return dict();Obj d=readJSON(path);
 if(!d||integer(get(d,"version"))!=1||!projectSyncStateValid(get(d,"projects"))||!automationSyncStateValid(get(d,"automations")))return nullptr;
 return d;
}
bool groupSyncSave(Obj a,Obj journal){
 put(journal,"version",num(1));Obj error=nullptr,bytes=send(cls("NSJSONSerialization"),"dataWithJSONObject:options:error:",journal,(UInt)0,&error);
 if(!bytes||send<UInt>(bytes,"length")>8*1024*1024)return false;
 return mkdirPrivate(sharedRoot(a))&&writeJSON(groupSyncPath(a),journal,0);
}
// Treat every untracked original instance as running, including an ambiguous pair.
bool groupBaseRunning(Obj a){Obj all=send(workspace,"runningApplications");for(UInt i=0;i<count(all);++i){Obj r=at(all,i);if(!send<bool>(r,"isTerminated")&&sameApp(r,a)&&!managedProcess(r))return true;}return false;}
Obj groupSyncParticipants(Obj a){
 Obj result=array(),base=dict();put(base,"profileId",str("base"));put(base,"home",canonical(baseSource(a)));put(base,"name",str(T("Primary","Основной")));put(base,"running",boolean(groupBaseRunning(a)));add(result,base);
 for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);if(appFor(p)!=a)continue;
  if(isMaster(p)){put(base,"profile",p);put(base,"name",get(p,"name"));continue;}
  if(!truth(get(p,"share")))continue;Obj entry=dict();put(entry,"profileId",get(p,"id"));put(entry,"home",join(profileRoot(p),"codex"));put(entry,"name",get(p,"name"));put(entry,"profile",p);put(entry,"running",boolean(running(p)||ownedProcess(p)));add(result,entry);
 }
 return result;
}
bool groupParticipantRunning(Obj a,Obj entry){return same(get(entry,"profileId"),str("base"))?groupBaseRunning(a):running(get(entry,"profile"))||ownedProcess(get(entry,"profile"));}
Obj groupClone(Obj object){Obj error=nullptr;Obj data=send(cls("NSJSONSerialization"),"dataWithJSONObject:options:error:",object,(UInt)0,&error);if(!data)return nullptr;return send(cls("NSJSONSerialization"),"JSONObjectWithData:options:error:",data,(UInt)1,&error);}
Obj groupProjectHost(Obj entry){return cat(str("local:"),canonical(get(entry,"home")));}
Obj groupProjectMapping(Obj current,Obj entry){Obj all=get(current,"app-server-project-id-by-legacy-project-id-by-host");Obj map=isClass(all,"NSDictionary")?get(all,utf8(groupProjectHost(entry))):nullptr;return isClass(map,"NSDictionary")?map:dict();}
Obj groupProjectDeleted(Obj projectState){Obj ids=array(),records=get(projectState,"records"),keys=send(records,"allKeys");for(UInt i=0;i<count(keys);++i)if(truth(get(get(records,utf8(at(keys,i))),"deleted")))add(ids,at(keys,i));return ids;}
bool groupContains(Obj list,Obj value){return isClass(list,"NSArray")&&send<bool>(list,"containsObject:",value);}
// Preserve unrelated window/account data. Project membership cleanup follows Codex's own
// remove-project behavior: keep tasks, make removed memberships projectless.
Obj groupProjectProjection(Obj current,Obj snapshot,Obj deleted){
 Obj out=groupClone(current);if(!out)return nullptr;Obj registry=dict(),old=get(current,"local-projects"),keys=send(old,"allKeys");
 for(UInt i=0;i<count(keys);++i){Obj id=at(keys,i);if(!deck::projectId(utf8(id)))put(registry,utf8(id),get(old,utf8(id)));}
 Obj local=get(snapshot,"local-projects");keys=send(local,"allKeys");for(UInt i=0;i<count(keys);++i)put(registry,utf8(at(keys,i)),get(local,utf8(at(keys,i))));put(out,"local-projects",registry);
 Obj order=array(),desired=get(snapshot,"project-order"),prior=get(current,"project-order");for(UInt i=0;i<count(desired);++i)add(order,at(desired,i));
 for(UInt i=0;i<count(prior);++i){Obj id=at(prior,i);if(isClass(id,"NSString")&&!deck::projectId(utf8(id))&&get(registry,utf8(id))&&!groupContains(order,id))add(order,id);}put(out,"project-order",order);
 Obj roots=array(),labels=dict();keys=send(local,"allKeys");for(UInt i=0;i<count(keys);++i){Obj entry=get(local,utf8(at(keys,i))),paths=get(entry,"rootPaths");for(UInt j=0;j<count(paths);++j){Obj path=at(paths,j);if(!groupContains(roots,path))add(roots,path);put(labels,utf8(path),get(entry,"name"));}}
 put(out,"electron-saved-workspace-roots",roots);put(out,"electron-workspace-root-labels",labels);
 Obj pinned=get(out,"pinned-project-ids");if(isClass(pinned,"NSArray"))for(UInt i=count(pinned);i>0;--i)if(groupContains(deleted,at(pinned,i-1)))send<void>(pinned,"removeObjectAtIndex:",i-1);
 Obj selectedProject=get(out,"selected-project");if(isClass(selectedProject,"NSDictionary")&&same(get(selectedProject,"type"),str("local"))&&groupContains(deleted,get(selectedProject,"projectId")))erase(out,"selected-project");
 Obj assignments=get(out,"thread-project-assignments"),projectless=get(out,"projectless-thread-ids");if(!isClass(projectless,"NSArray"))projectless=array();
 if(isClass(assignments,"NSDictionary")){keys=send(assignments,"allKeys");for(UInt i=0;i<count(keys);++i){Obj thread=at(keys,i),assignment=get(assignments,utf8(thread));if(isClass(assignment,"NSDictionary")&&same(get(assignment,"projectKind"),str("local"))&&groupContains(deleted,get(assignment,"projectId"))){erase(assignments,utf8(thread));if(!groupContains(projectless,thread))add(projectless,thread);}}}
 if(count(projectless)||get(out,"projectless-thread-ids"))put(out,"projectless-thread-ids",projectless);
 return out;
}
bool groupWriteBackup(Obj path){
 if(!exists(path))return true;if(symlink(path))return false;Obj backup=cat(path,str(".appdeck-previous"));if(symlink(backup))return false;
 Obj data=send(cls("NSData"),"dataWithContentsOfFile:",path);if(!data||!send<bool>(data,"writeToFile:atomically:",backup,true))return false;chmod(utf8(backup),0600);return true;
}
Obj groupProjectRequest(Obj a,Obj entry){
 Obj binary=usageBinary(a),scratch=usageScratch();if(!binary||!scratch)return nullptr;Obj request=dict(),env=cleanEnvironment();
 put(env,"CODEX_HOME",canonical(get(entry,"home")));put(env,"CODEX_SQLITE_HOME",canonical(baseSource(a)));put(env,"CODEX_INTERNAL_APP_SERVER_REMOTE_CONTROL_DISABLED",str("1"));
 put(request,"binary",binary);put(request,"scratch",scratch);put(request,"environment",env);return request;
}
Obj groupReadAutomations(Obj entry){
 Obj observation=dict(),files=dict(),folder=join(get(entry,"home"),"automations");put(observation,"profileId",get(entry,"profileId"));put(observation,"stopped",boolean(!truth(get(entry,"running"))));put(observation,"files",files);
 if(!exists(folder)){put(observation,"complete",boolean(directory(get(entry,"home"))&&!symlink(get(entry,"home"))));return observation;}
 if(symlink(folder)||!directory(folder)){put(observation,"complete",boolean(false));return observation;}
 Obj error=nullptr,names=send(fileManager,"contentsOfDirectoryAtPath:error:",folder,&error);bool complete=names&&!error&&count(names)<=4096;
 for(UInt i=0;complete&&i<count(names);++i){Obj id=at(names,i);if(!automationId(utf8(id)))continue;Obj dir=join(folder,id),file=join(dir,"automation.toml");if(!exists(file))continue;
  Obj text=symlink(dir)||!same(get(attrs(file),"NSFileType"),str("NSFileTypeRegular"))?nullptr:readText(file);if(!text){complete=false;break;}put(files,utf8(id),text);
 }
 put(observation,"complete",boolean(complete));return observation;
}
void groupSyncBookmarks(Obj a,Obj snapshot){
 // The old imported list is replaced by the group projection; explicit unrelated bookmarks stay.
 for(UInt i=count(projects);i>0;--i){Obj p=at(projects,i-1);if(same(get(p,"origin"),str("codex-base"))||same(get(p,"syncAppId"),get(a,"id")))send<void>(projects,"removeObjectAtIndex:",i-1);}
 Obj registry=get(snapshot,"local-projects"),order=get(snapshot,"project-order");for(UInt i=0;i<count(order);++i){Obj entry=get(registry,utf8(at(order,i))),paths=get(entry,"rootPaths");if(!count(paths))continue;
  Obj p=dict();put(p,"name",get(entry,"name"));put(p,"path",at(paths,0));put(p,"origin",str("codex-group"));put(p,"syncAppId",get(a,"id"));put(p,"syncProjectId",get(entry,"id"));add(projects,p);
 }
}
// A mapped native ID missing from a successful project/list is stronger evidence than an
// unchanged stale Desktop cache. Record that confirmed deletion before retrying cache apply.
bool groupAcceptNativeDeletes(Obj projectState,Obj mapping,Obj missing){
 if(!isClass(missing,"NSArray")||!count(missing))return false;Obj snapshot=projectSyncSnapshot(projectState);if(!snapshot)return false;
 Obj registry=get(snapshot,"local-projects");for(UInt i=0;i<count(missing);++i){Obj id=at(missing,i);if(!isClass(id,"NSString")||!get(mapping,utf8(id))||!get(registry,utf8(id)))return false;}
 if(!projectSyncMarkApplied(projectState,str("codex-server"),snapshot))return false;
 for(UInt i=0;i<count(missing);++i)erase(registry,utf8(at(missing,i)));
 Obj observations=array(),entry=dict();put(entry,"profileId",str("codex-server"));put(entry,"snapshot",snapshot);add(observations,entry);
 Obj result=projectSyncReconcile(projectState,observations);return truth(get(result,"ok"))&&truth(get(result,"canonicalChanged"));
}
Obj syncGroupPass(Obj a,bool apply){
 if(previewMode||groupSyncBusy||!groupSyncEnabled(a))return nullptr;groupSyncBusy=true;
 struct Reset{~Reset(){groupSyncBusy=false;}} reset;
 Obj journal=groupSyncLoad(a);if(!journal)return str(T("The shared sync journal is damaged or has an unknown version. Profiles are unchanged.","Общий реестр синхронизации повреждён или имеет неизвестную версию. Профили не изменены."));
 Obj previous=groupClone(journal),ps=get(journal,"projects"),as=get(journal,"automations");if(!ps){ps=dict();put(journal,"projects",ps);}
 Obj members=groupSyncParticipants(a),observations=array(),automationObservations=array();bool unreadable=false;
 for(UInt i=0;i<count(members);++i){Obj entry=at(members,i),path=join(get(entry,"home"),".codex-global-state.json"),current=readJSON(path);
  bool established=get(get(ps,"profiles"),utf8(get(entry,"profileId")))!=nullptr;
  if(!current&&!exists(path)&&directory(get(entry,"home"))&&!established){current=dict();put(current,"local-projects",dict());put(current,"project-order",array());}
  if(current)put(entry,"raw",current);else if(exists(path)||established)unreadable=true;
  Obj observation=dict();put(observation,"profileId",get(entry,"profileId"));put(observation,"snapshot",current);add(observations,observation);
  Obj automationObservation=groupReadAutomations(entry),automationBaseline=get(get(as,"profiles"),utf8(get(entry,"profileId")));
  if(count(automationBaseline)&&!exists(join(get(entry,"home"),"automations")))put(automationObservation,"complete",boolean(false));
  if(!truth(get(automationObservation,"complete"))&&directory(get(entry,"home")))unreadable=true;
  add(automationObservations,automationObservation);
 }
 Obj reconciled=projectSyncReconcile(ps,observations);if(!truth(get(reconciled,"ok")))return str(T("Project reconciliation rejected: check the version and size of the shared journal.","Согласование проектов отклонено: проверьте версию и размер общего реестра."));
 Obj automationPlan=automationSyncReconcile(as,automationObservations);
 Obj nextAutomation=get(automationPlan,"state");if(nextAutomation){as=nextAutomation;put(journal,"automations",as);}else return str(T("Could not check the automation definitions.","Не удалось проверить определения автоматизаций."));
 Obj snapshot=projectSyncSnapshot(ps);if(!snapshot)return str(T("Project list not recognized. Sync stopped without replacing Codex data.","Список проектов не распознан. Синхронизация остановлена без замены данных Codex."));
 Obj sharedMapping=get(journal,"serverMapping");if(!isClass(sharedMapping,"NSDictionary")){sharedMapping=dict();put(journal,"serverMapping",sharedMapping);}
 // The native registry is shared. Retain mappings learned in any group profile, including
 // deleted projects whose only known mapping belonged to the profile that removed them.
 for(UInt i=0;i<count(members);++i){Obj entry=at(members,i),mapping=groupProjectMapping(get(entry,"raw"),entry),keys=send(mapping,"allKeys");
  for(UInt j=0;j<count(keys);++j){Obj key=at(keys,j),native=get(mapping,utf8(key));if(!get(get(ps,"records"),utf8(key)))continue;
   if(!isClass(native,"NSString"))return str(T("Codex contains an unrecognized project-to-server mapping.","Codex содержит нераспознанную связь проекта с сервером."));Obj prior=get(sharedMapping,utf8(key));
   if(prior&&!same(prior,native))return str(T("Profiles map one project to different Codex records. Sync stopped to protect tasks.","Профили связывают один проект с разными записями Codex. Синхронизация остановлена для сохранения задач."));put(sharedMapping,utf8(key),native);
  }
 }
 if(!same(previous,journal)&&!groupSyncSave(a,journal))return str(T("Could not save the shared journal. Profiles are unchanged.","Не удалось сохранить общий реестр. Профили не изменены."));
 Obj deleted=groupProjectDeleted(ps),serverRevisions=get(journal,"serverRevisions");if(!isClass(serverRevisions,"NSDictionary")){serverRevisions=dict();put(journal,"serverRevisions",serverRevisions);}
 Int revision=integer(get(ps,"revision")),pending=0;Obj pendingProfiles=dict(),failure=nullptr;bool nativeValidated=false;
 for(UInt i=0;i<count(members);++i){Obj entry=at(members,i),id=get(entry,"profileId"),current=get(entry,"raw"),profile=get(entry,"profile");if(!current)continue;
  // A malformed local entry makes an observation unusable as a whole; never overwrite it.
  if(!projectSyncCleanSnapshot(current)){unreadable=true;continue;}
  Obj projected=groupProjectProjection(current,snapshot,deleted);bool needed=!same(current,projected)||integer(get(serverRevisions,utf8(id)))!=revision||
   (apply&&!nativeValidated&&!groupParticipantRunning(a,entry)&&directory(get(entry,"home")));
  if(!needed){if(profile)erase(profile,"syncPending");continue;}
  if(profile)put(profile,"syncPending",boolean(true));put(pendingProfiles,utf8(id),boolean(true));++pending;
  if(!apply||groupParticipantRunning(a,entry)||!directory(get(entry,"home")))continue;
  Obj path=join(get(entry,"home"),".codex-global-state.json");if(!groupWriteBackup(path)){failure=str(T("Could not save the previous project list.","Не удалось сохранить предыдущий список проектов."));continue;}
  Obj request=groupProjectRequest(a,entry);if(!request){failure=str(T("No compatible app-server for project sync was found in the app.","В приложении не найден совместимый app-server для синхронизации проектов."));continue;}
  Obj reply=syncProjectsWithAppServer(request,get(snapshot,"local-projects"),sharedMapping,deleted);
  if(isClass(get(reply,"mapping"),"NSDictionary")){send<void>(sharedMapping,"addEntriesFromDictionary:",get(reply,"mapping"));if(!groupSyncSave(a,journal)){failure=str(T("Could not save the project mappings with Codex.","Не удалось сохранить связи проектов с Codex."));break;}}
  if(get(reply,"error")){
   if(same(get(reply,"error"),str("missing-project"))&&groupAcceptNativeDeletes(ps,sharedMapping,get(reply,"missingLegacyIds"))){
    if(!groupSyncSave(a,journal))return str(T("Could not save the deletions confirmed by Codex.","Не удалось сохранить подтверждённые удаления из Codex."));put(a,"syncNeedsRetry",boolean(true));note("Confirmed native project deletions recorded; stale Desktop caches will be reconciled.");return nullptr;
   }
   failure=cat(str(T("Codex did not finish syncing projects: ","Codex не завершил синхронизацию проектов: ")),get(reply,"error"));continue;
  }
  nativeValidated=true;
  if(count(get(reply,"unresolvedDeletedIds"))){failure=str(T("The removal waits for a match in the Codex registry: no saved project identifier was found.","Удаление ожидает сопоставления с реестром Codex: не найден сохранённый идентификатор проекта."));continue;}
  // A desktop can be started independently during the RPC. Never overwrite its live state.
  Obj latest=readJSON(path);if(groupParticipantRunning(a,entry)||(exists(path)&&!same(latest,current))){failure=str(T("The profile changed during sync. Try again after closing it.","Профиль изменился во время синхронизации. Повторите после его закрытия."));continue;}
  Obj mappings=get(projected,"app-server-project-id-by-legacy-project-id-by-host");if(!isClass(mappings,"NSDictionary")){mappings=dict();put(projected,"app-server-project-id-by-legacy-project-id-by-host",mappings);}put(mappings,utf8(groupProjectHost(entry)),get(reply,"mapping"));
  if(!same(current,projected)&&!writeJSON(path,projected,0)){failure=str(T("Could not write the reconciled list. The previous version is kept next to it.","Не удалось записать согласованный список. Предыдущая версия сохранена рядом."));continue;}
  projectSyncMarkApplied(ps,id,snapshot);put(serverRevisions,utf8(id),num(revision));if(profile)erase(profile,"syncPending");erase(pendingProfiles,utf8(id));--pending;
  // Each acknowledgement is durable. A later failed profile cannot roll back completed work.
  if(!groupSyncSave(a,journal)){failure=str(T("Could not save the sync result; retrying is safe.","Не удалось сохранить результат синхронизации; повтор безопасен."));break;}
 }
 Obj writes=get(automationPlan,"writes");
 for(UInt i=0;i<count(writes);++i){Obj item=at(writes,i),entry=nullptr;for(UInt j=0;j<count(members);++j)if(same(get(at(members,j),"profileId"),get(item,"profileId"))){entry=at(members,j);break;}
  if(!entry)continue;if(get(entry,"profile"))put(get(entry,"profile"),"syncPending",boolean(true));
  if(!apply||groupParticipantRunning(a,entry))continue;
  Obj id=get(item,"id");if(!isClass(id,"NSString")||!automationId(utf8(id))){failure=str(T("Invalid automation identifier.","Некорректный идентификатор автоматизации."));continue;}
  Obj folder=join(get(entry,"home"),"automations"),dir=join(folder,id),file=join(dir,"automation.toml");
  if(!directory(get(entry,"home"))||symlink(folder)||symlink(dir)||symlink(file)){failure=str(T("The automation path is unavailable or a symbolic link.","Путь автоматизации недоступен или является символической ссылкой."));continue;}
  Obj observed=nullptr;for(UInt j=0;j<count(automationObservations);++j)if(same(get(at(automationObservations,j),"profileId"),get(item,"profileId")))observed=get(get(at(automationObservations,j),"files"),utf8(id));
  if(!same(observed,readText(file))){failure=str(T("The automation changed during sync. It will be checked again.","Автоматизация изменилась во время синхронизации. Она будет проверена повторно."));continue;}
  Obj definition=get(item,"text")?automationDefinition(get(item,"text"),id):nullptr;
  if(definition&&same(get(definition,"status"),str("ACTIVE"))){bool safe=true;
   // Ownership changes pause every other copy first. A failed pause must never leave two
   // ACTIVE files, even when the new owner sorts before the old owner in the profile list.
   for(UInt j=0;j<count(members);++j){Obj other=at(members,j);if(same(get(other,"profileId"),get(entry,"profileId")))continue;
    if(!exists(get(other,"home"))&&!get(get(as,"profiles"),utf8(get(other,"profileId"))))continue;
    Obj view=groupReadAutomations(other);if(!truth(get(view,"complete"))){safe=false;break;}Obj text=get(get(view,"files"),utf8(id));if(!text)continue;Obj parsed=automationDefinition(text,id);
    if(!parsed||same(get(parsed,"status"),str("ACTIVE"))){safe=false;break;}
   }
   if(!safe){failure=str(T("Owner not activated: another copy of the schedule is still active or unavailable.","Исполнитель не активирован: другая копия расписания ещё активна или недоступна."));continue;}
  }
  if(!groupWriteBackup(file)){failure=str(T("Could not save the previous automation.","Не удалось сохранить предыдущую автоматизацию."));continue;}
  bool ok=false;if(truth(get(item,"remove"))){Obj error=nullptr;ok=!exists(file)||send<bool>(fileManager,"removeItemAtPath:error:",file,&error);}
  else ok=mkdirPrivate(folder)&&mkdirPrivate(dir)&&writeText(file,get(item,"text"));
  if(!ok){failure=str(T("Could not transfer the automation definition.","Не удалось перенести определение автоматизации."));continue;}
  automationSyncMarkApplied(as,get(item,"profileId"),id,truth(get(item,"remove"))?nullptr:get(item,"text"));
  if(!groupSyncSave(a,journal)){failure=str(T("Could not save the result of the automation transfer.","Не удалось сохранить результат переноса автоматизаций."));break;}
 }
 // Re-read after writes: stopped duplicate ACTIVE copies may just have been paused. Launch
 // admission must use the remaining on-disk risk rather than the pre-apply observation.
 Obj after=array();for(UInt i=0;i<count(members);++i){Obj entry=at(members,i);put(entry,"running",boolean(groupParticipantRunning(a,entry)));Obj observation=groupReadAutomations(entry);
  if(count(get(get(as,"profiles"),utf8(get(entry,"profileId"))))&&!exists(join(get(entry,"home"),"automations")))put(observation,"complete",boolean(false));add(after,observation);}
 Obj checked=automationSyncReconcile(as,after);put(a,"syncUnsafe",boolean(count(get(checked,"unsafe"))>0));
 Obj awaiting=get(checked,"pending");for(UInt i=0;i<count(awaiting);++i)put(pendingProfiles,utf8(at(awaiting,i)),boolean(true));
 awaiting=get(checked,"writes");for(UInt i=0;i<count(awaiting);++i)put(pendingProfiles,utf8(get(at(awaiting,i),"profileId")),boolean(true));
 pending=(Int)count(pendingProfiles);for(UInt i=0;i<count(members);++i){Obj entry=at(members,i),p=get(entry,"profile");if(p){if(get(pendingProfiles,utf8(get(entry,"profileId"))))put(p,"syncPending",boolean(true));else erase(p,"syncPending");}}
 Obj conflictsState=dict();put(conflictsState,"projects",get(reconciled,"conflicts"));put(conflictsState,"automations",get(checked,"conflicts"));put(journal,"conflicts",conflictsState);
 groupSyncBookmarks(a,snapshot);
 Int conflicts=(Int)count(get(reconciled,"conflicts"))+(Int)count(get(automationPlan,"conflicts"));
 if(unreadable&&apply&&!failure)failure=str(T("Some profile data is unavailable or unrecognized. It is kept unchanged; launch is postponed until it is fixed.","Есть недоступные или нераспознанные данные профиля. Они сохранены без изменений; запуск отложен до исправления."));
 Obj status=failure?failure:unreadable?str(T("Unrecognized data found: kept unchanged","Есть нераспознанные данные: они сохранены без изменений")):conflicts?str(T("Conflicting changes: the reconciled version was kept; details in the journal","Конфликт изменений: сохранён согласованный вариант; подробности в реестре")):pending?formatInt(T("Updates waiting for a restart: %ld","Обновления ожидают перезапуска: %ld"),pending):str(T("Projects and automations are in sync","Проекты и автоматизации согласованы"));
 put(a,"syncStatus",status);if(!same(previous,journal))groupSyncSave(a,journal);
 save();return failure;
}
Obj syncGroup(Obj a,bool apply){
 if(!a)return nullptr;for(int attempt=0;attempt<3;++attempt){erase(a,"syncNeedsRetry");Obj failure=syncGroupPass(a,apply);if(!truth(get(a,"syncNeedsRetry")))return failure;}
 erase(a,"syncNeedsRetry");return str(T("The Codex registry is changing during reconciliation. Sync again once the changes are done.","Реестр Codex изменяется во время согласования. Повторите синхронизацию после завершения изменений."));
}
void groupSyncPump(bool force=false){
 if(previewMode||quitting||groupSyncBusy||(!force&&usageUptime()<groupSyncNext))return;groupSyncNext=usageUptime()+10;
 // Polling only captures deltas and pending status. Native server work and profile writes run
 // on explicit synchronization or before a real launch, never in the periodic UI timer.
 for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);if(!groupSyncEnabled(a))continue;Obj failure=syncGroup(a,false);if(failure)put(a,"syncStatus",failure);}
}
Obj groupSyncEditProject(Obj a,Obj path,Obj removedId){
 if(Obj failure=syncGroup(a,false))return failure;Obj journal=groupSyncLoad(a);if(!journal)return str(T("The shared journal could not be read.","Общий реестр не прочитан."));Obj ps=get(journal,"projects"),snapshot=projectSyncSnapshot(ps);
 if(!snapshot)return str(T("Connect a supported Codex project list first.","Сначала подключите поддерживаемый список проектов Codex."));
 if(!projectSyncMarkApplied(ps,str("appdeck"),snapshot))return str(T("Could not prepare the project change.","Не удалось подготовить изменение проекта."));
 Obj registry=get(snapshot,"local-projects");if(removedId)erase(registry,utf8(removedId));else{
  Obj keys=send(registry,"allKeys");for(UInt i=0;i<count(keys);++i){Obj entry=get(registry,utf8(at(keys,i)));if(groupContains(get(entry,"rootPaths"),path))return nullptr;}
  Obj entry=dict(),id=uuid(),paths=array();add(paths,path);put(entry,"id",id);put(entry,"name",send(path,"lastPathComponent"));put(entry,"rootPaths",paths);put(entry,"createdAt",real(nowUnix()*1000));put(entry,"updatedAt",real(nowUnix()*1000));put(registry,utf8(id),entry);
 }
 Obj observations=array(),observation=dict();put(observation,"profileId",str("appdeck"));put(observation,"snapshot",snapshot);add(observations,observation);Obj result=projectSyncReconcile(ps,observations);
 if(!truth(get(result,"ok"))||!groupSyncSave(a,journal))return str(T("Could not save the change to the shared list.","Не удалось сохранить изменение общего списка."));return syncGroup(a,true);
}
Obj groupOwnerRemovalError(Obj p){
 Obj a=appFor(p);if(!a||isMaster(p)||!exists(groupSyncPath(a)))return nullptr;Obj journal=groupSyncLoad(a);if(!journal)return str(T("Repair the shared sync journal first.","Сначала восстановите общий реестр синхронизации."));
 Obj records=get(get(journal,"automations"),"records"),keys=send(records,"allKeys");for(UInt i=0;i<count(keys);++i){Obj r=get(records,utf8(at(keys,i)));
  if(!truth(get(r,"deleted"))&&same(get(r,"owner"),get(p,"id")))return str(T("This profile owns an automation. Choose another owner in Shared settings → Automations first, then detach or remove the profile.","Этот профиль назначен исполнителем автоматизации. Сначала выберите другого исполнителя в «Общие настройки → Автоматизации», затем отделите или уберите профиль."));}
 return nullptr;
}
void automationOwnerAction(Obj,Sel,Obj){
 Obj a=currentApp();if(!groupSyncEnabled(a)){showError(str(T("Shared workspace is off","Общая среда выключена")),str(T("Connect the base workspace and turn on shared history and projects first.","Сначала подключите базовую среду и включите общую историю и проекты.")));return;}
 if(Obj failure=syncGroup(a,true)){showError(str(T("Sync","Синхронизация")),failure);return;}
 Obj journal=groupSyncLoad(a),as=get(journal,"automations"),records=get(as,"records"),keys=send(records,"allKeys"),ids=array(),members=groupSyncParticipants(a);
 for(UInt i=0;i<count(keys);++i)if(!truth(get(get(records,utf8(at(keys,i))),"deleted")))add(ids,at(keys,i));
 if(!count(ids)){showError(str(T("No automations yet","Автоматизаций пока нет")),str(T("Create an automation in any linked profile. It appears in the others after they fully restart.","Создайте автоматизацию в любом связанном профиле. Она появится в остальных после их полного перезапуска.")));return;}
 Obj alert=make("NSAlert");send<void>(alert,"setMessageText:",str(T("Automation owner","Исполнитель автоматизации")));send<void>(alert,"setInformativeText:",str(T("The definition is available in every profile. Only the assigned profile runs the schedule; in the others it is paused. To change the owner, close this group's Codex instances. A paused schedule is never switched on automatically.","Определение доступно во всех профилях. Расписание выполняет только назначенный профиль; в остальных оно приостановлено. Для смены исполнителя закройте экземпляры Codex этой группы. Приостановленное расписание автоматически не включается.")));
 Obj accessory=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,460,84));
 Obj tasks=send(send(cls("NSPopUpButton"),"alloc"),"initWithFrame:pullsDown:",rect(0,48,460,28),false),owners=send(send(cls("NSPopUpButton"),"alloc"),"initWithFrame:pullsDown:",rect(0,8,460,28),false);
 for(UInt i=0;i<count(ids);++i){Obj id=at(ids,i),record=get(records,utf8(id)),parsed=automationDefinition(get(record,"text"),id),name=automationPlainString(get(get(parsed,"fields"),"name")),ownerName=get(record,"owner");
  for(UInt j=0;j<count(members);++j)if(same(get(at(members,j),"profileId"),get(record,"owner")))ownerName=get(at(members,j),"name");
  send<void>(tasks,"addItemWithTitle:",cat(cat(name?name:id,str(" — ")),ownerName));}
 for(UInt i=0;i<count(members);++i)send<void>(owners,"addItemWithTitle:",get(at(members,i),"name"));
 send<void>(accessory,"addSubview:",tasks);send<void>(accessory,"addSubview:",owners);send<void>(alert,"setAccessoryView:",accessory);send(alert,"addButtonWithTitle:",str(T("Assign","Назначить")));send(alert,"addButtonWithTitle:",str(T("Close","Закрыть")));frontForModal();
 if(send<Int>(alert,"runModal")==1000){bool active=false;for(UInt i=0;i<count(members);++i)if(groupParticipantRunning(a,at(members,i)))active=true;
  if(active)showError(str(T("Close the Codex instances first","Сначала закройте экземпляры Codex")),str(T("The owner change must finish before the schedule runs next. AppDeck stays open.","Смена исполнителя должна завершиться до следующего запуска расписания. AppDeck остаётся открытым.")));
  else{Int taskIndex=send<Int>(tasks,"indexOfSelectedItem"),ownerIndex=send<Int>(owners,"indexOfSelectedItem");
   Obj refreshFailure=syncGroup(a,true);journal=refreshFailure?nullptr:groupSyncLoad(a);as=get(journal,"automations");
   if(!refreshFailure&&taskIndex>=0&&ownerIndex>=0&&(UInt)taskIndex<count(ids)&&(UInt)ownerIndex<count(members)&&automationSyncSetOwner(as,at(ids,(UInt)taskIndex),get(at(members,(UInt)ownerIndex),"profileId"))&&groupSyncSave(a,journal)){
    Obj failure=syncGroup(a,true);if(failure)showError(str(T("Owner change is waiting for sync","Смена исполнителя ожидает синхронизации")),failure);else showError(str(T("Owner assigned","Исполнитель назначен")),str(T("Launch the assigned profile. If the schedule is paused, turn it on in that profile.","Запустите назначенный профиль. Если расписание приостановлено, включите его в этом профиле.")));
   }else showError(str(T("Owner unchanged","Исполнитель не изменён")),str(T("Could not save the selected profile.","Не удалось сохранить выбранный профиль.")));
  }
 }
 drop(tasks);drop(owners);drop(accessory);drop(alert);buildUI();
}
