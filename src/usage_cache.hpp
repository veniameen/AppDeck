// Included after usage_parse.hpp. Only immutable, in-memory quota snapshots are shared with a
// worker. Authentication is always checked by the vendor first; no credential or disk access.
bool usageCacheText(Obj value,UInt max){return usageKind(value,"NSString")&&send<UInt>(value,"length")>0&&send<UInt>(value,"length")<=max;}
bool usageCacheAccount(Obj account){
 return usageKind(account,"NSDictionary")&&usageKind(get(account,"signedIn"),"NSNumber")&&truth(get(account,"signedIn"))&&same(get(account,"kind"),str("claude.ai"))
  &&usageCacheText(get(account,"email"),254)&&usageCacheText(get(account,"orgId"),128)&&usageCacheText(get(account,"plan"),40);
}
bool usageCacheSameAccount(Obj a,Obj b){
 if(!usageCacheAccount(a)||!usageCacheAccount(b))return false;
 for(const char* key:{"kind","email","orgId","plan"})if(!same(get(a,key),get(b,key)))return false;
 return true;
}
// Explicit current-request permission is required. A caller with a profile-specific proxy, an
// unresolved route or an earlier error never receives the cache snapshot in the first place.
bool usageReuseCachedAt(Obj request,Obj result,double now){
 if(!same(get(request,"kind"),str("claude"))||!truth(get(request,"reuseAllowed"))||get(result,"error"))return false;
 Obj account=get(result,"account"),rows=get(request,"cachedAccounts"),best=nullptr;double newest=0;
 if(!usageCacheAccount(account)||!usageKind(rows,"NSArray"))return false;
 for(UInt i=0;i<count(rows)&&i<256;++i){Obj row=at(rows,i);if(!usageKind(row,"NSDictionary")||!usageCacheSameAccount(account,get(row,"account")))continue;
  Obj at=get(row,"windowsAt"),limits=get(row,"limits");if(!usageKind(at,"NSNumber")||!usageKind(limits,"NSDictionary")||!usageKind(get(limits,"windows"),"NSArray")||!count(get(limits,"windows")))continue;
  double fetched=send<double>(at,"doubleValue");if(fetched>newest&&deck::usageCacheFresh(now,fetched,truth(get(request,"manual")))){best=row;newest=fetched;}}
 if(!best)return false;put(result,"limits",get(best,"limits"));put(result,"windowsAt",get(best,"windowsAt"));put(result,"reused",boolean(true));return true;
}
bool usageReuseCached(Obj request,Obj result){return usageReuseCachedAt(request,result,send<double>(send(cls("NSDate"),"date"),"timeIntervalSince1970"));}
