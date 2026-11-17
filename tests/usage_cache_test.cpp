// The production scheduler/cache and probe run with isolated state and a local CLI stand-in only.
#define main appdeckApplicationMain
#include "../src/main.cpp"
#undef main
extern "C" int dprintf(int,const char*,...);
namespace {
unsigned cacheChecks=0;Obj cacheScratch=nullptr,cacheFixture=nullptr,cacheLog=nullptr;
void requireCache(bool ok,const char* what){++cacheChecks;if(!ok){dprintf(2,"FAIL: %s\nScratch: %s\n",what,utf8(cacheScratch));exit(1);}}
Obj cacheAccount(const char* email="first@example.invalid",const char* org="org-one",const char* plan="max"){
 Obj a=dict();put(a,"signedIn",boolean(true));put(a,"kind",str("claude.ai"));put(a,"email",str(email));if(org)put(a,"orgId",str(org));put(a,"plan",str(plan));return a;
}
Obj cacheEnvironment(const char* email="first@example.invalid",const char* org="org-one"){
 Obj env=dict();put(env,"HOME",cacheScratch);put(env,"PATH",str("/usr/bin:/bin"));put(env,"CLAUDE_CONFIG_DIR",cacheScratch);
 put(env,"APPDECK_USAGE_FIXTURE_LOG",cacheLog);put(env,"APPDECK_USAGE_FIXTURE_EMAIL",str(email));if(org)put(env,"APPDECK_USAGE_FIXTURE_ORG",str(org));return env;
}
Obj cacheRequest(Obj p,Obj env,bool manual=false){
 Obj request=dict();put(request,"profile",get(p,"id"));put(request,"kind",str("claude"));put(request,"binary",cacheFixture);put(request,"scratch",cacheScratch);put(request,"environment",env);usageCachePrepare(p,env,request,manual);return request;
}
Obj cacheProbe(Obj p,Obj env,bool manual=false){
 Obj request=cacheRequest(p,env,manual),result=dict();put(result,"profile",get(p,"id"));put(result,"cacheEligible",get(request,"cacheEligible"));usageProbeClaude(request,result,true);return result;
}
UInt cacheEvents(const char* event){Obj text=send(cls("NSString"),"stringWithContentsOfFile:encoding:error:",cacheLog,(UInt)4,(Obj*)nullptr);Obj lines=send(text,"componentsSeparatedByString:",str("\n"));UInt n=0;for(UInt i=0;i<count(lines);++i)if(same(at(lines,i),str(event)))++n;return n;}
void finishCache(Obj p,Obj result){put(result,"profile",get(p,"id"));usageFinished(nullptr,nullptr,result);}
}
int main(int argc,char** argv){
 if(argc!=2)return 2;appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);if(!appKit)return 3;Pool pool;
 fileManager=keep(send(cls("NSFileManager"),"defaultManager"));cacheScratch=keep(cat(str("/private/tmp/appdeck-usage-cache-"),uuid()));requireCache(mkdirPrivate(cacheScratch),"create private scratch directory");
 home=cacheScratch;dataRoot=cacheScratch;cacheFixture=str(argv[1]);cacheLog=join(cacheScratch,"events.txt");previewMode=true;
 state=keep(dict());apps=keep(array());profiles=keep(array());proxies=keep(array());events=keep(array());put(state,"apps",apps);put(state,"profiles",profiles);
 Obj a=dict();put(a,"id",uuid());put(a,"adapter",str("claude"));put(a,"usageLimits",boolean(true));add(apps,a);
 Obj first=newProfile(a,str("First fixture"),0,false),second=newProfile(a,str("Second fixture"),1,false),third=newProfile(a,str("Third fixture"),2,false);
 for(Obj p:{first,second,third})put(p,"proxy",str("none"));
 Obj r1=cacheProbe(first,cacheEnvironment());requireCache(!get(r1,"error")&&count(get(get(r1,"limits"),"windows"))==1,"first account obtains actual fixture quota");finishCache(first,r1);
 Obj firstAt=get(usageOf(first),"windowsAt");requireCache(count(usageAccountCache)==1,"successful direct quota enters only volatile cache");
 requireCache(!get(usageOf(first),"orgId")&&!get(usageOf(first),"account")&&!get(usageOf(first),"cacheEligible"),"account scope and cache metadata are absent from persisted display record");
 Obj r2=cacheProbe(second,cacheEnvironment());requireCache(truth(get(r2,"reused")),"second profile reuses quota after fresh official auth status");finishCache(second,r2);
 requireCache(cacheEvents("auth")==2&&cacheEvents("usage")==1,"two fresh auth checks produce one quota request");
 requireCache(same(get(usageOf(second),"windowsAt"),firstAt),"reused display retains original quota timestamp");
 Obj beforeRouteEdit=usageOf(second);put(second,"proxy",str("missing-proxy"));finishCache(second,r2);requireCache(usageOf(second)==beforeRouteEdit,"late borrowed result is discarded after a profile route edit");put(second,"proxy",str("none"));
 Obj r3=cacheProbe(third,cacheEnvironment("first@example.invalid","org-two"));requireCache(!truth(get(r3,"reused")),"same email in another organization cannot reuse quota");finishCache(third,r3);requireCache(cacheEvents("usage")==2,"organization switch makes an independent quota request");
 Obj switched=cacheProbe(second,cacheEnvironment("second@example.invalid"));requireCache(!truth(get(switched,"reused")),"fresh changed account overrides old profile association");finishCache(second,switched);requireCache(cacheEvents("usage")==3,"account switch obtains independent quota");
 Obj missing=cacheProbe(third,cacheEnvironment("first@example.invalid",nullptr));requireCache(!truth(get(missing,"reused")),"missing organization disables reuse");finishCache(third,missing);requireCache(cacheEvents("usage")==4,"unknown scope is independently queried");requireCache(!get(usageAccountCache,utf8(get(third,"id"))),"incomplete identity removes earlier association");
 Obj request=cacheRequest(first,cacheEnvironment());double fetched=send<double>(firstAt,"doubleValue");
 auto result=[](){Obj r=dict();put(r,"account",cacheAccount());return r;};
 requireCache(usageReuseCachedAt(request,result(),fetched+3599),"automatic cache remains usable before hourly boundary");
 requireCache(!usageReuseCachedAt(request,result(),fetched+3600),"automatic cache expires at hourly boundary");
 requireCache(!usageReuseCachedAt(request,result(),fetched-1),"clock rollback never accepts a future snapshot");
 put(request,"manual",boolean(true));requireCache(usageReuseCachedAt(request,result(),fetched+19),"manual duplicate inside floor reuses quota");requireCache(!usageReuseCachedAt(request,result(),fetched+20),"manual refresh after floor asks vendor again");
 Obj wrong=result();put(wrong,"account",cacheAccount("first@example.invalid","org-one","pro"));requireCache(!usageReuseCachedAt(request,wrong,fetched),"changed subscription scope is not reused");
 wrong=result();put(get(wrong,"account"),"kind",str("apiKey"));requireCache(!usageReuseCachedAt(request,wrong,fetched),"API credentials cannot reuse subscription quota");
 wrong=result();put(get(wrong,"account"),"signedIn",boolean(false));requireCache(!usageReuseCachedAt(request,wrong,fetched),"signed-out profile cannot reuse quota");
 wrong=result();put(wrong,"error",str("expired"));requireCache(!usageReuseCachedAt(request,wrong,fetched),"explicit authentication failure cannot be masked");
 put(request,"kind",str("codex"));requireCache(!usageReuseCachedAt(request,result(),fetched),"providers never share cached quota");
 put(first,"proxy",str("missing-proxy"));requireCache(!truth(get(cacheRequest(first,cacheEnvironment()),"reuseAllowed")),"unresolved profile route does not reuse quota");put(first,"proxy",str("none"));
 Obj proxy=dict();put(proxy,"id",str("fixture-proxy"));add(proxies,proxy);put(first,"proxy",get(proxy,"id"));requireCache(!truth(get(cacheRequest(first,cacheEnvironment()),"reuseAllowed")),"per-profile proxy route does not reuse another profile's successful request");put(first,"proxy",str("none"));
 Obj inherited=cacheEnvironment();put(inherited,"HTTPS_PROXY",str("http://127.0.0.1:12345"));requireCache(!truth(get(cacheRequest(first,inherited),"reuseAllowed")),"inherited proxy route is not considered direct");
 put(first,"proxy",str("default"));put(state,"defaultProxy",str("missing-default"));requireCache(!truth(get(cacheRequest(first,cacheEnvironment()),"reuseAllowed")),"unresolved default proxy cannot gain a shared successful result");erase(state,"defaultProxy");put(first,"proxy",str("none"));
 // Rebuild a valid association after the route-change invalidations, then test explicit failures.
 finishCache(first,cacheProbe(first,cacheEnvironment()));finishCache(second,cacheProbe(second,cacheEnvironment()));
 Obj failed=dict();put(failed,"account",cacheAccount("changed@example.invalid"));put(failed,"error",str("network"));finishCache(second,failed);
 requireCache(same(get(usageOf(second),"email"),str("changed@example.invalid"))&&!get(usageOf(second),"windows"),"network failure after account switch never carries old account numbers");
 requireCache(!truth(get(cacheRequest(second,cacheEnvironment()),"reuseAllowed")),"profile with earlier failure is independently retried");
 Obj outEnv=cacheEnvironment();put(outEnv,"APPDECK_USAGE_FIXTURE_SIGNED_OUT",str("1"));UInt quotaBefore=cacheEvents("usage");Obj out=cacheProbe(first,outEnv);finishCache(first,out);
 requireCache(same(get(usageOf(first),"error"),str("auth"))&&!get(usageOf(first),"windows")&&cacheEvents("usage")==quotaBefore,"signed-out vendor result clears numbers without requesting quota");
 requireCache(!get(usageAccountCache,utf8(get(first,"id"))),"sign-out invalidates volatile association");
 Obj explicitFailure=dict();put(explicitFailure,"account",dict());put(explicitFailure,"error",str("region"));finishCache(third,explicitFailure);requireCache(same(get(usageOf(third),"error"),str("region")),"explicit vendor failure is not replaced with generic sign-in error");
 // Retaining this profile's stale display must not depend on eligibility for shared quota reuse.
 auto failQuota=[](Obj p,const char* category,Obj account){Obj r=dict();put(r,"error",str(category));if(account)put(r,"account",account);finishCache(p,r);};
 auto checkStale=[](Obj p,Obj windows,Obj timestamp,const char* category,const char* message){
  Obj u=usageOf(p);requireCache(same(get(u,"windows"),windows)&&same(get(u,"windowsAt"),timestamp)&&same(get(u,"error"),str(category)),message);
 };
 put(first,"proxy",get(proxy,"id"));finishCache(first,cacheProbe(first,cacheEnvironment()));
 Obj staleWindows=get(usageOf(first),"windows"),staleAt=get(usageOf(first),"windowsAt");
 requireCache(count(staleWindows)==1&&!get(usageAccountCache,utf8(get(first,"id"))),"proxied success has its own display without a shared cache entry");
 failQuota(first,"network",cacheAccount());checkStale(first,staleWindows,staleAt,"network","proxied failure retains its last good numbers and original timestamp");
 failQuota(first,"proxy",cacheAccount());checkStale(first,staleWindows,staleAt,"proxy","repeated proxy failure retains stale numbers and current error");
 Obj labeled=cacheProbe(first,cacheEnvironment());put(get(labeled,"limits"),"plan",str("Max display label"));finishCache(first,labeled);
 staleWindows=get(usageOf(first),"windows");staleAt=get(usageOf(first),"windowsAt");failQuota(first,"network",cacheAccount());
 checkStale(first,staleWindows,staleAt,"network","matching auth identity retains numbers even when quota uses another plan label");
 put(first,"proxy",str("none"));finishCache(first,cacheProbe(first,cacheEnvironment()));staleWindows=get(usageOf(first),"windows");staleAt=get(usageOf(first),"windowsAt");
 failQuota(first,"network",cacheAccount());checkStale(first,staleWindows,staleAt,"network","first direct failure retains stale display");
 requireCache(!get(usageAccountCache,utf8(get(first,"id"))),"temporary failure still invalidates reusable quota");
 failQuota(first,"quota",cacheAccount());checkStale(first,staleWindows,staleAt,"quota","second direct failure keeps display after shared cache invalidation");
 finishCache(first,cacheProbe(first,cacheEnvironment()));staleWindows=get(usageOf(first),"windows");staleAt=get(usageOf(first),"windowsAt");
 failQuota(first,"network",nullptr);checkStale(first,staleWindows,staleAt,"network","auth-stage network failure retains old numbers without a current account reply");
 requireCache(same(get(usageOf(first),"email"),str("first@example.invalid"))&&same(get(usageOf(first),"kind"),str("claude.ai"))&&same(get(usageOf(first),"plan"),str("max")),"account-absent failure retains every display identity field");
 failQuota(first,"service",cacheAccount());checkStale(first,staleWindows,staleAt,"service","quota failure after auth-stage failure still recognizes the same display account");
 // Persist only the display record, then discard both process-local identity caches as on restart.
 Obj plistError=nullptr;Obj plistData=send(cls("NSPropertyListSerialization"),"dataWithPropertyList:format:options:error:",usageOf(first),(UInt)200,(UInt)0,&plistError);
 requireCache(plistData&&!plistError,"serialize stale display as a binary property list");
 UInt plistFormat=0;Obj restoredUsage=send(cls("NSPropertyListSerialization"),"propertyListWithData:options:format:error:",plistData,(UInt)1,&plistFormat,&plistError);
 requireCache(restoredUsage&&!plistError&&!get(restoredUsage,"orgId")&&!get(restoredUsage,"account")&&!get(restoredUsage,"cacheEligible"),"restart roundtrip persists only allowlisted display metadata");
 put(first,"usage",restoredUsage);usageSanitize(first);drop(usageAccountCache);usageAccountCache=nullptr;drop(usageDisplayAccounts);usageDisplayAccounts=nullptr;
 staleWindows=get(usageOf(first),"windows");staleAt=get(usageOf(first),"windowsAt");failQuota(first,"network",cacheAccount());
 checkStale(first,staleWindows,staleAt,"network","matching account retains its persisted stale display after restart");
 requireCache(!usageAccountCache&&!truth(get(cacheRequest(first,cacheEnvironment()),"reuseAllowed")),"restored stale display never becomes a reusable successful quota");
 Obj peerRequest=cacheRequest(second,cacheEnvironment()),peerResult=dict();put(peerResult,"account",cacheAccount());
 requireCache(!usageReuseCachedAt(peerRequest,peerResult,nowUnix()),"another profile cannot borrow restored stale numbers");
 // Changed scope must still discard the old numbers, even after temporary errors invalidated sharing.
 for(const char* changed:{"email","plan","kind","org"}){
  finishCache(first,cacheProbe(first,cacheEnvironment()));failQuota(first,"network",cacheAccount());failQuota(first,"service",cacheAccount());
  Obj changedAccount=cacheAccount();if(!strcmp(changed,"email"))put(changedAccount,"email",str("changed@example.invalid"));
  else if(!strcmp(changed,"plan"))put(changedAccount,"plan",str("pro"));else if(!strcmp(changed,"kind"))put(changedAccount,"kind",str("apiKey"));else put(changedAccount,"orgId",str("org-two"));
  failQuota(first,"network",changedAccount);requireCache(!get(usageOf(first),"windows"),"changed account, plan, kind or known organization clears previous numbers");
 }
 for(const char* category:{"expired","suspended","auth"}){
  finishCache(first,cacheProbe(first,cacheEnvironment()));failQuota(first,category,cacheAccount());
  requireCache(same(get(usageOf(first),"error"),str(category))&&!get(usageOf(first),"windows")&&!get(usageAccountCache,utf8(get(first,"id"))),"lost sign-in or suspended access clears old numbers and reusable quota");
 }
 usageInFlight=keep(str("fixture-in-flight"));usageStarted=usageUptime()-1000;previewMode=false;usagePump();requireCache(same(usageInFlight,str("fixture-in-flight")),"slow in-flight worker never permits a parallel probe");previewMode=true;drop(usageInFlight);usageInFlight=nullptr;
 requireCache(!exists(join(cacheScratch,"state.plist")),"fixture never writes persisted profile state");
 Obj error=nullptr;requireCache(send<bool>(fileManager,"removeItemAtPath:error:",cacheScratch,&error),"remove fixture-owned scratch only");
 dprintf(1,"%u account-cache assertions passed; synthetic CLI only, no real profiles or network\n",cacheChecks);return 0;
}
