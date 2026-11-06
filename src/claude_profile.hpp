// Private first-run storage for new Claude profiles. Existing profiles keep their original mode.
// Preserve the native HOME: macOS Security resolves the login keychain directly from it.
bool claudeCleanProfile(Obj p){return p&&!isMaster(p)&&appFor(p)&&deck::adapter(utf8(get(appFor(p),"adapter")))==deck::Adapter::Claude&&truth(get(p,"claudeClean"));}
Obj claudeProfileHome(Obj p){return join(profileRoot(p),"work");}
// Keep Unix-domain socket paths short even when AppDeck's data root is deeply nested.
Obj claudeProfileTemp(Obj p){return deck::uuid(utf8(get(p,"id")))?cat(str("/private/tmp/appdeck-claude-"),get(p,"id")):nullptr;}
Obj prepareClaudeProfile(Obj p){
 if(!claudeCleanProfile(p))return nullptr;
 Obj h=claudeProfileHome(p),tmp=claudeProfileTemp(p);
 if(!mkdirPrivate(h)||!mkdirPrivate(tmp))return str(T("Could not create this Claude profile's working or temporary folder. Symbolic links are not allowed.","Не удалось создать рабочую или временную папку профиля Claude. Символические ссылки не допускаются."));
 for(const char* leaf:{".config",".cache",".local",".local/share",".local/state"})
  if(!mkdirPrivate(join(h,leaf)))return str(T("Could not prepare the empty Claude environment. Existing files were not replaced.","Не удалось подготовить чистую среду Claude. Существующие файлы не заменены."));
 return nullptr;
}
void applyClaudeEnvironment(Obj p,Obj env){
 // Claude launches with an exact NSTask environment: absent keys must stay absent. Some embedded
 // Code helpers use nullish fallback, so an empty CLAUDE_CONFIG_DIR is not equivalent to removal.
 Obj inherited=send(send(cls("NSProcessInfo"),"processInfo"),"environment"),keys=send(inherited,"allKeys");
 for(UInt i=0;i<count(keys);++i){const char* key=utf8(at(keys,i));
  if(deck::prefix(key,"CLAUDE_")||deck::prefix(key,"CLAUDECODE")||deck::prefix(key,"ANTHROPIC_")||deck::prefix(key,"CODEX_")||deck::prefix(key,"OPENAI_")||!strcmp(key,"NODE_OPTIONS")||!strcmp(key,"ELECTRON_RUN_AS_NODE"))erase(env,key);
 }
 put(env,"CLAUDE_USER_DATA_DIR",join(profileRoot(p),"user-data"));
 if(!claudeCleanProfile(p))return;
 Obj h=claudeProfileHome(p),tmp=claudeProfileTemp(p);
 put(env,"HOME",home);put(env,"PWD",h);put(env,"TMPDIR",tmp);put(env,"MAC_CHROMIUM_TMPDIR",tmp);
 put(env,"XDG_CONFIG_HOME",join(h,".config"));put(env,"XDG_CACHE_HOME",join(h,".cache"));
 put(env,"XDG_DATA_HOME",join(h,".local/share"));put(env,"XDG_STATE_HOME",join(h,".local/state"));
 // Do not let an inherited Foundation home override redirect native helpers to another profile.
 erase(env,"CFFIXED_USER_HOME");
}
Obj claudeLaunchTasks=nullptr;
Obj claudeStartingProfiles=nullptr;
bool claudeLaunchInProgress(Obj p){return claudeStartingProfiles&&get(claudeStartingProfiles,utf8(get(p,"id")))!=nullptr;}
struct ClaudeLaunchScope{
 Obj id;
 explicit ClaudeLaunchScope(Obj p):id(keep(get(p,"id"))){if(!claudeStartingProfiles)claudeStartingProfiles=keep(dict());put(claudeStartingProfiles,utf8(id),boolean(true));}
 ~ClaudeLaunchScope(){erase(claudeStartingProfiles,utf8(id));drop(id);}
};
void claudeReapLaunches(){
 for(UInt i=count(claudeLaunchTasks);i>0;--i){Obj task=at(claudeLaunchTasks,i-1);
  if(!send<bool>(task,"isRunning")){send<void>(task,"waitUntilExit");send<void>(claudeLaunchTasks,"removeObjectAtIndex:",i-1);}}
}
Obj launchClaudeProcess(Obj p,Obj env,Obj args,bool activate,Obj* error){
 if(claudeLaunchInProgress(p))return nullptr;ClaudeLaunchScope launchScope(p);
 Obj bundle=send(cls("NSBundle"),"bundleWithPath:",get(appFor(p),"path")),executable=send(bundle,"executablePath");
 if(!executable)return nullptr;
 Obj task=make("NSTask");send<void>(task,"setExecutableURL:",url(executable));send<void>(task,"setArguments:",args);send<void>(task,"setEnvironment:",env);
 send<void>(task,"setCurrentDirectoryURL:",url(claudeCleanProfile(p)?claudeProfileHome(p):home));
 Obj null=send(cls("NSFileHandle"),"fileHandleWithNullDevice");send<void>(task,"setStandardInput:",null);send<void>(task,"setStandardOutput:",null);send<void>(task,"setStandardError:",null);
 if(!send<bool>(task,"launchAndReturnError:",error)){drop(task);return nullptr;}
 if(!claudeLaunchTasks)claudeLaunchTasks=keep(array());add(claudeLaunchTasks,task);
 int pid=send<int>(task,"processIdentifier");Obj result=nullptr;
 double deadline=send<double>(send(cls("NSProcessInfo"),"processInfo"),"systemUptime")+10;
 // An unmodified app executable registers normally with macOS. Track the exact child, never
 // resolve a bundle ID through LaunchServices (which could select another account).
 while(send<bool>(task,"isRunning")&&send<double>(send(cls("NSProcessInfo"),"processInfo"),"systemUptime")<deadline){
  Obj candidate=send(cls("NSRunningApplication"),"runningApplicationWithProcessIdentifier:",pid),path=send(send(candidate,"bundleURL"),"path");
  if(candidate&&path&&same(canonical(path),canonical(get(appFor(p),"path")))&&processStarted(candidate)>0){result=candidate;break;}
  send<void>(send(cls("NSRunLoop"),"currentRunLoop"),"runUntilDate:",send(cls("NSDate"),"dateWithTimeIntervalSinceNow:",.025));
 }
 if(!result&&send<bool>(task,"isRunning"))send<void>(task,"terminate");
 if(result&&activate)send<bool>(result,"activateWithOptions:",(UInt)3);
 drop(task);return result;
}
