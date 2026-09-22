// Exercise the production reconciliation engine without macOS or user data.
#include "../src/core.hpp"
#include "../src/edge_policy.hpp"
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
struct Node {
 enum Kind {String,Array,Dictionary,Number} kind;
 std::string text;
 long value=0;
 std::vector<Node*> children;
 std::map<std::string,Node*> fields;
 explicit Node(Kind k):kind(k){}
};
using Obj=Node*;using UInt=unsigned long;using Int=long;
std::vector<std::unique_ptr<Node>> arena;
Obj make(Node::Kind k){arena.emplace_back(std::make_unique<Node>(k));return arena.back().get();}
Obj str(const char* s){Obj n=make(Node::String);n->text=s?s:"";return n;}
Obj num(Int v){Obj n=make(Node::Number);n->value=v;return n;}
Obj boolean(bool v){return num(v?1:0);}
Obj array(){return make(Node::Array);}Obj dict(){return make(Node::Dictionary);}
const char* utf8(Obj n){return n?n->text.c_str():"";}
Int integer(Obj n){return n?n->value:0;}bool truth(Obj n){return integer(n)!=0;}
Obj get(Obj n,const char* key){if(!n)return nullptr;auto i=n->fields.find(key);return i==n->fields.end()?nullptr:i->second;}
void put(Obj n,const char* key,Obj v){if(v)n->fields[key]=v;}
void erase(Obj n,const char* key){n->fields.erase(key);}
void add(Obj n,Obj v){if(v)n->children.push_back(v);}
UInt count(Obj n){return n?static_cast<UInt>(n->kind==Node::Dictionary?n->fields.size():n->children.size()):0;}
Obj at(Obj n,UInt i){return n->children.at(i);}
bool isClass(Obj n,const char* name){return n&&((n->kind==Node::String&&deck::equal(name,"NSString"))||
 (n->kind==Node::Array&&deck::equal(name,"NSArray"))||(n->kind==Node::Dictionary&&deck::equal(name,"NSDictionary"))||
 (n->kind==Node::Number&&deck::equal(name,"NSNumber")));}
template<class R> R send(Obj n,const char* selector){
 if constexpr(std::is_same_v<R,Obj>){if(!deck::equal(selector,"allKeys"))std::abort();Obj keys=array();for(const auto& field:n->fields)add(keys,str(field.first.c_str()));return keys;}
 else {if(deck::equal(selector,"doubleValue"))return static_cast<R>(n->value);if(!deck::equal(selector,"length"))std::abort();return static_cast<R>(n->text.size());}
}
template<class R> R send(Obj n,const char* selector,Obj value){
 if(!deck::equal(selector,"containsObject:"))std::abort();
 for(Obj entry:n->children)if(entry==value||(entry->kind==Node::String&&value->kind==Node::String&&entry->text==value->text))return static_cast<R>(true);
 return static_cast<R>(false);
}
template<class R> R send(Obj n,const char* selector,Obj value,UInt index){
 static_assert(std::is_same_v<R,void>);if(!deck::equal(selector,"insertObject:atIndex:"))std::abort();
 n->children.insert(n->children.begin()+static_cast<long>(index),value);
}
#include "../src/workspace_filter.hpp"
#include "../src/project_sync.hpp"

bool equalTree(Obj a,Obj b){
 if(!a||!b)return a==b;if(a->kind!=b->kind||a->text!=b->text||a->value!=b->value||a->fields.size()!=b->fields.size()||a->children.size()!=b->children.size())return false;
 for(UInt i=0;i<a->children.size();++i)if(!equalTree(a->children[i],b->children[i]))return false;
 for(const auto& entry:a->fields)if(!equalTree(entry.second,get(b,entry.first.c_str())))return false;return true;
}
Obj copyTree(Obj source){
 if(!source)return nullptr;Obj result=make(source->kind);result->text=source->text;result->value=source->value;
 for(Obj child:source->children)add(result,copyTree(child));for(const auto& field:source->fields)put(result,field.first.c_str(),copyTree(field.second));return result;
}
Obj project(const char* id,const char* name,const char* path="/workspace/project"){
 Obj p=dict(),paths=array();add(paths,str(path));put(p,"id",str(id));put(p,"name",str(name));put(p,"rootPaths",paths);put(p,"createdAt",num(100));put(p,"updatedAt",num(200));return p;
}
Obj snapshot(std::initializer_list<Obj> projects){
 Obj result=dict(),registry=dict(),order=array();for(Obj p:projects){put(registry,utf8(get(p,"id")),p);add(order,get(p,"id"));}
 put(result,"local-projects",registry);put(result,"project-order",order);return result;
}
Obj observation(const char* profile,Obj snap){Obj o=dict();put(o,"profileId",str(profile));put(o,"snapshot",snap);return o;}
Obj batch(std::initializer_list<Obj> observations){Obj result=array();for(Obj o:observations)add(result,o);return result;}
Obj reconcile(Obj state,std::initializer_list<Obj> observations){return projectSyncReconcile(state,batch(observations));}
Obj registry(Obj state){Obj result=projectSyncSnapshot(state);return result?get(result,"local-projects"):nullptr;}
Obj record(Obj state,const char* id="local-p"){return get(get(state,"records"),id);}
const char* name(Obj state,const char* id="local-p"){return utf8(get(get(registry(state),id),"name"));}
Obj seeded(){Obj state=dict();reconcile(state,{observation("a",snapshot({project("local-p","Original")})),observation("b",snapshot({project("local-p","Original")}))});return state;}

int main(){
 unsigned assertions=0;auto check=[&](bool pass,const char* message){++assertions;if(!pass){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}};
 // The first complete observations form a union, with native IDs preserved.
 const char* nativeId="749d7688-afe1-47cd-b737-16b46098971f";
 Obj p=snapshot({project("local-p","P"),project(nativeId,"Native","/workspace/native")}),q=snapshot({project("local-q","Q","/workspace/q")});
 Obj state=dict(),reverse=dict();Obj first=reconcile(state,{observation("b",q),observation("a",p),observation("empty",snapshot({}))});
 reconcile(reverse,{observation("empty",snapshot({})),observation("a",p),observation("b",q)});
 check(truth(get(first,"ok"))&&truth(get(first,"canonicalChanged")),"bootstrap succeeds");
 check(count(registry(state))==3&&get(registry(state),nativeId),"bootstrap is a union preserving native IDs");
 check(equalTree(state,reverse),"bootstrap does not depend on observation order");
 check(projectSyncStateValid(state),"bootstrapped state validates");
 check(!truth(get(reconcile(state,{observation("a",p),observation("b",q)}),"changed")),"unchanged partial profiles are idempotent");
 check(count(registry(state))==3,"a partial baseline never implies deletion of unseen projects");
 // Reads/projections cannot mutate source snapshots, and copies cannot alias them.
 put(get(get(p,"local-projects"),"local-p"),"name",str("Mutated input"));
 check(deck::equal(name(state),"P"),"central records own sanitized project copies");
 Obj projected=projectSyncSnapshot(state);put(get(projected,"local-projects"),"auth",str("local-only"));
 check(!get(registry(state),"auth"),"output snapshot cannot mutate central records");

 // A change in either member contributes to the canonical state; absence of a
 // profile from a batch means unavailable, never removed projects.
 state=seeded();Obj added=snapshot({project("local-p","Original"),project("local-new","Added in B","/workspace/new")});
 check(truth(get(reconcile(state,{observation("b",added)}),"canonicalChanged")),"copy-created project propagates");
 check(get(registry(state),"local-new")&&get(registry(state),"local-p"),"unobserved profile cannot delete projects");
 Obj canonical=projectSyncSnapshot(state);
 check(projectSyncMarkApplied(state,str("a"),canonical),"successful application can advance baseline");
 reconcile(state,{observation("a",snapshot({project("local-new","Added in B","/workspace/new")}))});
 check(!get(registry(state),"local-p")&&truth(get(record(state),"deleted")),"observed removal becomes a tombstone");
 check(!truth(get(reconcile(state,{observation("b",added)}),"canonicalChanged")),"unchanged stale profile cannot resurrect a deleted project");
 Obj edited=snapshot({project("local-p","Offline edit"),project("local-new","Added in B","/workspace/new")});
 Obj stale=reconcile(state,{observation("b",edited)});
 check(!get(registry(state),"local-p")&&count(get(stale,"conflicts"))>0,"editing a stale project cannot resurrect a tombstone");
 check(!truth(get(reconcile(state,{observation("b",edited)}),"changed")),"rejected edit is observed once, not retried forever");
 check(!projectSyncMarkApplied(state,str("b"),added),"a stale write receipt cannot advance the baseline");

 // A valid empty registry deletes the final project; missing/invalid state cannot.
 state=seeded();Obj before=copyTree(state);
 Obj unknown=dict();put(unknown,"unrecognized-registry",dict());
 Obj malformed=snapshot({project("local-p","Broken","ssh://remote/path")});
 Obj unavailable=reconcile(state,{observation("a",unknown),observation("b",malformed),observation("missing",nullptr)});
 check(equalTree(state,before)&&count(get(unavailable,"ignored"))==3,"unknown/malformed/unavailable snapshots preserve baselines and projects");
 check(truth(get(reconcile(state,{observation("a",snapshot({}))}),"canonicalChanged")),"a valid empty registry removes the last project");
 check(count(registry(state))==0&&projectSyncStateValid(state),"empty canonical registry remains valid");
 check(projectSyncCleanSnapshot(projectSyncSnapshot(state))!=nullptr,"empty canonical snapshot remains recognized");
 Obj newcomer=reconcile(state,{observation("newcomer",snapshot({project("local-p","Stale newcomer")}))});
 check(!truth(get(newcomer,"canonicalChanged"))&&count(registry(state))==0,"newly joined stale profile cannot resurrect tombstones");

 // Re-adding after actually observing/applying deletion is a new generation.
 check(projectSyncMarkApplied(state,str("a"),projectSyncSnapshot(state)),"deletion receipt is acknowledged");
 reconcile(state,{observation("a",snapshot({project("local-p","Explicitly re-added")}))});
 check(deck::equal(name(state),"Explicitly re-added")&&!truth(get(record(state),"deleted")),"explicit re-add after observed deletion succeeds");
 Int generation=integer(get(record(state),"generation"));
 Obj oldRemoval=reconcile(state,{observation("b",snapshot({}))});
 check(deck::equal(name(state),"Explicitly re-added")&&count(get(oldRemoval,"conflicts"))>0,"offline deletion of the old generation cannot delete a re-added project");
 check(integer(get(record(state),"generation"))==generation,"ignored old deletion preserves generation");

 // Concurrent deletion beats edits regardless of batch order, and its stored
 // recovery entry remains the pre-conflict project rather than a losing edit.
 state=seeded();reverse=seeded();Obj deletion=observation("a",snapshot({})),edit=observation("b",snapshot({project("local-p","Concurrent edit")}));
 Obj conflict=reconcile(state,{edit,deletion});reconcile(reverse,{deletion,edit});
 check(equalTree(state,reverse),"simultaneous deletion/edit is iteration-order independent");
 check(count(registry(state))==0&&truth(get(record(state),"deleted")),"deletion wins concurrent edit");
 check(deck::equal(utf8(get(get(record(state),"project"),"name")),"Original"),"tombstone retains the deleted project for recovery");
 check(count(get(conflict,"conflicts"))>0,"conflicting edit is reported");
 state=seeded();reconcile(state,{observation("b",snapshot({project("local-p","Edit observed first")}))});
 reconcile(state,{observation("a",snapshot({}))});
 check(count(registry(state))==0,"deletion also wins when concurrent edit was observed in a previous batch");

 // Two edits have a deterministic causal-revision, then profile-id ordering.
 // The same winner is selected if the observations arrive in separate batches.
 state=seeded();reverse=seeded();Obj aa=observation("a",snapshot({project("local-p","A edit")})),bb=observation("b",snapshot({project("local-p","B edit")}));
 reconcile(state,{aa,bb});reconcile(reverse,{bb,aa});
 check(equalTree(state,reverse)&&deck::equal(name(state),"B edit"),"concurrent edit winner is deterministic");
 Obj sequentialA=seeded(),sequentialB=seeded();reconcile(sequentialA,{aa});reconcile(sequentialA,{bb});reconcile(sequentialB,{bb});reconcile(sequentialB,{aa});
 check(deck::equal(name(sequentialA),"B edit")&&deck::equal(name(sequentialB),"B edit"),"concurrent edit arbitration survives staggered observations");
 check(projectSyncMarkApplied(state,str("a"),projectSyncSnapshot(state)),"winner is applied before a later edit");
 reconcile(state,{observation("a",snapshot({project("local-p","Later causal A edit")}))});
 check(deck::equal(name(state),"Later causal A edit"),"a causally newer edit wins regardless of profile-id tie-break");

 // Joining later can add an unknown project but never silently replace a known
 // project with older metadata from a profile with no synchronization baseline.
 Obj joining=reconcile(state,{observation("fresh",snapshot({project("local-p","Old name"),project("local-fresh","New project","/workspace/fresh")}))});
 check(truth(get(joining,"canonicalChanged"))&&get(registry(state),"local-fresh"),"new profile contributes unknown project");
 check(deck::equal(name(state),"Later causal A edit"),"new profile does not replace known project");

 // Duplicate observations are ambiguous; neither is allowed to imply deletion.
 before=copyTree(state);Obj dup=reconcile(state,{observation("a",snapshot({})),observation("a",snapshot({project("local-p","Wrong")}))});
 check(equalTree(state,before)&&count(get(dup,"ignored"))==1,"duplicate profile observations are rejected as a unit");
 Obj broken=copyTree(state);put(broken,"order",array());Obj brokenBefore=copyTree(broken);
 check(!truth(get(reconcile(broken,{observation("a",snapshot({}))}),"ok"))&&equalTree(broken,brokenBefore),"corrupt persistent order cannot produce a partial canonical snapshot");
 broken=copyTree(state);put(broken,"version",num(2));brokenBefore=copyTree(broken);
 check(!truth(get(reconcile(broken,{observation("a",snapshot({}))}),"ok"))&&equalTree(broken,brokenBefore),"unknown persistent state version fails without mutation");
 Obj noData=dict();reconcile(noData,{observation("a",nullptr)});check(count(noData)==0,"unavailable first launch does not bootstrap an empty canonical state");

 // Foreign account entries and secrets are never projected; unknown local shapes
 // invalidate the complete observation instead of silently erasing missing IDs.
 state=dict();Obj mixed=snapshot({project("local-p","Local"),project("cloud:account:project","Cloud")});
 put(mixed,"auth",str("DO_NOT_COPY"));put(mixed,"electron-persisted-atom-state",str("DO_NOT_COPY"));
 reconcile(state,{observation("a",mixed)});canonical=projectSyncSnapshot(state);
 check(count(get(canonical,"local-projects"))==1&&!get(get(canonical,"local-projects"),"cloud:account:project"),"account-bound projects remain outside shared registry");
 check(count(canonical)==4&&!get(canonical,"auth")&&!get(canonical,"electron-persisted-atom-state"),"canonical snapshot is restricted to four workspace keys");
 before=copyTree(state);Obj invalidLocal=snapshot({project("local-p","Good"),project("local-invalid","Invalid","relative")});
 reconcile(state,{observation("a",invalidLocal)});check(equalTree(state,before),"one malformed local entry invalidates the whole observation");
 check(projectSyncStateValid(state),"final state validates");
 std::cout<<assertions<<" production project-sync assertions passed\n";
}
