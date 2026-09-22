// Settings source + legacy seed. Versioned group_sync.hpp owns the stopped-profile
// project/automation reconciliation. Never mirror a complete CODEX_HOME.
bool isMaster(Obj p){return truth(get(p,"master"));}
Obj baseSource(Obj a){Obj s=get(a,"baseSource");return isClass(s,"NSString")?s:nullptr;}
Obj settingsSource(Obj a){Obj s=baseSource(a);return s?s:sharedRoot(a);}
Obj readJSON(Obj path){
 if(symlink(path)||!exists(path)||!same(get(attrs(path),"NSFileType"),str("NSFileTypeRegular"))||integer(get(attrs(path),"NSFileSize"))>8*1024*1024)return nullptr;
 Obj data=send(cls("NSData"),"dataWithContentsOfFile:",path);if(!data)return nullptr;Obj e=nullptr;
 Obj o=send(cls("NSJSONSerialization"),"JSONObjectWithData:options:error:",data,(UInt)1,&e);
 return isClass(o,"NSDictionary")?o:nullptr;
}
bool sharesHistory(Obj p);
bool writeJSON(Obj path,Obj data,UInt options=3){
 if(symlink(path))return false;Obj err=nullptr;Obj bytes=send(cls("NSJSONSerialization"),"dataWithJSONObject:options:error:",data,options,&err);
 if(!bytes)return false;bool ok=send<bool>(bytes,"writeToFile:atomically:",path,true);if(ok)chmod(utf8(path),0600);return ok;
}
#include "workspace_filter.hpp"
Obj workspaceSnapshot(Obj source){return sanitizeWorkspaceSnapshot(readJSON(join(source,".codex-global-state.json")));}
void importProjectPath(Obj path,Obj name,Int& added){
 for(UInt j=0;j<count(projects);++j)if(same(get(at(projects,j),"path"),path))return;
 Obj p=dict();put(p,"path",path);put(p,"name",name?name:send(path,"lastPathComponent"));put(p,"origin",str("codex-base"));add(projects,p);++added;
}
Int importProjectList(Obj snapshot){
 if(!snapshot)return 0;Obj roots=get(snapshot,"electron-saved-workspace-roots"),labels=get(snapshot,"electron-workspace-root-labels");Int added=0;
 // Current Desktop builds: the registry carries the project names.
 Obj registry=get(snapshot,"local-projects"),order=get(snapshot,"project-order");
 for(UInt i=0;i<count(order);++i){Obj entry=get(registry,utf8(at(order,i)));Obj paths=get(entry,"rootPaths");if(count(paths))importProjectPath(at(paths,0),get(entry,"name"),added);}
 for(UInt i=0;i<count(roots);++i){Obj path=at(roots,i);bool found=false;
  for(UInt j=0;j<count(projects);++j)if(same(get(at(projects,j),"path"),path)){found=true;break;}
  if(found)continue;Obj p=dict();put(p,"path",path);Obj name=get(labels,utf8(path));put(p,"name",name?name:send(path,"lastPathComponent"));put(p,"origin",str("codex-base"));add(projects,p);++added;
 }
 return added;
}
// Project list of a linked copy. Two modes:
//  * seed (experimental option): written once, only into a profile that has no Desktop state yet;
//  * mirror (group shares history AND projects): before every launch of the STOPPED copy the
//    allowlisted registry keys are merged in from the base. The rest of the copy's Desktop state is
//    preserved byte-for-meaning, projects that exist only in the copy are kept, and the previous
//    file stays next to it as `.appdeck-previous`. The base is only read.
Obj seedWorkspace(Obj p){
 Obj a=appFor(p);bool mirror=sharesHistory(p),seed=truth(get(a,"seedWorkspace"));
 if(!baseSource(a)||isMaster(p)||!truth(get(p,"share"))||(!mirror&&(!seed||get(p,"workspaceSeeded"))))return nullptr;
 Obj target=join(join(profileRoot(p),"codex"),".codex-global-state.json");if(symlink(target))return nullptr;
 Obj snapshot=workspaceSnapshot(baseSource(a));if(!snapshot){put(p,"workspaceSeeded",str("unrecognized-source"));return nullptr;}
 if(!exists(target)){
  if(!writeJSON(target,snapshot))return str(T("Could not write the initial project list. The original Codex state is unchanged.","Не удалось записать начальный список проектов. Исходное состояние Codex не изменено."));
  put(p,"workspaceSeeded",str("registry"));note("Allowlisted project registry seeded in a previously empty profile; no history or credentials copied.");return nullptr;
 }
 if(!mirror){put(p,"workspaceSeeded",str("skipped-existing"));return nullptr;} // without sharing, existing state is never touched
 Obj current=readJSON(target);if(!current){note("Project mirror skipped: the copy's Desktop state is unreadable or larger than 8 MiB.");return nullptr;}
 if(!mergeWorkspaceSnapshot(current,snapshot))return nullptr; // already identical: the file is not rewritten
 Obj backup=cat(target,str(".appdeck-previous"));Obj previous=symlink(backup)?nullptr:send(cls("NSData"),"dataWithContentsOfFile:",target);
 if(!previous||!send<bool>(previous,"writeToFile:atomically:",backup,true))return str(T("Could not back up the copy's state before updating its project list. Launch cancelled; no files changed.","Не удалось сохранить прежнее состояние копии перед обновлением списка проектов. Запуск отменён, файлы не изменены."));
 chmod(utf8(backup),0600);
 if(!writeJSON(target,current,0))return str(T("Could not update the copy's project list. The previous state is kept next to it as .appdeck-previous.","Не удалось обновить список проектов копии. Прежнее состояние сохранено рядом как .appdeck-previous."));
 put(p,"workspaceSeeded",str("mirrored"));note("Project registry of the stopped copy brought up to the base (allowlisted keys, union; previous file kept).");return nullptr;
}
bool linkSettingDirectory(Obj source,Obj dest,Obj a,const char* leaf){
 if(!exists(source))return true;Obj resolved=canonical(source);if(!directory(resolved))return false;
 if(symlink(dest)){
  Obj error=nullptr,old=send(fileManager,"destinationOfSymbolicLinkAtPath:error:",dest,&error);
  if(same(old,resolved)||same(old,source))return true;
  // Only replace AppDeck's own former shared link, never an arbitrary user's link.
  if(!same(old,join(sharedRoot(a),leaf)))return false;
  if(!send<bool>(fileManager,"removeItemAtPath:error:",dest,&error))return false;
  if(sharedLink(resolved,dest))return true;sharedLink(old,dest);return false;
 }
 return sharedLink(resolved,dest);
}
// Opt-in shared history. The thread/project databases stay where the original Codex keeps them:
// copies are pointed at that folder through Codex's own CODEX_SQLITE_HOME switch. Nothing is
// cloned, and auth.json, cookies and window data remain private to every account.
void consentKeys(Obj alert);
bool historyAvailable(Obj a){
 Obj base=baseSource(a);if(!base||!directory(canonical(base)))return false;Obj e=nullptr;Obj items=send(fileManager,"contentsOfDirectoryAtPath:error:",canonical(base),&e);
 for(UInt i=0;i<count(items);++i)if(deck::stateDatabase(utf8(at(items,i))))return true;return false;
}
bool sharesHistory(Obj p){Obj a=appFor(p);return a&&!isMaster(p)&&truth(get(p,"share"))&&truth(get(a,"sharedHistory"))&&historyAvailable(a);}
const char* const historyLeaves[]={"sessions","archived_sessions","thread-writer-locks"};
// Rollout folders are linked only into a copy that has none of its own: local history is never hidden.
void linkHistoryDirectory(Obj source,Obj dest){
 Obj resolved=canonical(source);if(!directory(resolved)||symlink(dest))return;Obj e=nullptr;
 if(exists(dest)){Obj items=send(fileManager,"contentsOfDirectoryAtPath:error:",dest,&e);
  if(!directory(dest)||!items||count(items))return;
  if(!send<bool>(fileManager,"removeItemAtPath:error:",dest,&e))return;}
 sharedLink(resolved,dest);
}
// Removes nothing but AppDeck's own link to the connected base.
void unlinkHistoryDirectory(Obj source,Obj dest){
 if(!symlink(dest))return;Obj e=nullptr;Obj link=send(fileManager,"destinationOfSymbolicLinkAtPath:error:",dest,&e);
 if(same(link,canonical(source))||same(link,source))send<bool>(fileManager,"removeItemAtPath:error:",dest,&e);
}
void syncHistoryLinks(Obj p){
 Obj a=appFor(p),base=baseSource(a);if(!base||isMaster(p))return;Obj ch=join(profileRoot(p),"codex");bool on=sharesHistory(p);
 for(const char* leaf:historyLeaves){if(!deck::historyLeaf(leaf))continue;if(on)linkHistoryDirectory(join(base,leaf),join(ch,leaf));else unlinkHistoryDirectory(join(base,leaf),join(ch,leaf));}
}
// Asked once per group, at the moment it first matters. Stored as an explicit yes/no.
void ensureHistoryChoice(Obj a){
 if(!a||previewMode||get(a,"sharedHistory")||!baseSource(a)||!historyAvailable(a))return;frontForModal();
 Obj alert=make("NSAlert");send<void>(alert,"setMessageText:",str(T("Show the primary Codex history and projects in copies?","Показывать историю и проекты основного Codex в копиях?")));
 send<void>(alert,"setInformativeText:",str(T("Linked copies will open the same thread database as the primary Codex (the CODEX_SQLITE_HOME variable) and its sessions / archived_sessions folders. Chats, projects and pins become shared by every account in the group; a new chat from any copy also appears in the primary Codex. The other Codex databases in that folder become shared too: log, queue, memory, goals.\n\nNo database is copied. Sign-in (auth.json), cookies and window data stay separate for each account. Continuing another account's chat depends on the rules of Codex itself.\n\nYou can change this in Shared settings.","Связанные копии будут открывать ту же базу тредов, что и основной Codex (переменная CODEX_SQLITE_HOME), и его папки sessions / archived_sessions. Чаты, проекты и закрепления станут общими для всех аккаунтов группы; новый чат из любой копии появится и в основном Codex. Вместе с базой тредов общими становятся и остальные базы Codex в этой папке: журнал, очередь, память, цели.\n\nБазы не копируются. Вход (auth.json), cookies и данные окна остаются отдельными у каждого аккаунта. Продолжение чужого чата под другим аккаунтом зависит от правил самого Codex.\n\nВыбор меняется в «Общих настройках».")));
 send(alert,"addButtonWithTitle:",str(T("Shared history","Общая история")));send(alert,"addButtonWithTitle:",str(T("Keep separate","Оставить раздельно")));consentKeys(alert);
 bool yes=send<Int>(alert,"runModal")==1000;drop(alert);put(a,"sharedHistory",boolean(yes));save();
 note(yes?"Shared history enabled through CODEX_SQLITE_HOME; no database copied.":"Shared history declined; copies keep separate thread databases.");
}
bool sameApp(Obj r,Obj a){
 Obj bid=get(a,"bundleId");if(bid&&send<UInt>(bid,"length")&&!same(send(r,"bundleIdentifier"),bid))return false;
 Obj rp=send(send(r,"bundleURL"),"path");return rp&&same(canonical(rp),canonical(get(a,"path")));
}
// A process started with --user-data-dir inside AppDeck's data is a copy, tracked or not.
bool managedProcess(Obj r){Obj ud=processUserDataDir(send<int>(r,"processIdentifier"));return ud&&deck::inside(utf8(canonical(dataRoot)),utf8(canonical(ud)));}
Obj ownedProcess(Obj p){
 Obj a=appFor(p),root=profileRoot(p);if(!a||!root||isMaster(p))return nullptr;Obj want=canonical(join(root,"user-data"));if(!exists(want))return nullptr;
 Obj all=send(workspace,"runningApplications");
 for(UInt i=0;i<count(all);++i){Obj r=at(all,i);if(send<bool>(r,"isTerminated")||!sameApp(r,a))continue;
  Obj ud=processUserDataDir(send<int>(r,"processIdentifier"));if(ud&&same(canonical(ud),want))return r;}
 return nullptr;
}
bool adopt(Obj p,Obj r){
 Obj date=r?send(r,"launchDate"):nullptr;if(!date)return false;
 put(p,"pid",num(send<int>(r,"processIdentifier")));put(p,"started",real(send<double>(date,"timeIntervalSince1970")));erase(p,"lastError");erase(p,"launchCheck");return true;
}
// Re-attach to copies that outlived a previous AppDeck session or restarted themselves.
// The original app of a master card: the one running process of that bundle that is not a copy.
// More than one such process is ambiguous and is left for an explicit click (which explains it).
Obj originalProcess(Obj p){
 Obj a=appFor(p);if(!a)return nullptr;Obj all=send(workspace,"runningApplications"),found=nullptr;
 for(UInt i=0;i<count(all);++i){Obj r=at(all,i);if(send<bool>(r,"isTerminated")||!sameApp(r,a)||managedProcess(r))continue;if(found)return nullptr;found=r;}
 return found;
}
bool adoptRunning(){
 bool changed=false;for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);
  // Status only: no window is raised and nothing is launched.
  if(isMaster(p)){if(!running(p)&&adopt(p,originalProcess(p)))changed=true;continue;}
  if(running(p)||!exists(profileRoot(p)))continue;
  if(adopt(p,ownedProcess(p))){changed=true;note("Re-attached to a running copy by its --user-data-dir argument.");}}
 return changed;
}
bool attachMaster(Obj p,bool activate){
 Obj a=appFor(p),path=get(a,"path"),candidates=array();Obj all=send(workspace,"runningApplications");
 for(UInt i=0;i<count(all);++i){Obj r=at(all,i),rp=send(send(r,"bundleURL"),"path");
  if(!rp||!same(canonical(rp),canonical(path))||send<bool>(r,"isTerminated"))continue;bool managed=managedProcess(r);
  for(UInt j=0;j<count(profiles);++j){Obj other=at(profiles,j);if(other!=p&&running(other)&&integer(get(other,"pid"))==send<int>(r,"processIdentifier")){managed=true;break;}}
  if(!managed)add(candidates,r);
 }
 Obj r=nullptr;
 if(count(candidates)>1){showError(cat(str(T("Several untracked windows found — ","Найдено несколько неучтённых окон — ")),get(a,"name")),str(T("Close the extra instances manually and keep only the primary one. AppDeck does not pick an account by an arbitrary PID.","Закройте лишние экземпляры вручную, оставив только основной. AppDeck не выбирает аккаунт по случайному PID.")));return false;}
 if(count(candidates)==1)r=at(candidates,0);
 if(!r){
  // Launch normally: default application identity and existing credentials remain.
  // While copies are alive LaunchServices would merely re-activate one of them, so a separate
  // process is requested. It gets no profile environment or --user-data-dir: it is the original.
  bool copies=false;for(UInt j=0;j<count(all);++j){Obj other=at(all,j);Obj op=send(send(other,"bundleURL"),"path");if(op&&same(canonical(op),canonical(path))&&!send<bool>(other,"isTerminated")){copies=true;break;}}
  Obj err=nullptr;r=send(workspace,"launchApplicationAtURL:options:configuration:error:",url(path),(UInt)((copies?(1UL<<19):0)|(activate?0:(1UL<<9))),dict(),&err);
  if(!r){showError(cat(str(T("Could not open the primary window — ","Не удалось открыть основное окно — ")),get(a,"name")),err?send(err,"localizedDescription"):str(T("Open the app manually.","Запустите приложение вручную.")));return false;}
  if(managedProcess(r)){showError(str(T("A copy opened instead of the primary window","Открылась копия, а не основное окно")),str(T("macOS returned the process of an additional profile. Close the copies, open the app from the Dock and try again.","macOS вернула процесс одного из дополнительных профилей. Закройте копии, откройте приложение из Dock и повторите.")));return false;}
 }
 Obj date=send(r,"launchDate");if(!date)return false;
 put(p,"pid",num(send<int>(r,"processIdentifier")));put(p,"started",real(send<double>(date,"timeIntervalSince1970")));erase(p,"lastError");
 if(activate)focus(r);note("Attached to original Codex without rewriting its profile or credentials.");save();refresh();return true;
}
