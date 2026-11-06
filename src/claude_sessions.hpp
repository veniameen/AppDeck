// Included inside main.cpp's namespace, after workspace_base.hpp. Reads only Claude's native
// session descriptors, never cookies, credentials, databases, transcripts or Electron storage.
// All directory components are opened without following links; the scanner is bounded.
struct ClaudeScanBudget{UInt files=0,bytes=0,skipped=0;bool complete=true;};
constexpr UInt claudeMetadataMax=1024*1024,claudeScanMaxBytes=24*1024*1024,claudeScanMaxFiles=2048;
Obj claudeCatalogue=nullptr,claudeCatalogueApp=nullptr,claudeStatusLabel=nullptr;
UInt claudeCatalogueSkipped=0;
bool claudeCatalogueComplete=true;
bool claudeGroup(Obj a){return a&&deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Claude;}
Obj claudeUserData(Obj p){return isMaster(p)?join(home,"Library/Application Support/Claude"):join(profileRoot(p),"user-data");}
int claudeOpenDirectory(Obj path){
 if(!isClass(path,"NSString")||!deck::absoluteLocalPath(utf8(path)))return -1;
 int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return -1;
 Obj components=send(path,"componentsSeparatedByString:",str("/"));
 for(UInt i=0;i<count(components);++i){const char* name=utf8(at(components,i));if(!*name)continue;
  if(!deck::safeLeaf(name)){close(fd);return -1;}
  int next=openat(fd,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(fd);fd=next;if(fd<0)return -1;
 }return fd;
}
Obj claudeDirectoryNames(int fd,ClaudeScanBudget& budget){
 int duplicate=dup(fd);if(duplicate<0){budget.complete=false;return nullptr;}
 DIR* dir=fdopendir(duplicate);if(!dir){close(duplicate);budget.complete=false;return nullptr;}
 Obj names=array();UInt seen=0;errno=0;
 while(struct dirent* entry=readdir(dir)){
  if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
  if(++seen>4096){budget.complete=false;break;}add(names,str(entry->d_name));errno=0;
 }
 if(errno)budget.complete=false;closedir(dir);return send(names,"sortedArrayUsingSelector:",sel("compare:"));
}
bool claudeSameFile(const struct stat& before,const struct stat& after){
 return before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&before.st_size==after.st_size&&before.st_nlink==after.st_nlink&&
  before.st_mtimespec.tv_sec==after.st_mtimespec.tv_sec&&before.st_mtimespec.tv_nsec==after.st_mtimespec.tv_nsec&&
  before.st_ctimespec.tv_sec==after.st_ctimespec.tv_sec&&before.st_ctimespec.tv_nsec==after.st_ctimespec.tv_nsec;
}
Obj claudeReadObject(int parent,const char* leaf,Obj* generation=nullptr,UInt* consumed=nullptr){
 if(!deck::safeLeaf(leaf))return nullptr;
 int fd=openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return nullptr;
 struct stat before{},after{};Obj result=nullptr;
 if(fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&before.st_nlink==1&&before.st_size>0&&(UInt)before.st_size<=claudeMetadataMax){
  UInt size=(UInt)before.st_size;if(consumed)*consumed+=size;char* bytes=(char*)calloc(size,1);
  if(bytes){UInt done=0;while(done<size){long got=read(fd,bytes+done,size-done);if(got<0&&errno==EINTR)continue;if(got<=0)break;done+=(UInt)got;}
   if(done==size&&fstat(fd,&after)==0&&claudeSameFile(before,after)){
    Obj data=send(cls("NSData"),"dataWithBytes:length:",bytes,size),error=nullptr;
    Obj parsed=send(cls("NSJSONSerialization"),"JSONObjectWithData:options:error:",data,(UInt)0,&error);
    if(isClass(parsed,"NSDictionary")){result=parsed;if(generation){char g[160];snprintf(g,sizeof g,"%llu:%llu:%lld:%ld:%lld:%ld:%lld",(unsigned long long)before.st_dev,(unsigned long long)before.st_ino,(long long)before.st_mtimespec.tv_sec,before.st_mtimespec.tv_nsec,(long long)before.st_ctimespec.tv_sec,before.st_ctimespec.tv_nsec,(long long)before.st_size);*generation=str(g);}}
   }free(bytes);
  }
 }close(fd);return result;
}
Obj claudeDescriptor(Obj raw,const char* leaf,Obj path,Obj generation,Obj profileId){
 if(!raw||!deck::claudeMetadataLeaf(leaf))return nullptr;
 Obj sid=get(raw,"sessionId"),id=get(raw,"cliSessionId"),cwd=get(raw,"cwd"),root=get(raw,"originCwd"),title=get(raw,"title"),archived=get(raw,"isArchived");
 char stem[43]{};memcpy(stem,leaf,42);
 if(!isClass(sid,"NSString")||strcmp(utf8(sid),stem)||!isClass(id,"NSString")||!deck::uuid(utf8(id))||!isClass(cwd,"NSString")||!deck::claudeProjectPath(utf8(cwd)))return nullptr;
 if(root&&(!isClass(root,"NSString")||!deck::claudeProjectPath(utf8(root))))return nullptr;
 if(title&&(!isClass(title,"NSString")||send<UInt>(title,"length")>1024))return nullptr;
 if(archived&&(!isClass(archived,"NSNumber")||(send<double>(archived,"doubleValue")!=0&&send<double>(archived,"doubleValue")!=1)))return nullptr;
 for(const char* k:{"lastActivityAt","lastFocusedAt"})if(Obj v=get(raw,k)){if(!isClass(v,"NSNumber"))return nullptr;double d=send<double>(v,"doubleValue");if(!(d>=0&&d<1e16))return nullptr;}
 Obj clean=dict();put(clean,"id",send(id,"lowercaseString"));put(clean,"cwd",cwd);put(clean,"projectRoot",root?root:cwd);
 put(clean,"title",title&&send<UInt>(title,"length")?title:send(cwd,"lastPathComponent"));put(clean,"archived",boolean(truth(archived)));
 put(clean,"profileId",profileId);put(clean,"metadataPath",path);put(clean,"generation",generation);
 put(clean,"activity",get(raw,"lastActivityAt")?get(raw,"lastActivityAt"):num(0));return clean;
}
Obj claudeScanProfile(Obj p,ClaudeScanBudget& budget){
 Obj records=array(),root=join(claudeUserData(p),"claude-code-sessions");int base=claudeOpenDirectory(root);
 if(base<0){if(errno!=ENOENT||exists(root)){budget.complete=false;++budget.skipped;}return records;}
 Obj accounts=claudeDirectoryNames(base,budget);if(count(accounts)>128){budget.complete=false;close(base);return records;}
 for(UInt i=0;i<count(accounts)&&budget.complete;++i){Obj account=at(accounts,i);if(!deck::uuid(utf8(account)))continue;
  int ac=openat(base,utf8(account),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(ac<0){++budget.skipped;continue;}
  Obj orgs=claudeDirectoryNames(ac,budget);if(count(orgs)>128){budget.complete=false;close(ac);break;}
  for(UInt j=0;j<count(orgs)&&budget.complete;++j){Obj org=at(orgs,j);if(!deck::uuid(utf8(org)))continue;
   int od=openat(ac,utf8(org),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(od<0){++budget.skipped;continue;}
   Obj names=claudeDirectoryNames(od,budget);
   for(UInt k=0;k<count(names)&&budget.complete;++k){Obj name=at(names,k);if(!deck::claudeMetadataLeaf(utf8(name)))continue;
    if(++budget.files>claudeScanMaxFiles||budget.bytes>claudeScanMaxBytes-claudeMetadataMax){budget.complete=false;break;}
    Obj generation=nullptr,raw=claudeReadObject(od,utf8(name),&generation,&budget.bytes);
    Obj path=join(join(join(root,account),org),name),record=claudeDescriptor(raw,utf8(name),path,generation,get(p,"id"));
    if(record)add(records,record);else ++budget.skipped;
   }close(od);
  }close(ac);
 }close(base);return records;
}
Obj claudeMergeCatalogue(Obj records){
 Obj byId=dict();for(UInt i=0;i<count(records);++i){Obj record=at(records,i),id=get(record,"id"),entry=get(byId,utf8(id));
  if(!entry){entry=dict();for(const char* k:{"id","cwd","projectRoot","title","activity"})put(entry,k,get(record,k));put(entry,"records",array());put(byId,utf8(id),entry);}
  else{if(!same(get(entry,"cwd"),get(record,"cwd"))||!same(get(entry,"projectRoot"),get(record,"projectRoot")))put(entry,"conflict",boolean(true));
   if(send<double>(get(record,"activity"),"doubleValue")>send<double>(get(entry,"activity"),"doubleValue")){put(entry,"title",get(record,"title"));put(entry,"activity",get(record,"activity"));}}
  add(get(entry,"records"),record);if(!truth(get(record,"archived")))put(entry,"visible",boolean(true));
 }
 Obj ids=send(send(byId,"allKeys"),"sortedArrayUsingSelector:",sel("compare:")),rows=array();
 for(UInt i=0;i<count(ids);++i){Obj e=get(byId,utf8(at(ids,i)));if(truth(get(e,"visible")))add(rows,e);}
 Obj sorting=array();for(const char* key:{"projectRoot","title","id"})add(sorting,send(cls("NSSortDescriptor"),"sortDescriptorWithKey:ascending:",str(key),true));
 return send(rows,"sortedArrayUsingDescriptors:",sorting);
}
Obj claudeLoadCatalogue(Obj a){
 Obj records=array();ClaudeScanBudget budget;
 for(UInt i=0;i<count(profiles)&&budget.complete;++i){Obj p=at(profiles,i);if(appFor(p)==a&&!claudeCleanProfile(p))send<void>(records,"addObjectsFromArray:",claudeScanProfile(p,budget));}
 claudeCatalogueSkipped=budget.skipped;claudeCatalogueComplete=budget.complete;return claudeMergeCatalogue(records);
}
bool claudeLeaseValid(Obj lease){
 if(!isClass(lease,"NSDictionary"))return false;
 for(const char* key:{"version","pid","started","requestedAt"})if(!isClass(get(lease,key),"NSNumber"))return false;
 for(const char* key:{"profileId","id","status","cwd"})if(!isClass(get(lease,key),"NSString"))return false;
 double started=send<double>(get(lease,"started"),"doubleValue"),requested=send<double>(get(lease,"requestedAt"),"doubleValue");
 Obj status=get(lease,"status"),before=get(lease,"before");
 double pid=send<double>(get(lease,"pid"),"doubleValue");
 if(send<double>(get(lease,"version"),"doubleValue")!=1||!deck::uuid(utf8(get(lease,"profileId")))||!deck::uuid(utf8(get(lease,"id")))||!deck::claudeProjectPath(utf8(get(lease,"cwd")))||!(pid>=1&&pid<=2147483647)||pid!=(int)pid||
  !(started>0&&started<1e11)||!(requested>0&&requested<1e11)||!isClass(before,"NSDictionary")||count(before)>claudeScanMaxFiles||
  (!same(status,str("pending"))&&!same(status,str("confirmed"))&&!same(status,str("timeout"))&&!same(status,str("transport-error"))))return false;
 Obj keys=send(before,"allKeys");for(UInt i=0;i<count(keys);++i)if(!isClass(at(keys,i),"NSString")||!isClass(get(before,utf8(at(keys,i))),"NSString"))return false;
 return true;
}
Obj claudeReadLease(Obj a,bool& valid){
 valid=false;bool recorded=truth(get(a,"claudeBridgeLeaseRecorded"));int fd=claudeOpenDirectory(sharedRoot(a));
 if(fd<0){valid=errno==ENOENT&&!recorded;return valid?dict():nullptr;}
 struct stat st{};if(fstatat(fd,"claude-code-lease.json",&st,AT_SYMLINK_NOFOLLOW)!=0){valid=errno==ENOENT&&!recorded;close(fd);return valid?dict():nullptr;}
 Obj lease=claudeReadObject(fd,"claude-code-lease.json");close(fd);valid=claudeLeaseValid(lease);return valid?lease:nullptr;
}
int claudePrivateChild(int parent,const char* name){
 if(!deck::safeLeaf(name)||(mkdirat(parent,name,0700)!=0&&errno!=EEXIST))return -1;
 int fd=openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
 struct stat st{};if(fstat(fd,&st)!=0||st.st_uid!=geteuid()||fchmod(fd,0700)!=0){close(fd);return -1;}return fd;
}
bool claudeWriteLease(Obj a,Obj lease,Obj expected=nullptr){
 if(!claudeLeaseValid(lease)||!deck::uuid(utf8(get(a,"id"))))return false;
 int root=claudeOpenDirectory(dataRoot);if(root<0)return false;
 int shared=claudePrivateChild(root,"Shared");close(root);if(shared<0)return false;
 int fd=claudePrivateChild(shared,utf8(get(a,"id")));close(shared);if(fd<0)return false;
 const char* leaf="claude-code-lease.json";struct stat st{};bool existed=fstatat(fd,leaf,&st,AT_SYMLINK_NOFOLLOW)==0;
 if(!existed&&errno!=ENOENT){close(fd);return false;}
 Obj generation=nullptr,old=existed?claudeReadObject(fd,leaf,&generation):nullptr;
 if((!existed&&truth(get(a,"claudeBridgeLeaseRecorded")))||(existed&&!claudeLeaseValid(old))||(expected&&!same(old,expected))){close(fd);return false;}
 Obj error=nullptr,data=send(cls("NSJSONSerialization"),"dataWithJSONObject:options:error:",lease,(UInt)0,&error);
 if(!data||send<UInt>(data,"length")>claudeMetadataMax){close(fd);return false;}
 Obj temporary=cat(cat(str(".claude-code-lease-"),uuid()),str(".tmp"));const char* temp=utf8(temporary);
 int out=openat(fd,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);bool ok=out>=0;
 if(ok){const char* bytes=send<const char*>(data,"bytes");UInt size=send<UInt>(data,"length"),done=0;
  while(done<size){long wrote=write(out,bytes+done,size-done);if(wrote<0&&errno==EINTR)continue;if(wrote<=0)break;done+=(UInt)wrote;}
  ok=done==size&&fsync(out)==0;close(out);
 }
 if(ok){Obj latestGeneration=nullptr;Obj latest=existed?claudeReadObject(fd,leaf,&latestGeneration):nullptr;
  ok=existed?same(old,latest)&&same(generation,latestGeneration):fstatat(fd,leaf,&st,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT;
 }
 if(ok)ok=renameat(fd,temp,fd,leaf)==0&&fsync(fd)==0;
 if(!ok)unlinkat(fd,temp,0);close(fd);return ok;
}
Obj claudeLeaseProcess(Obj a,Obj lease){
 int pid=(int)integer(get(lease,"pid"));if(pid<=0)return nullptr;
 Obj r=send(cls("NSRunningApplication"),"runningApplicationWithProcessIdentifier:",pid);if(!r||send<bool>(r,"isTerminated"))return nullptr;
 Obj path=send(send(r,"bundleURL"),"path");double started=processStarted(r);if(!path||!same(canonical(path),canonical(get(a,"path")))||started<=0)return nullptr;
 double dt=started-send<double>(get(lease,"started"),"doubleValue");return dt>-.1&&dt<.1?r:nullptr;
}
bool claudeCompetingDesktop(Obj target){
 Obj all=send(workspace,"runningApplications");int pid=target?send<int>(target,"processIdentifier"):0;
 for(UInt i=0;i<count(all);++i){Obj r=at(all,i);if(send<int>(r,"processIdentifier")!=pid&&same(send(r,"bundleIdentifier"),str("com.anthropic.claudefordesktop"))&&!send<bool>(r,"isTerminated"))return true;}return false;
}
Obj claudeLaunchGate(Obj p){
 if(!claudeGroup(appFor(p))||claudeCleanProfile(p))return nullptr;
 for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);if(!claudeGroup(a))continue;bool valid;Obj lease=claudeReadLease(a,valid);
  if(!valid)return str(T("The Claude session reservation is unreadable. Keep its file and check AppDeck's Shared folder before launching another profile.","Не удалось прочитать резервирование сессии Claude. Сохраните файл и проверьте папку Shared AppDeck перед запуском другого профиля."));
  if(!deck::claudeLeaseAllows(claudeLeaseProcess(a,lease)!=nullptr,same(get(lease,"profileId"),get(p,"id")),false))return str(T("Another Claude profile owns the shared session. Quit that Claude instance before opening a different account.","Общая сессия открыта в другом профиле Claude. Завершите тот экземпляр Claude перед открытием другого аккаунта."));
 }return nullptr;
}
bool claudeAcknowledged(Obj records,Obj lease){
 Obj before=get(lease,"before");for(UInt i=0;i<count(records);++i){Obj r=at(records,i);
  if(same(get(r,"profileId"),get(lease,"profileId"))&&same(get(r,"id"),get(lease,"id"))&&same(get(r,"cwd"),get(lease,"cwd"))&&!truth(get(r,"archived"))&&!same(get(before,utf8(get(r,"metadataPath"))),get(r,"generation")))return true;
 }return false;
}
Obj claudeLeaseStatus(Obj a){
 bool valid;Obj lease=claudeReadLease(a,valid);if(!valid)return str(T("Session reservation could not be read; opening is blocked.","Резервирование сессии не прочитано; открытие заблокировано."));
 if(!claudeLeaseProcess(a,lease))return str(T("Local Code sessions only. Cloud Chat projects remain account-specific.","Только локальные сессии Code. Облачные проекты Chat остаются отдельными для каждого аккаунта."));
 Obj s=get(lease,"status");if(same(s,str("confirmed")))return str(T("Claude registered the session. Quit that profile before switching accounts.","Claude зарегистрировал сессию. Завершите этот профиль перед сменой аккаунта."));
 if(same(s,str("pending")))return str(T("Waiting for Claude to register the session in the chosen profile…","Ожидание регистрации сессии Claude в выбранном профиле…"));
 return str(T("Registration was not confirmed. Check Claude's sign-in or trust dialog, then retry.","Регистрация не подтверждена. Проверьте вход или запрос доверия в Claude и повторите."));
}
void claudeSessionPump(){
 if(previewMode)return;bool changed=false;
 for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);if(!claudeGroup(a))continue;bool valid;Obj lease=claudeReadLease(a,valid);
  if(!valid||!same(get(lease,"status"),str("pending"))||!claudeLeaseProcess(a,lease))continue;
  Obj p=nullptr;for(UInt j=0;j<count(profiles);++j)if(same(get(at(profiles,j),"id"),get(lease,"profileId"))){p=at(profiles,j);break;}if(!p)continue;
  ClaudeScanBudget budget;Obj records=claudeScanProfile(p,budget);const char* status=budget.complete&&!budget.skipped&&claudeAcknowledged(records,lease)?"confirmed":nullptr;
  double now=send<double>(send(cls("NSDate"),"date"),"timeIntervalSince1970");if(!status&&now-send<double>(get(lease,"requestedAt"),"doubleValue")>45)status="timeout";
  if(status){Obj next=autoRelease(send(lease,"mutableCopy"));put(next,"status",str(status));if(claudeWriteLease(a,next,lease)){changed=true;note(status[0]=='c'?"Claude session registration observed in target metadata.":"Claude session registration was not confirmed before the deadline.");}}
 }
 if(changed&&claudeStatusLabel&&claudeGroup(currentApp()))send<void>(claudeStatusLabel,"setStringValue:",claudeLeaseStatus(currentApp()));
}
bool claudeBridgeSupported(Obj a){
 Obj bundle=send(cls("NSBundle"),"bundleWithPath:",get(a,"path")),info=send(bundle,"infoDictionary");
 return same(get(info,"CFBundleIdentifier"),str("com.anthropic.claudefordesktop"))&&isClass(get(info,"CFBundleShortVersionString"),"NSString")&&deck::claudeResumeVersion(utf8(get(info,"CFBundleShortVersionString")));
}
bool claudeTranscriptPresent(Obj id){
 if(!isClass(id,"NSString")||!deck::uuid(utf8(id)))return false;
 int root=claudeOpenDirectory(join(home,".claude/projects"));if(root<0)return false;ClaudeScanBudget budget;Obj names=claudeDirectoryNames(root,budget);bool found=false;
 Obj leaf=cat(id,str(".jsonl"));for(UInt i=0;i<count(names)&&budget.complete&&!found;++i){const char* name=utf8(at(names,i));if(!deck::safeLeaf(name))continue;
  int dir=openat(root,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)continue;struct stat st{};
  found=fstatat(dir,utf8(leaf),&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISREG(st.st_mode)&&st.st_nlink==1&&st.st_size>0;close(dir);
 }close(root);return found;
}
Obj claudeResumeEvent(int pid,Obj id){
 char link[80]{};if(pid<=0||!isClass(id,"NSString")||!deck::claudeResumeURL(utf8(id),link,sizeof link))return nullptr;
 Obj target=send(cls("NSAppleEventDescriptor"),"descriptorWithProcessIdentifier:",pid);if(!target)return nullptr;
 Obj event=send(cls("NSAppleEventDescriptor"),"appleEventWithEventClass:eventID:targetDescriptor:returnID:transactionID:",(unsigned)0x4755524c,(unsigned)0x4755524c,target,(short)-1,(int)0);
 if(event)send<void>(event,"setParamDescriptor:forKeyword:",send(cls("NSAppleEventDescriptor"),"descriptorWithString:",str(link)),(unsigned)0x2d2d2d2d);return event;
}
bool claudeSendResume(Obj target,Obj id){
 Obj event=claudeResumeEvent(send<int>(target,"processIdentifier"),id),error=nullptr;if(!event)return false;
 send(event,"sendEventWithOptions:timeout:error:",(UInt)(0x1|0x10),2.0,&error);return !error;
}
Obj claudeMakeLease(Obj p,Obj entry,int pid,double started,Obj before,double now){
 Obj lease=dict(),generations=dict();for(UInt i=0;i<count(before);++i){Obj r=at(before,i);if(same(get(r,"id"),get(entry,"id")))put(generations,utf8(get(r,"metadataPath")),get(r,"generation"));}
 put(lease,"version",num(1));put(lease,"id",get(entry,"id"));put(lease,"cwd",get(entry,"cwd"));put(lease,"profileId",get(p,"id"));put(lease,"pid",num(pid));put(lease,"started",real(started));
 put(lease,"requestedAt",real(now));put(lease,"before",generations);put(lease,"status",str("pending"));return lease;
}
