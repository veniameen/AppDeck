// Included where mac:: is already in scope (main.cpp's private namespace, the API smoke test).
// Turns replies of Codex's own `codex app-server` (account/read, account/rateLimits/read) into
// AppDeck's small display record: percentages, reset times, plan, account label. The payloads
// contain no credentials and none are ever requested.
#include "usage_policy.hpp"
bool usageKind(Obj o,const char* name){return o&&send<bool>(o,"isKindOfClass:",cls(name));}
Obj usageJSON(const char* bytes,Size length){
 if(!bytes||!length||length>1024*1024)return nullptr;Obj data=send(cls("NSData"),"dataWithBytes:length:",bytes,(UInt)length);Obj e=nullptr;
 Obj o=send(cls("NSJSONSerialization"),"JSONObjectWithData:options:error:",data,(UInt)0,&e);return usageKind(o,"NSDictionary")?o:nullptr;
}
Obj usageWindowRecord(Obj w){
 if(!usageKind(w,"NSDictionary"))return nullptr;Obj used=get(w,"usedPercent");if(!usageKind(used,"NSNumber"))return nullptr;
 Obj r=dict();put(r,"used",real(send<double>(used,"doubleValue")));
 Obj minutes=get(w,"windowDurationMins");if(usageKind(minutes,"NSNumber"))put(r,"minutes",num(integer(minutes)));
 Obj resets=get(w,"resetsAt");if(usageKind(resets,"NSNumber"))put(r,"resets",real(send<double>(resets,"doubleValue")));
 return r;
}
// One metered limit -> its windows, weekly first. `primary`/`secondary` carry no meaning of their
// own: a Pro account reports the weekly window as `primary` and has no short one.
Obj usageWindows(Obj snapshot){
 Obj out=array();if(!usageKind(snapshot,"NSDictionary"))return out;
 for(const char* key:{"primary","secondary"}){Obj r=usageWindowRecord(get(snapshot,key));if(r)add(out,r);}
 auto weekly=[](Obj r){return deck::usageWindow(integer(get(r,"minutes")))==deck::UsageWindow::Weekly;};
 if(count(out)==2&&weekly(at(out,1))&&!weekly(at(out,0)))send<void>(out,"exchangeObjectAtIndex:withObjectAtIndex:",(UInt)0,(UInt)1);
 return out;
}
// result of account/rateLimits/read -> {windows:[{used,minutes,resets}], plan, reached}
Obj usageRecord(Obj result){
 Obj r=dict();if(!usageKind(result,"NSDictionary")){put(r,"windows",array());return r;}
 Obj buckets=get(result,"rateLimitsByLimitId"),single=get(result,"rateLimits"),chosen=nullptr;
 if(usageKind(buckets,"NSDictionary")&&count(usageWindows(get(buckets,"codex"))))chosen=get(buckets,"codex");
 if(!chosen&&count(usageWindows(single)))chosen=single;
 if(!chosen&&usageKind(buckets,"NSDictionary")){Obj all=send(buckets,"allValues");for(UInt i=0;i<count(all)&&!chosen;++i)if(count(usageWindows(at(all,i))))chosen=at(all,i);}
 if(!chosen&&usageKind(single,"NSDictionary"))chosen=single; // may carry only the plan name
 put(r,"windows",usageWindows(chosen));
 Obj plan=get(chosen,"planType");if(usageKind(plan,"NSString")&&send<UInt>(plan,"length")<=40)put(r,"plan",plan);
 Obj reached=get(chosen,"rateLimitReachedType");if(usageKind(reached,"NSString")&&send<UInt>(reached,"length")<=80)put(r,"reached",reached);
 return r;
}
// result of account/read -> {signedIn, kind, email, plan}
Obj usageAccount(Obj result){
 Obj r=dict();Obj account=usageKind(result,"NSDictionary")?get(result,"account"):nullptr;bool in=usageKind(account,"NSDictionary");put(r,"signedIn",boolean(in));if(!in)return r;
 Obj kind=get(account,"type");if(usageKind(kind,"NSString")&&send<UInt>(kind,"length")<=40)put(r,"kind",kind);
 Obj email=get(account,"email");if(usageKind(email,"NSString")&&send<UInt>(email,"length")<=254)put(r,"email",email);
 Obj plan=get(account,"planType");if(usageKind(plan,"NSString")&&send<UInt>(plan,"length")<=40)put(r,"plan",plan);
 return r;
}
// ---- Claude Code: `claude auth status` and the `get_usage` control reply (SDK stream-json) ----
// ISO 8601 -> unix seconds; the server sends both "…T08:00:00Z" and "…T08:00:00.123456+00:00".
double usageISO8601(Obj text){
 if(!usageKind(text,"NSString")||!send<UInt>(text,"length")||send<UInt>(text,"length")>64)return 0;
 for(UInt options:{(UInt)1907,(UInt)(1907|2048)}){ // NSISO8601DateFormatWithInternetDateTime, + WithFractionalSeconds
  Obj f=autoRelease(make("NSISO8601DateFormatter"));send<void>(f,"setFormatOptions:",options);Obj date=send(f,"dateFromString:",text);
  if(date)return send<double>(date,"timeIntervalSince1970");}
 return 0;
}
Obj usageClaudeWindow(Obj w,long minutes,Obj label,const char* percentKey="utilization"){
 if(!usageKind(w,"NSDictionary"))return nullptr;Obj used=get(w,percentKey);if(!usageKind(used,"NSNumber"))return nullptr;
 Obj r=dict();put(r,"used",real(send<double>(used,"doubleValue")));put(r,"minutes",num(minutes));
 double resets=usageISO8601(get(w,"resets_at"));if(resets>0)put(r,"resets",real(resets));
 if(usageKind(label,"NSString")&&send<UInt>(label,"length")&&send<UInt>(label,"length")<=24)put(r,"label",label);
 return r;
}
// body of the get_usage reply -> {windows:[weekly all models, 5 hours, per-model weekly…], plan, available}
Obj usageRecordClaude(Obj body){
 Obj r=dict(),windows=array();put(r,"windows",windows);if(!usageKind(body,"NSDictionary"))return r;
 Obj plan=get(body,"subscription_type");if(usageKind(plan,"NSString")&&send<UInt>(plan,"length")<=40)put(r,"plan",plan);
 Obj available=get(body,"rate_limits_available");put(r,"available",boolean(usageKind(available,"NSNumber")&&truth(available)));
 Obj limits=get(body,"rate_limits");if(!usageKind(limits,"NSDictionary"))return r;
 // Preferred: `limits[]`, the server's list in the very terms of Claude's own "Plan usage limits" screen —
 // kind session (5 hours) / weekly_all / weekly_scoped (scope.model.display_name, e.g. "Fable"). It does not
 // depend on client-side feature flags, unlike `model_scoped`, which disappears when telemetry is off.
 Obj rows=get(limits,"limits");
 if(usageKind(rows,"NSArray")){Obj week=nullptr,session=nullptr,scoped=array();
  for(UInt i=0;i<count(rows)&&i<32;++i){Obj row=at(rows,i);if(!usageKind(row,"NSDictionary"))continue;Obj kind=get(row,"kind");if(!usageKind(kind,"NSString"))continue;
   if(!week&&!strcmp(utf8(kind),"weekly_all"))week=usageClaudeWindow(row,10080,nullptr,"percent");
   else if(!session&&!strcmp(utf8(kind),"session"))session=usageClaudeWindow(row,300,nullptr,"percent");
   else if(!strcmp(utf8(kind),"weekly_scoped")){Obj scope=get(row,"scope"),model=usageKind(scope,"NSDictionary")?get(scope,"model"):nullptr;
    Obj w=usageClaudeWindow(row,10080,usageKind(model,"NSDictionary")?get(model,"display_name"):nullptr,"percent");if(w&&get(w,"label"))add(scoped,w);}}
  if(week||session||count(scoped)){add(windows,week);add(windows,session);for(UInt i=0;i<count(scoped)&&count(windows)<4;++i)add(windows,at(scoped,i));return r;}}
 add(windows,usageClaudeWindow(get(limits,"seven_day"),10080,nullptr));add(windows,usageClaudeWindow(get(limits,"five_hour"),300,nullptr));
 Obj scoped=get(limits,"model_scoped");bool any=false;
 if(usageKind(scoped,"NSArray"))for(UInt i=0;i<count(scoped)&&count(windows)<4;++i){Obj row=at(scoped,i);if(!usageKind(row,"NSDictionary"))continue;
  Obj w=usageClaudeWindow(row,10080,get(row,"display_name"));if(w&&get(w,"label")){add(windows,w);any=true;}}
 // Older servers name the per-model weekly windows explicitly.
 if(!any){if(count(windows)<4)add(windows,usageClaudeWindow(get(limits,"seven_day_opus"),10080,str("Opus")));if(count(windows)<4)add(windows,usageClaudeWindow(get(limits,"seven_day_sonnet"),10080,str("Sonnet")));}
 return r;
}
// `claude auth status` JSON -> {signedIn, kind, email, plan}
Obj usageAccountClaude(Obj status){
 Obj r=dict();bool in=usageKind(status,"NSDictionary")&&usageKind(get(status,"loggedIn"),"NSNumber")&&truth(get(status,"loggedIn"));put(r,"signedIn",boolean(in));if(!in)return r;
 Obj method=get(status,"authMethod");if(usageKind(method,"NSString")&&send<UInt>(method,"length")<=40)put(r,"kind",method);
 Obj email=get(status,"email");if(usageKind(email,"NSString")&&send<UInt>(email,"length")<=254)put(r,"email",email);
 Obj plan=get(status,"subscriptionType");if(usageKind(plan,"NSString")&&send<UInt>(plan,"length")<=40)put(r,"plan",plan);
 return r;
}
// JSON-RPC error object -> "auth" | "unsupported" | "error"
const char* usageErrorKind(Obj error){
 if(!usageKind(error,"NSDictionary"))return "error";
 if(integer(get(error,"code"))==-32601)return "unsupported"; // method not found: older Codex
 Obj message=get(error,"message");const char* m=usageKind(message,"NSString")?utf8(message):"";
 if(strstr(m,"authentication required")||strstr(m,"not logged in")||strstr(m,"login"))return "auth";
 return "error";
}
