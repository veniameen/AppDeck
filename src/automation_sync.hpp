#pragma once
// Definition-only automation reconciliation. No filesystem, database, credential or scheduler API.
// Foundation dictionaries are also exercised by the portable test adapter. The caller owns IO,
// persists `state` before applying `writes`, and acknowledges only successful stopped-home writes.
inline bool automationId(const char* s){
 Size n=deck::length(s);if(!n||n>128||deck::equal(s,".")||deck::equal(s,".."))return false;
 for(Size i=0;i<n;++i){char c=s[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;}return true;
}
Obj automationSlice(const char* s,Size begin,Size end){
 if(end<begin)return nullptr;char* b=(char*)calloc(end-begin+1,1);if(!b)return nullptr;memcpy(b,s+begin,end-begin);Obj out=str(b);free(b);return out;
}
bool automationSpace(char c){return c==' '||c=='\t'||c=='\r';}
bool automationHex(char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
// Skip a TOML string, including literal/basic multiline prompts, without interpreting its contents.
bool automationStringEnd(const char* s,Size n,Size& p){
 if(p>=n||(s[p]!='\''&&s[p]!='"'))return false;char quote=s[p];
 bool multi=p+2<n&&s[p+1]==quote&&s[p+2]==quote;p+=multi?3:1;
 while(p<n){char c=s[p];if(c==quote){if(!multi){++p;return true;}if(p+2<n&&s[p+1]==quote&&s[p+2]==quote){p+=3;return true;}}
  if(!multi&&(c=='\n'||c=='\r'))return false;
  if((unsigned char)c<32&&c!='\t'&&c!='\n'&&c!='\r')return false;
  if(quote=='"'&&c=='\\'){
   if(++p>=n)return false;char e=s[p];
   if(e=='u'||e=='U'){Size digits=e=='u'?4:8;for(Size j=0;j<digits;++j)if(++p>=n||!automationHex(s[p]))return false;}
   else if(e=='\n'||e=='\r'){if(!multi)return false;}
   else if(e!='b'&&e!='t'&&e!='n'&&e!='f'&&e!='r'&&e!='"'&&e!='\\'&&!(multi&&automationSpace(e)))return false;
  }++p;
 }return false;
}
bool automationKnownKey(const char* k){
 for(const char* field:{"version","id","kind","name","prompt","status","rrule","model","reasoning_effort","notification_policy","execution_environment","local_environment_config_path","plugin_template_id","target","cwds","target_thread_id","created_at","updated_at"})if(deck::equal(k,field))return true;return false;
}
Obj automationPlainString(Obj raw){
 if(!isClass(raw,"NSString"))return nullptr;const char* s=utf8(raw);Size n=deck::length(s);if(n<2||(s[0]!='"'&&s[0]!='\'')||s[n-1]!=s[0])return nullptr;
 for(Size i=1;i+1<n;++i)if(s[i]=='\\'||s[i]==s[0]||(unsigned char)s[i]<32)return nullptr;
 return automationSlice(s,1,n-1);
}
bool automationQuotedValue(Obj raw){if(!isClass(raw,"NSString"))return false;const char* s=utf8(raw);Size p=0,n=deck::length(s);return automationStringEnd(s,n,p)&&p==n;}
bool automationIntegerValue(Obj raw){const char* s=utf8(raw);if(!*s)return false;for(;*s;++s)if(*s<'0'||*s>'9')return false;return true;}
bool automationCwds(Obj raw){
 const char* s=utf8(raw);Size n=deck::length(s),p=0;if(!n||s[p++]!='[')return false;
 for(;;){while(p<n&&(automationSpace(s[p])||s[p]=='\n'))++p;if(p<n&&s[p]==']')return ++p==n;
  Size start=p;if(!automationStringEnd(s,n,p))return false;Obj value=automationPlainString(automationSlice(s,start,p));
  if(!value||!deck::absoluteLocalPath(utf8(value)))return false;
  while(p<n&&(automationSpace(s[p])||s[p]=='\n'))++p;if(p<n&&s[p]==','){++p;continue;}if(p<n&&s[p]==']')return ++p==n;return false;
 }
}
bool automationTarget(Obj raw){
 const char* s=utf8(raw);Size n=deck::length(s),p=0;if(!n||s[p++]!='{')return false;Obj fields=dict();
 for(;;){while(p<n&&automationSpace(s[p]))++p;if(p<n&&s[p]=='}'){++p;break;}
  Size start=p;while(p<n&&((s[p]>='a'&&s[p]<='z')||s[p]=='_'))++p;if(start==p)return false;Obj key=automationSlice(s,start,p);
  if((!deck::equal(utf8(key),"type")&&!deck::equal(utf8(key),"project_id"))||get(fields,utf8(key)))return false;
  while(p<n&&automationSpace(s[p]))++p;if(p>=n||s[p++]!='=')return false;while(p<n&&automationSpace(s[p]))++p;
  start=p;if(!automationStringEnd(s,n,p))return false;Obj value=automationPlainString(automationSlice(s,start,p));if(!value)return false;put(fields,utf8(key),value);
  while(p<n&&automationSpace(s[p]))++p;if(p<n&&s[p]==','){++p;continue;}if(p<n&&s[p]=='}'){++p;break;}return false;
 }
 if(p!=n)return false;const char* type=utf8(get(fields,"type"));
 if(deck::equal(type,"projectless"))return !get(fields,"project_id");
 return deck::equal(type,"project")&&deck::projectId(utf8(get(fields,"project_id")));
}
// Supported legacy schema only. Unknown/duplicate keys and account/cloud bindings fail closed.
Obj automationDefinition(Obj text,Obj expectedId=nullptr){
 if(!isClass(text,"NSString"))return nullptr;const char* s=utf8(text);Size n=deck::length(s);if(!n||n>1024*1024||send<UInt>(text,"lengthOfBytesUsingEncoding:",(UInt)4)!=n)return nullptr;
 Obj fields=dict();Size p=0,statusBegin=0,statusEnd=0;
 while(p<n){while(p<n&&(automationSpace(s[p])||s[p]=='\n'))++p;if(p==n)break;if(s[p]=='#'){while(p<n&&s[p]!='\n')++p;continue;}
  Size begin=p;while(p<n&&((s[p]>='a'&&s[p]<='z')||s[p]=='_'))++p;if(p==begin)return nullptr;
  Obj key=automationSlice(s,begin,p);if(!automationKnownKey(utf8(key))||get(fields,utf8(key)))return nullptr;
  while(p<n&&automationSpace(s[p]))++p;if(p==n||s[p++]!='=')return nullptr;while(p<n&&automationSpace(s[p]))++p;begin=p;
  int arrayDepth=0,tableDepth=0;
  while(p<n){char c=s[p];if(c=='"'||c=='\''){if(!automationStringEnd(s,n,p))return nullptr;continue;}
   if(c=='[')++arrayDepth;else if(c==']'){if(--arrayDepth<0)return nullptr;}else if(c=='{')++tableDepth;else if(c=='}'){if(--tableDepth<0)return nullptr;}
   if((c=='\n'||c=='#')&&!arrayDepth&&!tableDepth)break;
   if(c=='#'){while(p<n&&s[p]!='\n')++p;continue;}++p;
  }
  if(arrayDepth||tableDepth)return nullptr;Size end=p;while(end>begin&&automationSpace(s[end-1]))--end;if(end==begin)return nullptr;
  Obj raw=automationSlice(s,begin,end);put(fields,utf8(key),raw);
  if(deck::equal(utf8(key),"status")){statusBegin=begin;statusEnd=end;}
  if(p<n&&s[p]=='#')while(p<n&&s[p]!='\n')++p;
 }
 if(!deck::equal(utf8(get(fields,"version")),"1"))return nullptr;
 Obj id=automationPlainString(get(fields,"id")),status=automationPlainString(get(fields,"status")),kind=automationPlainString(get(fields,"kind"));
 if(!id||!automationId(utf8(id))||(expectedId&&!same(id,expectedId))||!status)return nullptr;
 if(!deck::equal(utf8(status),"ACTIVE")&&!deck::equal(utf8(status),"PAUSED")&&!deck::equal(utf8(status),"DELETED"))return nullptr;
 if(get(fields,"kind")&&!kind)return nullptr;if(!kind)kind=str("cron");
 if(!deck::equal(utf8(kind),"cron")&&!deck::equal(utf8(kind),"heartbeat"))return nullptr;
 for(const char* k:{"name","prompt","rrule"})if(!automationQuotedValue(get(fields,k)))return nullptr;
 for(const char* k:{"created_at","updated_at"})if(!automationIntegerValue(get(fields,k)))return nullptr;
 for(const char* k:{"model","reasoning_effort","notification_policy","execution_environment","local_environment_config_path","plugin_template_id"})if(get(fields,k)&&!automationQuotedValue(get(fields,k)))return nullptr;
 if(get(fields,"cwds")&&!automationCwds(get(fields,"cwds")))return nullptr;if(get(fields,"target")&&!automationTarget(get(fields,"target")))return nullptr;
 if(deck::equal(utf8(kind),"heartbeat")){Obj target=automationPlainString(get(fields,"target_thread_id"));if(!target||!automationId(utf8(target))||get(fields,"cwds")||get(fields,"target"))return nullptr;}
 else if(!get(fields,"cwds")||get(fields,"target_thread_id"))return nullptr;
 Obj out=dict();put(out,"text",text);put(out,"fields",fields);put(out,"id",id);put(out,"status",status);put(out,"kind",kind);put(out,"statusBegin",num((Int)statusBegin));put(out,"statusEnd",num((Int)statusEnd));return out;
}
Obj automationWithStatus(Obj definition,const char* status){
 const char* s=utf8(get(definition,"text"));Obj left=automationSlice(s,0,(Size)integer(get(definition,"statusBegin"))),right=str(s+(Size)integer(get(definition,"statusEnd")));
 Obj value=cat(str("\""),cat(str(status),str("\"")));return cat(cat(left,value),right);
}
bool automationDefinitionsEqual(Obj a,Obj b,bool ignoreStatus=false){
 if(same(a,b))return true;Obj x=automationDefinition(a),y=automationDefinition(b);if(!x||!y)return false;
 Obj xf=get(x,"fields"),yf=get(y,"fields");
 for(Obj fields:{xf,yf}){Obj keys=send<Obj>(fields,"allKeys");for(UInt i=0;i<count(keys);++i){const char* key=utf8(at(keys,i));if(deck::equal(key,"updated_at")||(ignoreStatus&&deck::equal(key,"status")))continue;
   if(!same(get(xf,key),get(yf,key)))return false;}}
 return true;
}
Obj automationDictionaryCopy(Obj source){Obj result=dict();if(isClass(source,"NSDictionary")){Obj keys=send<Obj>(source,"allKeys");for(UInt i=0;i<count(keys);++i)put(result,utf8(at(keys,i)),get(source,utf8(at(keys,i))));}return result;}
bool automationSyncStateValid(Obj state){
 if(!isClass(state,"NSDictionary")||!isClass(get(state,"version"),"NSNumber")||integer(get(state,"version"))!=1||!isClass(get(state,"records"),"NSDictionary")||!isClass(get(state,"profiles"),"NSDictionary"))return false;
 Obj records=get(state,"records"),ids=send<Obj>(records,"allKeys");if(count(ids)>4096)return false;
 for(UInt i=0;i<count(ids);++i){Obj id=at(ids,i),r=get(records,utf8(id));if(!isClass(r,"NSDictionary")||!isClass(get(r,"owner"),"NSString")||!send<UInt>(get(r,"owner"),"length")||!isClass(get(r,"deleted"),"NSNumber")||!automationDefinition(get(r,"text"),id))return false;}
 Obj profiles=get(state,"profiles"),keys=send<Obj>(profiles,"allKeys");if(count(keys)>512)return false;
 for(UInt i=0;i<count(keys);++i){Obj baseline=get(profiles,utf8(at(keys,i)));if(!isClass(baseline,"NSDictionary"))return false;Obj bs=send<Obj>(baseline,"allKeys");if(count(bs)>4096)return false;
  for(UInt j=0;j<count(bs);++j)if(!automationDefinition(get(baseline,utf8(at(bs,j))),at(bs,j)))return false;}
 return true;
}
void automationSyncConflict(Obj result,Obj profile,Obj id,const char* reason){Obj item=dict();put(item,"profileId",profile?profile:str(""));put(item,"id",id);put(item,"reason",str(reason));add(get(result,"conflicts"),item);}
void automationSyncUnique(Obj list,Obj value){if(!send<bool>(list,"containsObject:",value))add(list,value);}
// Missing directory is a complete empty observation. Without a previous baseline it is never a
// deletion. Set complete=false on read errors, symlinks, unsupported entries or an unknown schema.
Obj automationSyncReconcile(Obj previous,Obj observations){
 Obj result=dict(),state=dict(),records=dict(),profiles=dict();put(result,"state",state);put(result,"writes",array());put(result,"pending",array());put(result,"conflicts",array());put(result,"unsafe",array());
 put(state,"version",num(1));put(state,"records",records);put(state,"profiles",profiles);
 if(previous&&!automationSyncStateValid(previous)){automationSyncConflict(result,nullptr,str(""),"invalid-state");put(result,"state",previous);return result;}
 if(previous){records=automationDictionaryCopy(get(previous,"records"));put(state,"records",records);Obj old=get(previous,"profiles"),keys=send<Obj>(old,"allKeys");for(UInt i=0;i<count(keys);++i)put(profiles,utf8(at(keys,i)),automationDictionaryCopy(get(old,utf8(at(keys,i)))));}
 if(!isClass(observations,"NSArray"))return result;
 if(count(observations)>512){automationSyncConflict(result,nullptr,str(""),"capacity");return result;}
 Obj ids=array(),blocked=dict(),valid=array();Obj known=send<Obj>(records,"allKeys");for(UInt i=0;i<count(known);++i)add(ids,at(known,i));
 for(UInt i=0;i<count(observations);++i){Obj o=at(observations,i),profile=get(o,"profileId"),files=get(o,"files");
  if(!isClass(profile,"NSString")||!send<UInt>(profile,"length")||!isClass(files,"NSDictionary")||!truth(get(o,"complete")))continue;
  add(valid,o);Obj keys=send<Obj>(files,"allKeys");for(UInt j=0;j<count(keys);++j){Obj id=at(keys,j);automationSyncUnique(ids,id);if(!automationDefinition(get(files,utf8(id)),id)){put(blocked,utf8(id),boolean(true));automationSyncConflict(result,profile,id,"unsupported-definition");}}
 }
 // Enforce the persisted state's bounds before accepting any changes. Individually bounded
 // profiles can otherwise produce an oversized union that cannot be read on the next launch.
 Obj ownerIds=array(),previousOwners=send<Obj>(profiles,"allKeys");for(UInt i=0;i<count(previousOwners);++i)add(ownerIds,at(previousOwners,i));
 for(UInt i=0;i<count(valid);++i)automationSyncUnique(ownerIds,get(at(valid,i),"profileId"));
 if(count(ids)>4096||count(ownerIds)>512){automationSyncConflict(result,nullptr,str(""),"capacity");return result;}
 for(UInt i=0;i<count(ids);++i){Obj id=at(ids,i);const char* key=utf8(id);if(truth(get(blocked,key)))continue;
  Obj old=get(records,key),owner=old?get(old,"owner"):nullptr,canonical=old?get(old,"text"):nullptr,candidate=nullptr,candidateProfile=nullptr;bool deletion=false,conflict=false;
  // Establish ownership once, in caller-provided order (base first), never from a filesystem mtime.
  if(!old)for(UInt j=0;j<count(valid);++j){Obj o=at(valid,j),text=get(get(o,"files"),key);if(text){owner=get(o,"profileId");canonical=text;break;}}
  if(!canonical)continue;
  for(UInt j=0;j<count(valid);++j){Obj o=at(valid,j),profile=get(o,"profileId"),text=get(get(o,"files"),key),baseline=get(get(profiles,utf8(profile)),key);bool isOwner=same(profile,owner);
   if(text){Obj parsed=automationDefinition(text,id);bool active=deck::equal(utf8(get(parsed,"status")),"ACTIVE");
    // Unsafe describes observed disk state, even for a stopped home: the caller must apply the
    // PAUSED projection and reconcile again before launching any executor. In a content conflict
    // we preserve both originals, so the unsafe condition deliberately remains launch-blocking.
    if(active&&!isOwner){automationSyncConflict(result,profile,id,"nonowner-active");automationSyncUnique(get(result,"unsafe"),id);}
    if(old&&truth(get(old,"deleted"))){if(!baseline){conflict=true;automationSyncConflict(result,profile,id,"deleted-id-reused");}continue;}
    bool deleted=deck::equal(utf8(get(parsed,"status")),"DELETED");
    if(baseline&&!deleted&&automationDefinitionsEqual(text,baseline,!isOwner))continue;
    Obj proposed=text;if(!isOwner)proposed=automationWithStatus(parsed,utf8(get(automationDefinition(canonical),"status")));
    if(!baseline){if(automationDefinitionsEqual(proposed,canonical))continue;conflict=true;automationSyncConflict(result,profile,id,"untracked-id-collision");continue;}
    if(deleted){deletion=true;continue;}
    if(candidate&&!automationDefinitionsEqual(candidate,proposed)){conflict=true;automationSyncConflict(result,profile,id,"concurrent-edit");}
    else{candidate=proposed;candidateProfile=profile;}
   }else if(baseline&&old&&!truth(get(old,"deleted")))deletion=true;
  }
  if(deletion&&candidate){conflict=true;automationSyncConflict(result,candidateProfile,id,"delete-edit-conflict");}
  if(!old){old=dict();put(old,"owner",owner);put(old,"text",canonical);put(old,"deleted",boolean(false));put(records,key,old);}
  if(conflict){put(blocked,key,boolean(true));continue;}
  Obj record=automationDictionaryCopy(old);if(candidate)put(record,"text",candidate);if(deletion||deck::equal(utf8(get(automationDefinition(get(record,"text")),"status")),"DELETED"))put(record,"deleted",boolean(true));put(records,key,record);
  Obj parsed=automationDefinition(get(record,"text"));
  for(UInt j=0;j<count(valid);++j){Obj o=at(valid,j),profile=get(o,"profileId"),text=get(get(o,"files"),key),baseline=get(profiles,utf8(profile));if(!baseline){baseline=dict();put(profiles,utf8(profile),baseline);}
   // Baseline first reflects what was actually observed, never an uncommitted write target.
   if(text)put(baseline,key,text);else erase(baseline,key);
   Obj wanted=truth(get(record,"deleted"))?nullptr:(same(profile,owner)?get(record,"text"):automationWithStatus(parsed,"PAUSED"));
   if((wanted&&text&&automationDefinitionsEqual(wanted,text))||(!wanted&&!text))continue;
   Obj write=dict();put(write,"profileId",profile);put(write,"id",id);if(wanted)put(write,"text",wanted);else put(write,"remove",boolean(true));
   if(truth(get(o,"stopped")))add(get(result,"writes"),write);else automationSyncUnique(get(result,"pending"),profile);
  }
 }
 // First disarm replicas/previous owners, then write any ACTIVE owner projection. The IO layer
 // must also verify all other homes are readable and not ACTIVE before each ACTIVE write, since
 // an earlier PAUSED write may have failed or a desktop may have started independently.
 Obj writes=get(result,"writes"),ordered=array();for(int phase=0;phase<2;++phase)for(UInt i=0;i<count(writes);++i){Obj w=at(writes,i),text=get(w,"text");
  bool active=text&&deck::equal(utf8(get(automationDefinition(text),"status")),"ACTIVE");if(active==(phase==1))add(ordered,w);}
 put(result,"writes",ordered);return result;
}
void automationSyncMarkApplied(Obj state,Obj profileId,Obj id,Obj text){
 Obj profiles=get(state,"profiles"),baseline=get(profiles,utf8(profileId));if(!baseline){baseline=dict();put(profiles,utf8(profileId),baseline);}if(text)put(baseline,utf8(id),text);else erase(baseline,utf8(id));
}
// The caller must require every participating desktop process to be stopped before changing
// ownership, persist this state, then apply all projections before permitting another launch.
bool automationSyncSetOwner(Obj state,Obj id,Obj profileId){
 if(!automationSyncStateValid(state)||!isClass(profileId,"NSString")||!send<UInt>(profileId,"length"))return false;
 Obj records=get(state,"records"),old=get(records,utf8(id));if(!old||truth(get(old,"deleted")))return false;
 Obj record=automationDictionaryCopy(old);put(record,"owner",profileId);put(records,utf8(id),record);return true;
}
