// Included inside main.cpp's private namespace, after workspace_base.hpp.
// Account limits come from Codex itself. The registered app bundle's own `codex app-server` is
// started with the profile's CODEX_HOME and asked two documented questions over stdio JSON-RPC:
// account/read and account/rateLimits/read. The request to OpenAI is made by that binary, exactly
// as the profile's window does. AppDeck never opens auth.json, holds no token and has no network
// client of its own. Opt-in per group; one probe at a time; an account at most once an hour and
// automatic probes at least five minutes apart (usage_policy.hpp).
//
// The probe is an ordinary second Codex client of that profile (like the CLI or an IDE extension
// next to the Desktop window): same CODEX_HOME and the same database folder the window uses. Do
// NOT point it at an empty CODEX_SQLITE_HOME "for isolation": a fresh state database makes Codex
// re-index every rollout under CODEX_HOME and startup then times out (tried and rejected; see docs/ARCHITECTURE.md).
#include "usage_probe.hpp"
Obj usageInFlight=nullptr,usageForced=nullptr; // profile id being asked; ids queued by a manual refresh
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
Obj usageBinary(Obj a){if(usageIsClaude(a))return claudeBinary();Obj b=join(get(a,"path"),"Contents/Resources/codex");return usageExecutable(b)?b:nullptr;}
Obj claudeMeterDir(Obj p){if(!deck::uuid(utf8(get(p,"id"))))return nullptr;Obj root=join(dataRoot,"ClaudeMeter"),dir=join(root,get(p,"id"));return mkdirPrivate(root)&&mkdirPrivate(dir)?dir:nullptr;}
Obj usageHome(Obj p){Obj a=appFor(p);if(isMaster(p))return baseSource(a)?baseSource(a):join(home,".codex");Obj r=profileRoot(p);return r?join(r,"codex"):nullptr;}
// Working directory only: an empty AppDeck folder, so no project-level .codex config or git
// repository is ever discovered from the probe's cwd.
Obj usageScratch(){Obj cache=join(dataRoot,"Cache"),dir=join(cache,"usage-probe");return mkdirPrivate(cache)&&mkdirPrivate(dir)?dir:nullptr;}

// ---- worker thread: see usage_probe.hpp; nothing of AppDeck's state is touched there ----
void usageWorker(Obj,Sel,Obj request){
 Pool pool;Obj result=dict();put(result,"profile",get(request,"profile"));
 if(same(get(request,"kind"),str("claude")))usageProbeClaude(request,result,true);else usageProbeCodex(request,result);
 Obj detail=get(result,"detail");if(detail&&send<UInt>(detail,"length")>200)put(result,"detail",send(detail,"substringToIndex:",(UInt)200));
 send<void>(controller,"performSelectorOnMainThread:withObject:waitUntilDone:",sel("usageFinished:"),result,false);
}

// ---- main thread ----
Obj usageOf(Obj p){Obj u=get(p,"usage");return isClass(u,"NSDictionary")?u:nullptr;}
Obj usageWindowsOf(Obj p){Obj w=get(usageOf(p),"windows");return isClass(w,"NSArray")?w:nullptr;}
Obj usageWindowAt(Obj p,UInt i){Obj w=usageWindowsOf(p);Obj r=i<count(w)?at(w,i):nullptr;return isClass(r,"NSDictionary")&&isClass(get(r,"used"),"NSNumber")?r:nullptr;}
int usageRemaining(Obj window){return deck::remainingPercent(send<double>(get(window,"used"),"doubleValue"));}
double usageResets(Obj window){return send<double>(get(window,"resets"),"doubleValue");}
// A cached record must never be able to break the UI: state.plist can be edited by hand.
void usageSanitize(Obj p){
 Obj u=get(p,"usage");if(!u)return;if(!isClass(u,"NSDictionary")){erase(p,"usage");return;}
 for(const char* k:{"checked","windowsAt"})if(get(u,k)&&!isClass(get(u,k),"NSNumber"))erase(u,k);
 for(const char* k:{"email","plan","kind","error","detail","reached"})if(get(u,k)&&!isClass(get(u,k),"NSString"))erase(u,k);
 Obj w=get(u,"windows");bool ok=!w||isClass(w,"NSArray");
 for(UInt i=0;ok&&i<count(w);++i){Obj r=at(w,i);ok=isClass(r,"NSDictionary")&&isClass(get(r,"used"),"NSNumber")&&(!get(r,"minutes")||isClass(get(r,"minutes"),"NSNumber"))&&(!get(r,"resets")||isClass(get(r,"resets"),"NSNumber"))&&(!get(r,"label")||(isClass(get(r,"label"),"NSString")&&send<UInt>(get(r,"label"),"length")<=24));}
 if(!ok||count(w)>4)erase(u,"windows");
}
void usageStartClaude(Obj p,Obj binary,Obj scratch){
 Obj meter=claudeMeterDir(p);Obj u=dict();put(u,"checked",real(nowUnix()));if(!meter){put(u,"error",str("unsupported"));put(p,"usage",u);return;}
 Obj env=cleanEnvironment();Obj keys=send(send(env,"allKeys"),"copy");
 // No identity or session state of any other Claude Code may leak into the meter.
 for(UInt i=0;i<count(keys);++i){const char* k=utf8(at(keys,i));if(deck::prefix(k,"CLAUDE_CODE_")||deck::prefix(k,"CLAUDECODE")||deck::prefix(k,"ANTHROPIC_")||deck::prefix(k,"CLAUDE_AGENT_SDK")||deck::equal(k,"CLAUDE_CONFIG_DIR"))send<void>(env,"removeObjectForKey:",at(keys,i));}
 drop(keys);put(env,"CLAUDE_CONFIG_DIR",canonical(meter));
 // Narrow opt-outs only. CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC must NOT be set: Claude Code counts the
 // usage request itself as non-essential and answers get_usage with rate_limits:null (0.7.0 did exactly that).
 for(const char* off:{"DISABLE_AUTOUPDATER","DISABLE_ERROR_REPORTING","DISABLE_TELEMETRY"})put(env,off,str("1"));
 if(!usageProxy(p,env))return; // behind a proxy: through its bridge, or not at all
 Obj request=dict();put(request,"profile",get(p,"id"));put(request,"kind",str("claude"));put(request,"binary",binary);put(request,"scratch",scratch);put(request,"environment",env);
 drop(usageInFlight);usageInFlight=keep(get(p,"id"));usageStarted=usageUptime();usageLastProbe=nowUnix();
 send<void>(cls("NSThread"),"detachNewThreadSelector:toTarget:withObject:",sel("usageWorker:"),controller,request);
}
void usageStart(Obj p){
 Obj a=appFor(p),binary=usageBinary(a),codexHome=usageHome(p),scratch=usageScratch();Obj u=dict();put(u,"checked",real(nowUnix()));
 if(usageIsClaude(a)){if(!binary||!scratch){put(u,"error",str("nocli"));put(p,"usage",u);return;}usageStartClaude(p,binary,scratch);return;}
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
 if(usageInFlight){if(usageUptime()-usageStarted<deck::usageProbeTimeout+15)return;drop(usageInFlight);usageInFlight=nullptr;} // lost worker
 double now=nowUnix(),bestAge=-1;Obj best=nullptr;
 for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);if(!usageEnabled(appFor(p)))continue;
  Obj u=usageOf(p);double checked=send<double>(get(u,"checked"),"doubleValue");bool forced=usageForced&&send<bool>(usageForced,"containsObject:",get(p,"id"));
  bool due=forced?deck::usageManualAllowed(now,checked):deck::usageDue(now,checked)&&deck::usageSpaced(now,usageLastProbe);
  if(forced&&!due)send<void>(usageForced,"removeObject:",get(p,"id")); // asked a moment ago: keep that answer
  if(!due)continue;double age=(forced?1e12:0)+(checked>0?now-checked:1e9);if(age>bestAge){bestAge=age;best=p;}
 }
 if(!best)return;if(usageForced)send<void>(usageForced,"removeObject:",get(best,"id"));
 usageStart(best);if(!usageInFlight)save();usageRefreshViews();
}
void usageFinished(Obj,Sel,Obj result){
 Obj id=get(result,"profile"),p=nullptr;for(UInt i=0;i<count(profiles);++i)if(same(get(at(profiles,i),"id"),id)){p=at(profiles,i);break;}
 if(same(usageInFlight,id)){drop(usageInFlight);usageInFlight=nullptr;}
 if(!p||!usageEnabled(appFor(p)))return; // removed or switched off while the probe was out
 Obj old=usageOf(p),u=dict(),account=get(result,"account"),limits=get(result,"limits"),failure=get(result,"error");double now=nowUnix();put(u,"checked",real(now));
 bool signedIn=account&&truth(get(account,"signedIn"));
 if(signedIn){put(u,"email",get(account,"email"));put(u,"plan",get(account,"plan"));put(u,"kind",get(account,"kind"));}
 if(account&&!signedIn)failure=str("auth");else if(signedIn&&same(get(account,"kind"),str("apiKey"))&&!count(get(limits,"windows")))failure=str("apikey");
 if(signedIn&&same(failure,str("noplan")))put(u,"plan",get(limits,"plan"));
 if(limits&&count(get(limits,"windows"))){put(u,"windows",get(limits,"windows"));put(u,"windowsAt",real(now));if(get(limits,"plan"))put(u,"plan",get(limits,"plan"));put(u,"reached",get(limits,"reached"));failure=nullptr;}
 else if(limits&&!failure)failure=str("nodata");
 if(failure){put(u,"error",failure);put(u,"detail",get(result,"detail"));
  // Keep the last good numbers visible (marked stale) unless the account itself is gone.
  bool gone=same(failure,str("auth"))||same(failure,str("apikey"))||same(failure,str("noplan"));
  if(!gone&&old&&get(old,"windows")){put(u,"windows",get(old,"windows"));put(u,"windowsAt",get(old,"windowsAt"));if(!get(u,"plan"))put(u,"plan",get(old,"plan"));if(!get(u,"email"))put(u,"email",get(old,"email"));}
  note(same(failure,str("auth"))?"Limits: profile is not signed in.":"Limits: probe did not return numbers (see card).");}
 put(p,"usage",u);save();usageRefreshViews();
}
