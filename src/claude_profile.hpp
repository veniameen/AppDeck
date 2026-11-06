// Private first-run storage for new Claude profiles. Existing profiles keep their original mode.
// Preserve the native HOME: macOS Security resolves the login keychain directly from it.
bool claudeCleanProfile(Obj p){return p&&!isMaster(p)&&appFor(p)&&deck::adapter(utf8(get(appFor(p),"adapter")))==deck::Adapter::Claude&&truth(get(p,"claudeClean"));}
bool claudeCodeIsolated(Obj p){return claudeCleanProfile(p)&&truth(get(p,"claudeCodeIsolated"));}
Obj claudeCodeRoot(Obj p){return join(profileRoot(p),"claude-code");}
Obj claudeProfileHome(Obj p){return join(profileRoot(p),"work");}
// Keep Unix-domain socket paths short even when AppDeck's data root is deeply nested.
Obj claudeProfileTemp(Obj p){return deck::uuid(utf8(get(p,"id")))?cat(str("/private/tmp/appdeck-claude-"),get(p,"id")):nullptr;}
Obj prepareClaudeProfile(Obj p){
 if(!claudeCleanProfile(p))return nullptr;
 Obj h=claudeProfileHome(p),tmp=claudeProfileTemp(p);
 if(!mkdirPrivate(h)||!mkdirPrivate(tmp))return str(T("Could not create this Claude profile's working or temporary folder. Symbolic links are not allowed.","Не удалось создать рабочую или временную папку профиля Claude. Символические ссылки не допускаются."));
 for(const char* leaf:{".config",".cache",".local",".local/share",".local/state"})
  if(!mkdirPrivate(join(h,leaf)))return str(T("Could not prepare the empty Claude environment. Existing files were not replaced.","Не удалось подготовить чистую среду Claude. Существующие файлы не заменены."));
 if(claudeCodeIsolated(p)&&!mkdirPrivate(claudeCodeRoot(p)))return str(T("Could not create the private Claude Code folder. Existing files and symbolic links are not replaced.","Не удалось создать отдельную папку Claude Code. Существующие файлы и символические ссылки не заменяются."));
 return nullptr;
}
void applyClaudeEnvironment(Obj p,Obj env){
 // Claude launches with an exact NSTask environment: absent keys must stay absent. Some embedded
 // Code helpers use nullish fallback, so an empty CLAUDE_CONFIG_DIR is not equivalent to removal.
 Obj inherited=send(send(cls("NSProcessInfo"),"processInfo"),"environment"),keys=send(inherited,"allKeys");
 for(UInt i=0;i<count(keys);++i){const char* key=utf8(at(keys,i));
  if(deck::prefix(key,"CLAUDE_")||deck::prefix(key,"CLAUDECODE")||deck::prefix(key,"ANTHROPIC_")||deck::prefix(key,"CODEX_")||deck::prefix(key,"OPENAI_")||!strcmp(key,"NODE_OPTIONS")||!strcmp(key,"ELECTRON_RUN_AS_NODE"))erase(env,key);
 }
 for(const char* key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy","NO_PROXY","no_proxy","grpc_proxy","GRPC_PROXY","no_grpc_proxy","NO_GRPC_PROXY","NODE_USE_ENV_PROXY","DISABLE_TELEMETRY","DISABLE_ERROR_REPORTING","DISABLE_GROWTHBOOK","DO_NOT_TRACK"})erase(env,key);
 put(env,"CLAUDE_USER_DATA_DIR",join(profileRoot(p),"user-data"));
 if(truth(get(p,"claudeReduceTraffic")))for(const char* key:{"CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC","DISABLE_TELEMETRY","DISABLE_ERROR_REPORTING","DISABLE_GROWTHBOOK","DO_NOT_TRACK"})put(env,key,str("1"));
 if(!claudeCleanProfile(p))return;
 Obj h=claudeProfileHome(p),tmp=claudeProfileTemp(p);
 put(env,"HOME",home);put(env,"PWD",h);put(env,"TMPDIR",tmp);put(env,"MAC_CHROMIUM_TMPDIR",tmp);
 put(env,"XDG_CONFIG_HOME",join(h,".config"));put(env,"XDG_CACHE_HOME",join(h,".cache"));
 put(env,"XDG_DATA_HOME",join(h,".local/share"));put(env,"XDG_STATE_HOME",join(h,".local/state"));
 // Do not let an inherited Foundation home override redirect native helpers to another profile.
 erase(env,"CFFIXED_USER_HOME");
 if(claudeCodeIsolated(p))put(env,"CLAUDE_CONFIG_DIR",claudeCodeRoot(p));
}
// Only AppDeck's explicit Code settings keys are reconciled. Native Desktop-managed Code workers
// may read settings.json instead of inheriting the parent's proxy environment.
constexpr UInt claudeSettingsMaxBytes=1024*1024;
const char* const claudeSettingsKeys[]={"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy","NO_PROXY","no_proxy","NODE_USE_ENV_PROXY","CLAUDE_CODE_HTTP_PROXY","CLAUDE_CODE_HTTPS_PROXY","CLAUDE_CODE_PROXY_RESOLVES_HOSTS","grpc_proxy","CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC","DISABLE_TELEMETRY","DISABLE_ERROR_REPORTING","DISABLE_GROWTHBOOK","DO_NOT_TRACK"};
struct ClaudeSettingsSnapshot{bool exists=false;struct stat identity{};Obj bytes=nullptr;};
bool claudeSettingsSameIdentity(const struct stat& a,const struct stat& b){
 return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_nlink==b.st_nlink&&a.st_uid==b.st_uid&&
  a.st_mtimespec.tv_sec==b.st_mtimespec.tv_sec&&a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec&&a.st_ctimespec.tv_sec==b.st_ctimespec.tv_sec&&a.st_ctimespec.tv_nsec==b.st_ctimespec.tv_nsec;
}
int claudeSettingsDirectory(Obj path){
 if(!isClass(path,"NSString")||!deck::absoluteLocalPath(utf8(path)))return -1;
 int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return -1;Obj parts=send(path,"componentsSeparatedByString:",str("/"));
 for(UInt i=0;i<count(parts);++i){const char* leaf=utf8(at(parts,i));if(!*leaf)continue;if(!deck::safeLeaf(leaf)){close(fd);return -1;}
  int child=openat(fd,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(fd);fd=child;if(fd<0)return -1;
 }
 struct stat s{};if(fstat(fd,&s)!=0||s.st_uid!=geteuid()){close(fd);return -1;}return fd;
}
bool claudeSettingsRead(int parent,const char* leaf,ClaudeSettingsSnapshot& snapshot){
 snapshot={};int fd=openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return errno==ENOENT;
 struct stat before{},after{};bool ok=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&before.st_nlink==1&&before.st_uid==geteuid()&&before.st_size>=0&&(UInt)before.st_size<=claudeSettingsMaxBytes;
 if(ok){UInt size=(UInt)before.st_size,done=0;char* bytes=(char*)calloc(size+1,1);ok=bytes!=nullptr;
  if(bytes){while(done<size){long got=read(fd,bytes+done,size-done);if(got<0&&errno==EINTR)continue;if(got<=0)break;done+=(UInt)got;}
   ok=done==size&&fstat(fd,&after)==0&&claudeSettingsSameIdentity(before,after);
   if(ok){snapshot.exists=true;snapshot.identity=after;snapshot.bytes=send(cls("NSData"),"dataWithBytes:length:",bytes,size);}free(bytes);
  }
 }close(fd);return ok;
}
bool claudeSettingsStillMatches(int parent,const char* leaf,const ClaudeSettingsSnapshot& expected){
 ClaudeSettingsSnapshot actual;if(!claudeSettingsRead(parent,leaf,actual)||actual.exists!=expected.exists)return false;
 return !actual.exists||(claudeSettingsSameIdentity(actual.identity,expected.identity)&&same(actual.bytes,expected.bytes));
}
Obj claudeSettingsStage(int parent,Obj bytes){
 Obj leaf=cat(cat(str(".appdeck-settings-"),uuid()),str(".tmp"));int fd=openat(parent,utf8(leaf),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return nullptr;
 UInt size=send<UInt>(bytes,"length"),done=0;const char* data=send<const char*>(bytes,"bytes");
 while(done<size){long wrote=write(fd,data+done,size-done);if(wrote<0&&errno==EINTR)continue;if(wrote<=0)break;done+=(UInt)wrote;}
 bool ok=done==size&&fchmod(fd,0600)==0&&fsync(fd)==0;close(fd);if(!ok){unlinkat(parent,utf8(leaf),0);return nullptr;}return leaf;
}
bool claudeSettingsApply(int parent,Obj object,const ClaudeSettingsSnapshot& original,const ClaudeSettingsSnapshot& previous){
 Obj error=nullptr,bytes=send(cls("NSJSONSerialization"),"dataWithJSONObject:options:error:",object,(UInt)1,&error);if(!bytes||send<UInt>(bytes,"length")>claudeSettingsMaxBytes)return false;
 Obj staged=claudeSettingsStage(parent,bytes);if(!staged)return false;
 Obj backup=original.exists?claudeSettingsStage(parent,original.bytes):nullptr;bool ok=!original.exists||backup;
 if(ok)ok=claudeSettingsStillMatches(parent,"settings.json",original)&&claudeSettingsStillMatches(parent,"settings.json.appdeck-previous",previous);
 ClaudeSettingsSnapshot savedPrevious=previous;
 if(ok&&original.exists){ok=renameat(parent,utf8(backup),parent,"settings.json.appdeck-previous")==0&&fsync(parent)==0;
  if(ok)ok=claudeSettingsRead(parent,"settings.json.appdeck-previous",savedPrevious)&&savedPrevious.exists&&same(savedPrevious.bytes,original.bytes);
 }
 if(ok)ok=claudeSettingsStillMatches(parent,"settings.json",original)&&claudeSettingsStillMatches(parent,"settings.json.appdeck-previous",savedPrevious);
 if(ok)ok=renameat(parent,utf8(staged),parent,"settings.json")==0&&fsync(parent)==0;
 unlinkat(parent,utf8(staged),0);if(backup)unlinkat(parent,utf8(backup),0);return ok;
}
Obj claudeConfigureSettings(Obj p,Obj env){
 if(!claudeCodeIsolated(p))return nullptr;
 Obj failure=str(T("Could not safely update this profile's Claude Code settings. Check settings.json and its backup for changed, invalid or linked files. Launch was cancelled.","Не удалось безопасно обновить настройки Claude Code этого профиля. Проверьте settings.json и его резервную копию: файлы могли измениться, иметь неверный формат или быть ссылками. Запуск отменён."));
 if(!isClass(env,"NSDictionary"))return failure;
 int parent=claudeSettingsDirectory(claudeCodeRoot(p));if(parent<0)return failure;
 ClaudeSettingsSnapshot original,previous;if(!claudeSettingsRead(parent,"settings.json",original)||!claudeSettingsRead(parent,"settings.json.appdeck-previous",previous)){close(parent);return failure;}
 Obj error=nullptr,object=original.exists?send(cls("NSJSONSerialization"),"JSONObjectWithData:options:error:",original.bytes,(UInt)0,&error):dict();
 if(!isClass(object,"NSDictionary")||(get(object,"env")&&!isClass(get(object,"env"),"NSDictionary"))){close(parent);return failure;}
 Obj updated=autoRelease(send(object,"mutableCopy")),oldEnv=get(object,"env"),settingsEnv=oldEnv?autoRelease(send(oldEnv,"mutableCopy")):dict();
 for(const char* key:claudeSettingsKeys){Obj value=get(env,key);if(value&&(!isClass(value,"NSString")||send<UInt>(value,"length")>4096)){close(parent);return failure;}
  if(value&&send<UInt>(value,"length"))put(settingsEnv,key,value);else erase(settingsEnv,key);
 }
 if(oldEnv||count(settingsEnv))put(updated,"env",settingsEnv);
 bool ok=same(object,updated)?claudeSettingsStillMatches(parent,"settings.json",original)&&claudeSettingsStillMatches(parent,"settings.json.appdeck-previous",previous):claudeSettingsApply(parent,updated,original,previous);close(parent);return ok?nullptr:failure;
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
