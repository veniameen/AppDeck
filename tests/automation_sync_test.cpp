// Exercise the production definition parser/reconciler without Foundation or real profiles.
#include "../src/core.hpp"
#include "../src/edge_policy.hpp"
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>
struct Node {enum Kind{String,Array,Dictionary,Number} kind;std::string text;long value=0;std::vector<Node*> children;std::map<std::string,Node*> fields;explicit Node(Kind k):kind(k){}};
using Obj=Node*;using UInt=unsigned long;using Int=long;using Size=unsigned long;
std::vector<std::unique_ptr<Node>> arena;
Obj make(Node::Kind k){arena.emplace_back(std::make_unique<Node>(k));return arena.back().get();}
Obj str(const char* s){Obj o=make(Node::String);o->text=s?s:"";return o;}
Obj num(Int n){Obj o=make(Node::Number);o->value=n;return o;}Obj boolean(bool b){return num(b?1:0);}Int integer(Obj o){return o?o->value:0;}bool truth(Obj o){return integer(o)!=0;}
Obj array(){return make(Node::Array);}Obj dict(){return make(Node::Dictionary);}
const char* utf8(Obj o){return o?o->text.c_str():"";}
Obj get(Obj o,const char* key){if(!o)return nullptr;auto i=o->fields.find(key);return i==o->fields.end()?nullptr:i->second;}
void put(Obj o,const char* key,Obj v){if(v)o->fields[key]=v;}void erase(Obj o,const char* key){o->fields.erase(key);}void add(Obj a,Obj v){a->children.push_back(v);}
UInt count(Obj o){return o?(UInt)o->children.size():0;}Obj at(Obj o,UInt i){return o->children.at(i);}
bool same(Obj a,Obj b){if(a==b)return true;if(!a||!b||a->kind!=b->kind)return false;return a->kind==Node::String?a->text==b->text:a->kind==Node::Number&&a->value==b->value;}
Obj cat(Obj a,Obj b){return str((std::string(utf8(a))+utf8(b)).c_str());}
bool isClass(Obj o,const char* k){return o&&((o->kind==Node::String&&deck::equal(k,"NSString"))||(o->kind==Node::Array&&deck::equal(k,"NSArray"))||(o->kind==Node::Dictionary&&deck::equal(k,"NSDictionary"))||(o->kind==Node::Number&&deck::equal(k,"NSNumber")));}
template<class R> R send(Obj o,const char* selector){
 if constexpr(std::is_same_v<R,Obj>){if(!deck::equal(selector,"allKeys"))std::abort();Obj keys=array();for(auto& f:o->fields)add(keys,str(f.first.c_str()));return keys;}
 else{if(!deck::equal(selector,"length"))std::abort();return static_cast<R>(o->text.size());}
}
template<class R> R send(Obj o,const char* selector,Obj p){if(!deck::equal(selector,"containsObject:"))std::abort();for(Obj x:o->children)if(same(x,p))return static_cast<R>(true);return static_cast<R>(false);}
template<class R> R send(Obj o,const char* selector,UInt encoding){if(!deck::equal(selector,"lengthOfBytesUsingEncoding:")||encoding!=4)std::abort();return static_cast<R>(o->text.size());}
#include "../src/automation_sync.hpp"
Obj definition(const char* id="daily",const char* status="PAUSED",const char* name="Daily",bool heartbeat=false){
 std::string t="version = 1\nid = \""+std::string(id)+"\"\nkind = \""+(heartbeat?"heartbeat":"cron")+"\"\nname = \""+name+"\"\nprompt = \"Inspect the project\"\nstatus = \""+status+"\"\nrrule = \"FREQ=HOURLY;INTERVAL=1\"\n";
 t+=heartbeat?"target_thread_id = \"019c6e27-e55b-73d1-87d8-4e01f1f75043\"\n":"cwds = [\"/tmp/project\"]\n";t+="created_at = 1\nupdated_at = 2\n";return str(t.c_str());
}
Obj observation(const char* profile,bool stopped=true,bool complete=true){Obj o=dict();put(o,"profileId",str(profile));put(o,"stopped",boolean(stopped));put(o,"complete",boolean(complete));put(o,"files",dict());return o;}
Obj observations(Obj a,Obj b=nullptr,Obj c=nullptr){Obj v=array();add(v,a);if(b)add(v,b);if(c)add(v,c);return v;}
void file(Obj o,const char* id,Obj text){if(text)put(get(o,"files"),id,text);else erase(get(o,"files"),id);}
Obj writeFor(Obj result,const char* profile,const char* id){for(Obj w:get(result,"writes")->children)if(get(w,"profileId")->text==profile&&get(w,"id")->text==id)return w;return nullptr;}
bool hasConflict(Obj result,const char* reason){for(Obj c:get(result,"conflicts")->children)if(get(c,"reason")->text==reason)return true;return false;}
Obj apply(Obj result,Obj obs){Obj state=get(result,"state");for(Obj w:get(result,"writes")->children)for(Obj o:obs->children)if(same(get(w,"profileId"),get(o,"profileId"))){Obj text=get(w,"text");file(o,utf8(get(w,"id")),text);automationSyncMarkApplied(state,get(w,"profileId"),get(w,"id"),text);}return state;}
std::string status(Obj text){return utf8(get(automationDefinition(text),"status"));}
int main(){
 unsigned tests=0;auto check=[&](bool b){++tests;if(!b){std::cerr<<"FAIL automation assertion "<<tests<<'\n';std::exit(1);}};
 check(automationDefinition(definition()));check(automationDefinition(definition("heartbeat","PAUSED","Watch",true)));
 check(automationDefinition(cat(definition("heartbeat","PAUSED","Watch",true),str("notification_policy = \"failed_runs_only\"\n"))));
 check(!automationDefinition(definition(),str("wrong-id")));check(!automationDefinition(cat(definition(),str("secret_key = \"no\"\n"))));
 check(!automationDefinition(cat(definition(),str("status = \"ACTIVE\"\n"))));check(!automationId("../task"));check(!automationId(".."));check(automationId("task-1"));
 auto altered=[&](Obj text,const std::string& from,const std::string& to){std::string s=utf8(text);s.replace(s.find(from),from.size(),to);return str(s.c_str());};
 check(!automationDefinition(altered(definition(),"version = 1","version = 2")));
 check(!automationDefinition(altered(definition(),"/tmp/project","ssh://host/project")));
 check(!automationDefinition(altered(definition(),"PAUSED","UNKNOWN")));
 Obj multiline=altered(definition(),"\"Inspect the project\"","\"\"\"Line one\nstatus = \\\"ACTIVE\\\"\nLine three\"\"\"");
 check(automationDefinition(multiline));Obj changed=automationWithStatus(automationDefinition(multiline),"ACTIVE");check(status(changed)=="ACTIVE");check(std::string(utf8(changed)).find("status = \\\"ACTIVE\\\"")!=std::string::npos);
 check(automationDefinitionsEqual(definition(),altered(definition(),"updated_at = 2","updated_at = 900")));
 check(automationDefinitionsEqual(definition(),definition("daily","ACTIVE"),true));check(!automationDefinitionsEqual(definition(),definition("daily","ACTIVE")));
 check(!automationDefinition(cat(definition(),str("target = { type = \"project\", project_id = \"cloud:account:id\" }\n"))));
 check(automationDefinition(cat(definition(),str("target = { type = \"project\", project_id = \"local-123\" }\n"))));
 check(automationDefinition(cat(definition(),str("target = { type = \"projectless\" }\n"))));
 Obj nul=definition();nul->text.push_back('\0');nul->text+="unknown = 1\n";check(!automationDefinition(nul));
 // First observation bootstraps from actual owner; a missing directory in a new profile cannot delete.
 Obj base=observation("base"),copy=observation("copy"),third=observation("third");file(base,"daily",definition());Obj obs=observations(base,copy,third),r=automationSyncReconcile(nullptr,obs),s=get(r,"state");
 check(automationSyncStateValid(s));check(count(get(r,"writes"))==2);check(get(get(get(s,"records"),"daily"),"owner")->text=="base");check(!truth(get(get(get(s,"records"),"daily"),"deleted")));
 check(status(get(writeFor(r,"copy","daily"),"text"))=="PAUSED");s=apply(r,obs);r=automationSyncReconcile(s,obs);check(count(get(r,"writes"))==0);check(count(get(r,"conflicts"))==0);
 // Owner activation projects PAUSED everywhere else, preserving global desired ACTIVE state.
 file(base,"daily",definition("daily","ACTIVE"));r=automationSyncReconcile(s,obs);s=apply(r,obs);check(status(get(get(get(s,"records"),"daily"),"text"))=="ACTIVE");check(status(get(get(copy,"files"),"daily"))=="PAUSED");
 // Nonowner content edits are accepted, but cannot acquire schedule execution by changing status.
 file(copy,"daily",definition("daily","ACTIVE","Changed from copy"));r=automationSyncReconcile(s,obs);check(hasConflict(r,"nonowner-active"));check(writeFor(r,"copy","daily"));check(writeFor(r,"base","daily"));s=apply(r,obs);
 check(status(get(get(copy,"files"),"daily"))=="PAUSED");check(status(get(get(base,"files"),"daily"))=="ACTIVE");check(std::string(utf8(get(get(base,"files"),"daily"))).find("Changed from copy")!=std::string::npos);
 file(copy,"daily",automationWithStatus(automationDefinition(get(get(copy,"files"),"daily")),"ACTIVE"));put(copy,"stopped",boolean(false));r=automationSyncReconcile(s,obs);check(count(get(r,"unsafe"))==1);check(!writeFor(r,"copy","daily"));check(count(get(r,"pending"))==1);
 put(copy,"stopped",boolean(true));r=automationSyncReconcile(s,obs);check(writeFor(r,"copy","daily"));s=apply(r,obs);
 // Explicit owner change retains status; exactly one projected ACTIVE definition remains.
 check(automationSyncSetOwner(s,str("daily"),str("copy")));r=automationSyncReconcile(s,obs);check(status(get(writeFor(r,"base","daily"),"text"))=="PAUSED");check(status(get(writeFor(r,"copy","daily"),"text"))=="ACTIVE");s=apply(r,obs);
 unsigned active=0;for(Obj o:obs->children)active+=status(get(get(o,"files"),"daily"))=="ACTIVE";check(active==1);
 check(automationSyncSetOwner(s,str("daily"),str("base")));r=automationSyncReconcile(s,obs);check(get(at(get(r,"writes"),0),"profileId")->text=="copy");check(status(get(at(get(r,"writes"),0),"text"))=="PAUSED");check(get(at(get(r,"writes"),1),"profileId")->text=="base");s=apply(r,obs);
 // New automation created in an additional profile owns its own schedule.
 file(third,"new-task",definition("new-task","PAUSED"));r=automationSyncReconcile(s,obs);s=apply(r,obs);check(get(get(get(s,"records"),"new-task"),"owner")->text=="third");check(automationSyncSetOwner(s,str("new-task"),str("base")));r=automationSyncReconcile(s,obs);s=apply(r,obs);check(status(get(get(get(s,"records"),"new-task"),"text"))=="PAUSED");
 // Deletion from a replica creates a tombstone and removes definitions, never arbitrary directories.
 file(third,"daily",nullptr);r=automationSyncReconcile(s,obs);check(truth(get(get(get(get(r,"state"),"records"),"daily"),"deleted")));check(truth(get(writeFor(r,"base","daily"),"remove")));s=apply(r,obs);check(!get(get(copy,"files"),"daily"));
 check(!automationSyncSetOwner(s,str("daily"),str("third")));r=automationSyncReconcile(s,obs);check(count(get(r,"writes"))==0);
 // An unavailable snapshot cannot turn every definition into a deletion.
 file(base,"new-task",nullptr);put(base,"complete",boolean(false));r=automationSyncReconcile(s,obs);check(!truth(get(get(get(get(r,"state"),"records"),"new-task"),"deleted")));put(base,"complete",boolean(true));file(base,"new-task",get(get(copy,"files"),"new-task"));
 // Collision on first import is retained as a conflict, never overwritten with arbitrary winner.
 Obj a=observation("a"),b=observation("b");file(a,"same",definition("same","PAUSED","A"));file(b,"same",definition("same","PAUSED","B"));Obj pair=observations(a,b);r=automationSyncReconcile(nullptr,pair);check(hasConflict(r,"untracked-id-collision"));check(count(get(r,"writes"))==0);r=automationSyncReconcile(get(r,"state"),pair);check(hasConflict(r,"untracked-id-collision"));
 // Concurrent edits remain reviewable and are not silently acknowledged on repeated passes.
 a=observation("a");b=observation("b");file(a,"edit",definition("edit"));pair=observations(a,b);s=apply(automationSyncReconcile(nullptr,pair),pair);file(a,"edit",definition("edit","PAUSED","A"));file(b,"edit",definition("edit","PAUSED","B"));r=automationSyncReconcile(s,pair);check(hasConflict(r,"concurrent-edit"));check(count(get(r,"writes"))==0);r=automationSyncReconcile(get(r,"state"),pair);check(hasConflict(r,"concurrent-edit"));
 file(a,"edit",nullptr);r=automationSyncReconcile(s,pair);check(hasConflict(r,"delete-edit-conflict"));check(count(get(r,"writes"))==0);
 // Corrupt/unknown definitions are not interpreted as missing; their originals survive.
 file(a,"edit",str("version = 999\n"));r=automationSyncReconcile(s,pair);check(hasConflict(r,"unsupported-definition"));check(count(get(r,"writes"))==0);
 a=observation("a");b=observation("b");file(a,"same",definition("same","ACTIVE","A"));file(b,"same",definition("same","ACTIVE","B"));r=automationSyncReconcile(nullptr,observations(a,b));check(hasConflict(r,"untracked-id-collision"));check(count(get(r,"unsafe"))==1);check(count(get(r,"writes"))==0);
 a=observation("a");b=observation("b");file(a,"delete",definition("delete","ACTIVE"));pair=observations(a,b);s=apply(automationSyncReconcile(nullptr,pair),pair);file(b,"delete",definition("delete","DELETED"));r=automationSyncReconcile(s,pair);check(truth(get(get(get(get(r,"state"),"records"),"delete"),"deleted")));
 check(!automationSyncStateValid(dict()));r=automationSyncReconcile(dict(),pair);check(hasConflict(r,"invalid-state"));check(count(get(r,"writes"))==0);
 // Heartbeat definitions use the same single-owner projection; no new thread IDs are invented.
 a=observation("a");b=observation("b");file(a,"heartbeat",definition("heartbeat","ACTIVE","Watch",true));pair=observations(a,b);r=automationSyncReconcile(nullptr,pair);check(status(get(writeFor(r,"b","heartbeat"),"text"))=="PAUSED");check(std::string(utf8(get(writeFor(r,"b","heartbeat"),"text"))).find("019c6e27-e55b-73d1-87d8-4e01f1f75043")!=std::string::npos);
 // Individually allowed inventories cannot persist an aggregate that fails the next read.
 a=observation("a");b=observation("b");for(unsigned i=0;i<4097;++i){std::string id="capacity-"+std::to_string(i);file(i<4096?a:b,id.c_str(),definition(id.c_str()));}
 r=automationSyncReconcile(nullptr,observations(a,b));check(hasConflict(r,"capacity"));check(count(get(r,"writes"))==0);check(automationSyncStateValid(get(r,"state")));
 std::cout<<tests<<" production automation-sync assertions passed\n";
}
