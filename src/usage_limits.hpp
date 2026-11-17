// Included inside main.cpp's private namespace, after workspace_base.hpp.
// Account limits come from Codex itself. The registered app bundle's own `codex app-server` is
// started with the profile's CODEX_HOME and asked two documented questions over stdio JSON-RPC:
// account/read and account/rateLimits/read. The request to OpenAI is made by that binary, exactly
// as the profile's window does. AppDeck never opens auth.json, holds no token and has no network
// client of its own. Opt-in per group; one probe at a time; each profile at most once an hour and
// automatic probes at least five minutes apart (usage_policy.hpp). Confirmed Claude identities on
// the ordinary system route also reuse successful quota replies after a fresh auth status check.
//
// The probe is an ordinary second Codex client of that profile (like the CLI or an IDE extension
// next to the Desktop window): same CODEX_HOME and the same database folder the window uses. Do
// NOT point it at an empty CODEX_SQLITE_HOME "for isolation": a fresh state database makes Codex
// re-index every rollout under CODEX_HOME and startup then times out (tried and rejected; see docs/ARCHITECTURE.md).
#include "usage_probe.hpp"
Obj usageInFlight=nullptr,usageForced=nullptr; // profile id being asked; ids queued by a manual refresh
Obj usageAccountCache=nullptr; // volatile successful Claude quotas; never serialized to state.plist
Obj usageDisplayAccounts=nullptr; // last-good per-profile identity, independent of reusable quotas
double usageLastProbe=0; // unix time the last helper process was started (any profile): spacing of automatic probes
double usageStarted=0;
double nowUnix(){return send<double>(send(cls("NSDate"),"date"),"timeIntervalSince1970");}
// Claude: limits are an ACCOUNT property and Claude Code can report them (`get_usage`, the control
// request its own docs describe "for callers that need only the plan rate limits, such as a usage
// meter"). Claude Desktop hands its sign-in to the embedded Claude Code in memory, so there is nothing
// on disk to ask with. AppDeck therefore keeps one dedicated Claude Code config folder per profile
// ("meter", inside AppDeck's data, never ~/.claude); the owner signs it in once with the CLI's own
// `claude auth login`. Tokens stay with Claude Code (Keychain); AppDeck only runs the CLI.
bool usageIsClaude(Obj a){return a&&deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Claude;}
bool usageCapable(Obj a){return a&&(deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Codex||usageIsClaude(a));}
bool usageEnabled(Obj a){return usageCapable(a)&&truth(get(a,"usageLimits"));}
bool usageExecutable(Obj b){return b&&!directory(canonical(b))&&send<bool>(fileManager,"isExecutableFileAtPath:",b);}
Obj claudeBinary(){
 for(const char* fixed:{".local/bin/claude"}){Obj b=join(home,fixed);if(usageExecutable(b))return b;}
 // The copy Claude Desktop downloads for its Code tab: newest version folder.
 Obj root=join(home,"Library/Application Support/Claude/claude-code");Obj e=nullptr;Obj versions=send(fileManager,"contentsOfDirectoryAtPath:error:",root,&e),best=nullptr,bestName=nullptr;
 for(UInt i=0;i<count(versions);++i){Obj b=join(join(root,at(versions,i)),"claude.app/Contents/MacOS/claude");if(!usageExecutable(b))continue;
  if(!bestName||send<Int>(at(versions,i),"compare:options:",bestName,(UInt)64)>0){best=b;bestName=at(versions,i);}} // NSNumericSearch
 if(best)return best;
 for(const char* fixed:{"/opt/homebrew/bin/claude","/usr/local/bin/claude"})if(usageExecutable(str(fixed)))return str(fixed);
 return nullptr;
}
// ChatGPT 26.924 moved the helper into codex-cli/, next to codex-package.json; the desktop app
// itself spawns the Mach-O inside CodexCLI.app. Earlier builds ship Contents/Resources/codex.
Obj codexBinary(Obj a){
 for(const char* rel:{"Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex","Contents/Resources/codex"}){Obj b=join(get(a,"path"),rel);if(usageExecutable(b))return b;}
 return nullptr;
}
Obj usageBinary(Obj a){return usageIsClaude(a)?claudeBinary():codexBinary(a);}
Obj claudeMeterDir(Obj p){if(!deck::uuid(utf8(get(p,"id"))))return nullptr;Obj root=join(dataRoot,"ClaudeMeter"),dir=join(root,get(p,"id"));return mkdirPrivate(root)&&mkdirPrivate(dir)?dir:nullptr;}
Obj usageHome(Obj p){Obj a=appFor(p);if(isMaster(p))return baseSource(a)?baseSource(a):join(home,".codex");Obj r=profileRoot(p);return r?join(r,"codex"):nullptr;}
// Working directory only: an empty AppDeck folder, so no project-level .codex config or git
// repository is ever discovered from the probe's cwd.
Obj usageScratch(){Obj cache=join(dataRoot,"Cache"),dir=join(cache,"usage-probe");return mkdirPrivate(cache)&&mkdirPrivate(dir)?dir:nullptr;}

// ---- worker thread: see usage_probe.hpp; nothing of AppDeck's state is touched there ----
void usageWorker(Obj,Sel,Obj request){
 Pool pool;Obj result=dict();put(result,"profile",get(request,"profile"));put(result,"cacheEligible",get(request,"cacheEligible"));
 if(same(get(request,"kind"),str("claude")))usageProbeClaude(request,result,true);else usageProbeCodex(request,result);
 // Vendor output can contain account identifiers or credential-bearing URLs. Keep categories only.
 erase(result,"detail");
 send<void>(controller,"performSelectorOnMainThread:withObject:waitUntilDone:",sel("usageFinished:"),result,false);
}

// ---- main thread ----
Obj usageOf(Obj p){Obj u=get(p,"usage");return isClass(u,"NSDictionary")?u:nullptr;}
Obj usageWindowsOf(Obj p){Obj w=get(usageOf(p),"windows");return isClass(w,"NSArray")?w:nullptr;}
Obj usageWindowAt(Obj p,UInt i){Obj w=usageWindowsOf(p);Obj r=i<count(w)?at(w,i):nullptr;return isClass(r,"NSDictionary")&&isClass(get(r,"used"),"NSNumber")?r:nullptr;}
int usageRemaining(Obj window){return deck::remainingPercent(send<double>(get(window,"used"),"doubleValue"));}
double usageResets(Obj window){return send<double>(get(window,"resets"),"doubleValue");}
// Sharing only a successful quota reply cannot establish another profile's proxy health. Keep
// profile-specific and unresolved routes independent, including inherited proxy variables.
bool usageCacheDirect(Obj p,Obj env=nullptr){
 if(!usageIsClaude(appFor(p))||proxyFor(p)||truth(get(p,"proxyRequired")))return false;
 Obj choice=get(p,"proxy");if(choice&&!same(choice,str("none"))&&!same(choice,str("default")))return false;
 if(!same(choice,str("none"))&&get(state,"defaultProxy"))return false;
 for(const char* key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy","grpc_proxy","CLAUDE_CODE_HTTP_PROXY","CLAUDE_CODE_HTTPS_PROXY"})
  if(Obj value=get(env,key))if(send<UInt>(value,"length"))return false;
 return true;
}
Obj usageCacheProfile(Obj id){for(UInt i=0;i<count(profiles);++i)if(same(get(at(profiles,i),"id"),id))return at(profiles,i);return nullptr;}
// Prune using current profile settings, not a persisted identity hint. The snapshot belongs only
// to this worker; a subsequent profile edit cannot mutate it while auth status is running.
void usageCachePrepare(Obj p,Obj env,Obj request,bool manual){
 bool eligible=usageCacheDirect(p,env);put(request,"cacheEligible",boolean(eligible));put(request,"manual",boolean(manual));
 put(request,"reuseAllowed",boolean(eligible&&!get(usageOf(p),"error")));Obj rows=array();double now=nowUnix();
 Obj keys=send(send(usageAccountCache,"allKeys"),"copy");
 for(UInt i=0;i<count(keys);++i){Obj id=at(keys,i),row=get(usageAccountCache,utf8(id)),source=usageCacheProfile(id),u=source?usageOf(source):nullptr;
  bool valid=source&&usageEnabled(appFor(source))&&usageCacheDirect(source)&&!get(u,"error")&&same(get(u,"windowsAt"),get(row,"windowsAt"));
  if(!valid)erase(usageAccountCache,utf8(id));
  else if(eligible&&count(rows)<256&&deck::usageCacheFresh(now,send<double>(get(row,"windowsAt"),"doubleValue"),false))add(rows,row);
 }
 drop(keys);put(request,"cachedAccounts",send(cls("NSArray"),"arrayWithArray:",rows));
}
void usageCacheRemember(Obj p,Obj result,Obj failure,double fetched){
 Obj id=get(p,"id"),account=get(result,"account");
 if(failure){
  // A failed or changed sign-in must not keep sharing a formerly successful account snapshot.
  Obj previousAccount=keep(get(get(usageAccountCache,utf8(id)),"account")),keys=send(send(usageAccountCache,"allKeys"),"copy");
  for(UInt i=0;i<count(keys);++i){Obj key=at(keys,i),row=get(usageAccountCache,utf8(key));
   if(same(key,id)||usageCacheSameAccount(get(row,"account"),account)||usageCacheSameAccount(get(row,"account"),previousAccount))erase(usageAccountCache,utf8(key));}
  drop(keys);drop(previousAccount);return;
 }
 erase(usageAccountCache,utf8(id));
 if(!truth(get(result,"cacheEligible"))||!usageCacheDirect(p)||!usageCacheAccount(account)||!count(get(get(result,"limits"),"windows")))return;
 if(!usageAccountCache)usageAccountCache=keep(dict());if(count(usageAccountCache)>=256)send<void>(usageAccountCache,"removeAllObjects");
 Obj row=dict();put(row,"account",account);put(row,"limits",get(result,"limits"));put(row,"windowsAt",real(fetched));
 put(usageAccountCache,utf8(id),send(cls("NSDictionary"),"dictionaryWithDictionary:",row));
}
// Keeping this profile's explicitly stale display does not authorize sharing its quota. Remember
// its last successful identity on every route, even when no reusable account cache is permitted.
bool usageDisplaySameAccount(Obj p,Obj account,Obj old){
 if(!account)return true;
 if(!truth(get(account,"signedIn")))return false;
 bool claude=usageIsClaude(appFor(p));Obj previous=claude?get(usageDisplayAccounts,utf8(get(p,"id"))):nullptr;
 if(previous&&same(get(previous,"windowsAt"),get(old,"windowsAt")))return usageCacheSameAccount(account,get(previous,"account"));
 for(const char* key:{"email","kind","plan"})if(!same(get(account,key),get(old,key)))return false;
 if(!claude)return true;
 if(!usageCacheText(get(account,"email"),254)||!usageCacheText(get(account,"kind"),40)||!usageCacheText(get(account,"plan"),40))return false;
 // After restart the persisted labels can retain only this profile's stale values. Organization
 // metadata is intentionally not persisted, so this fallback never establishes a shared identity.
 return true;
}
void usageDisplayRemember(Obj p,Obj account,bool fresh){
 Obj keys=send(send(usageDisplayAccounts,"allKeys"),"copy");
 for(UInt i=0;i<count(keys);++i){Obj id=at(keys,i),row=get(usageDisplayAccounts,utf8(id)),source=usageCacheProfile(id),u=source?usageOf(source):nullptr;
  if(!source||!usageEnabled(appFor(source))||!count(get(u,"windows"))||!same(get(u,"windowsAt"),get(row,"windowsAt")))erase(usageDisplayAccounts,utf8(id));}
 drop(keys);if(!fresh)return;
 Obj id=get(p,"id");erase(usageDisplayAccounts,utf8(id));
 if(!usageIsClaude(appFor(p))||!usageCacheAccount(account)||!count(usageWindowsOf(p)))return;
 if(!usageDisplayAccounts)usageDisplayAccounts=keep(dict());
 Obj row=dict();put(row,"account",send(cls("NSDictionary"),"dictionaryWithDictionary:",account));put(row,"windowsAt",get(usageOf(p),"windowsAt"));
 put(usageDisplayAccounts,utf8(id),row);
}
// A cached record must never be able to break the UI: state.plist can be edited by hand.
void usageSanitize(Obj p){
 Obj u=get(p,"usage");if(!u)return;if(!isClass(u,"NSDictionary")){erase(p,"usage");return;}
 for(const char* k:{"checked","windowsAt"})if(get(u,k)&&!isClass(get(u,k),"NSNumber"))erase(u,k);
 for(const char* k:{"email","plan","kind","error","detail","reached"})if(get(u,k)&&!isClass(get(u,k),"NSString"))erase(u,k);
 Obj w=get(u,"windows");bool ok=!w||isClass(w,"NSArray");
 for(UInt i=0;ok&&i<count(w);++i){Obj r=at(w,i);ok=isClass(r,"NSDictionary")&&isClass(get(r,"used"),"NSNumber")&&(!get(r,"minutes")||isClass(get(r,"minutes"),"NSNumber"))&&(!get(r,"resets")||isClass(get(r,"resets"),"NSNumber"))&&(!get(r,"label")||(isClass(get(r,"label"),"NSString")&&send<UInt>(get(r,"label"),"length")<=24));}
 if(!ok||count(w)>4)erase(u,"windows");
}
void usageStartClaude(Obj p,Obj binary,Obj scratch,bool manual){
 Obj meter=claudeMeterDir(p);Obj u=dict();put(u,"checked",real(nowUnix()));if(!meter){put(u,"error",str("unsupported"));put(p,"usage",u);return;}
 Obj env=cleanEnvironment();Obj keys=send(send(env,"allKeys"),"copy");
 // No identity or session state of any other Claude Code may leak into the meter.
 for(UInt i=0;i<count(keys);++i){const char* k=utf8(at(keys,i));if(deck::prefix(k,"CLAUDE_CODE_")||deck::prefix(k,"CLAUDECODE")||deck::prefix(k,"ANTHROPIC_")||deck::prefix(k,"CLAUDE_AGENT_SDK")||deck::equal(k,"CLAUDE_CONFIG_DIR"))send<void>(env,"removeObjectForKey:",at(keys,i));}
 drop(keys);put(env,"CLAUDE_CONFIG_DIR",canonical(meter));
 for(const char* key:{"DISABLE_GROWTHBOOK","DO_NOT_TRACK","grpc_proxy","CLAUDE_CODE_HTTP_PROXY","CLAUDE_CODE_HTTPS_PROXY"})erase(env,key);
 // Narrow opt-outs only. CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC must NOT be set: Claude Code counts the
 // usage request itself as non-essential and answers get_usage with rate_limits:null (0.7.0 did exactly that).
 for(const char* off:{"DISABLE_AUTOUPDATER","DISABLE_ERROR_REPORTING","DISABLE_TELEMETRY"})put(env,off,str("1"));
 if(!usageProxy(p,env))return; // behind a proxy: through its bridge, or not at all
 Obj request=dict();put(request,"profile",get(p,"id"));put(request,"kind",str("claude"));put(request,"binary",binary);put(request,"scratch",scratch);put(request,"environment",env);
 usageCachePrepare(p,env,request,manual);
 drop(usageInFlight);usageInFlight=keep(get(p,"id"));usageStarted=usageUptime();usageLastProbe=nowUnix();
 send<void>(cls("NSThread"),"detachNewThreadSelector:toTarget:withObject:",sel("usageWorker:"),controller,request);
}
void usageStart(Obj p,bool manual){
 Obj a=appFor(p),binary=usageBinary(a),codexHome=usageHome(p),scratch=usageScratch();Obj u=dict();put(u,"checked",real(nowUnix()));
 if(usageIsClaude(a)){if(!binary||!scratch){put(u,"error",str("nocli"));put(p,"usage",u);return;}usageStartClaude(p,binary,scratch,manual);return;}
 // Nothing to ask: no codex helper in this bundle, or a copy that has never been started (no login can exist).
 if(!binary||!scratch){put(u,"error",str("unsupported"));put(p,"usage",u);return;}
 if(!codexHome||!directory(canonical(codexHome))){put(u,"error",str("auth"));put(p,"usage",u);return;}
 Obj env=cleanEnvironment();put(env,"CODEX_HOME",canonical(codexHome));put(env,"CODEX_INTERNAL_APP_SERVER_REMOTE_CONTROL_DISABLED",str("1"));
 // Same database folder as the profile's own window: the shared base when history is shared.
 if(sharesHistory(p))put(env,"CODEX_SQLITE_HOME",canonical(baseSource(a)));
 if(!usageProxy(p,env))return; // behind a proxy: through its bridge, or not at all
 Obj request=dict();put(request,"profile",get(p,"id"));put(request,"binary",binary);put(request,"scratch",scratch);put(request,"environment",env);
 drop(usageInFlight);usageInFlight=keep(get(p,"id"));usageStarted=usageUptime();usageLastProbe=nowUnix();
 send<void>(cls("NSThread"),"detachNewThreadSelector:toTarget:withObject:",sel("usageWorker:"),controller,request);
}
void usageRefreshViews();
// Called from the status tick and after a manual refresh. Picks the most overdue account, one at a time.
void usagePump(){
 if(previewMode||quitting||!profiles)return;
 // Claude's two bounded subprocesses can together exceed one subprocess timeout. Never start a
 // second worker merely because the first one's auth + quota exchange is still completing.
 if(usageInFlight)return;
 double now=nowUnix(),bestAge=-1;Obj best=nullptr;bool bestManual=false;
 for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);if(!usageEnabled(appFor(p)))continue;
  Obj u=usageOf(p);double checked=send<double>(get(u,"checked"),"doubleValue");bool forced=usageForced&&send<bool>(usageForced,"containsObject:",get(p,"id"));
  bool due=forced?deck::usageManualAllowed(now,checked):deck::usageDue(now,checked)&&deck::usageSpaced(now,usageLastProbe);
  if(forced&&!due)send<void>(usageForced,"removeObject:",get(p,"id")); // asked a moment ago: keep that answer
  if(!due)continue;double age=(forced?1e12:0)+(checked>0?now-checked:1e9);if(age>bestAge){bestAge=age;best=p;bestManual=forced;}
 }
 if(!best)return;if(usageForced)send<void>(usageForced,"removeObject:",get(best,"id"));
 usageStart(best,bestManual);if(!usageInFlight)save();usageRefreshViews();
}
void usageFinished(Obj,Sel,Obj result){
 Obj id=get(result,"profile"),p=nullptr;for(UInt i=0;i<count(profiles);++i)if(same(get(at(profiles,i),"id"),id)){p=at(profiles,i);break;}
 if(same(usageInFlight,id)){drop(usageInFlight);usageInFlight=nullptr;}
 if(!p||!usageEnabled(appFor(p)))return; // removed or switched off while the probe was out
 // A route edit while auth status was running invalidates permission to borrow another profile's
 // success. Leave the earlier display untouched; its original timestamp remains visible.
 if(truth(get(result,"reused"))&&!usageCacheDirect(p)){erase(usageAccountCache,utf8(id));usageRefreshViews();return;}
 Obj old=usageOf(p),u=dict(),account=get(result,"account"),limits=get(result,"limits"),failure=get(result,"error");double now=nowUnix();put(u,"checked",real(now));
 bool signedIn=account&&truth(get(account,"signedIn"));
 if(signedIn){put(u,"email",get(account,"email"));put(u,"plan",get(account,"plan"));put(u,"kind",get(account,"kind"));}
 if(!failure&&account&&!signedIn)failure=str("auth");else if(!failure&&signedIn&&same(get(account,"kind"),str("apiKey"))&&!count(get(limits,"windows")))failure=str("apikey");
 if(signedIn&&same(failure,str("noplan")))put(u,"plan",get(limits,"plan"));
 double fetched=truth(get(result,"reused"))?send<double>(get(result,"windowsAt"),"doubleValue"):now;
 if(!failure&&limits&&count(get(limits,"windows"))){put(u,"windows",get(limits,"windows"));put(u,"windowsAt",real(fetched));if(get(limits,"plan"))put(u,"plan",get(limits,"plan"));put(u,"reached",get(limits,"reached"));}
 else if(limits&&!failure)failure=str("nodata");
 if(failure){put(u,"error",failure);
  // Keep the last good numbers visible (marked stale) unless the account itself is gone.
  bool gone=same(failure,str("auth"))||same(failure,str("expired"))||same(failure,str("suspended"))||same(failure,str("apikey"))||same(failure,str("noplan"));
  if(!gone&&usageDisplaySameAccount(p,account,old)&&old&&get(old,"windows")){
   put(u,"windows",get(old,"windows"));put(u,"windowsAt",get(old,"windowsAt"));
   for(const char* key:{"email","kind","plan"})if(!get(u,key))put(u,key,get(old,key));}
  note(same(failure,str("auth"))?"Limits: profile is not signed in.":"Limits: probe did not return numbers (see card).");}
 put(p,"usage",u);usageDisplayRemember(p,account,!failure);usageCacheRemember(p,result,failure,fetched);save();usageRefreshViews();
}
