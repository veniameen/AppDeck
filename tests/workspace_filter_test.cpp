// Test the exact production dictionary projection without macOS/Foundation.
#include "../src/core.hpp"
#include "../src/edge_policy.hpp"
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include <iostream>
#include <type_traits>
struct Node {enum Kind{String,Array,Dictionary,Number} kind;std::string text;std::vector<Node*> children;std::map<std::string,Node*> fields;explicit Node(Kind k):kind(k){}};
using Obj=Node*;using UInt=unsigned long;
std::vector<std::unique_ptr<Node>> arena;
Obj make(Node::Kind k){arena.emplace_back(std::make_unique<Node>(k));return arena.back().get();}
Obj str(const char* s){Obj o=make(Node::String);o->text=s?s:"";return o;}
Obj number(){return make(Node::Number);}
Obj array(){return make(Node::Array);}Obj dict(){return make(Node::Dictionary);}
const char* utf8(Obj o){return o?o->text.c_str():"";}
Obj get(Obj o,const char* key){if(!o)return nullptr;auto i=o->fields.find(key);return i==o->fields.end()?nullptr:i->second;}
void put(Obj o,const char* key,Obj v){o->fields[key]=v;}void add(Obj a,Obj v){a->children.push_back(v);}
UInt count(Obj o){return o?(UInt)o->children.size():0;}Obj at(Obj o,UInt i){return o->children.at(i);}
bool isClass(Obj o,const char* k){return o&&((o->kind==Node::String&&deck::equal(k,"NSString"))||(o->kind==Node::Array&&deck::equal(k,"NSArray"))||(o->kind==Node::Dictionary&&deck::equal(k,"NSDictionary"))||(o->kind==Node::Number&&deck::equal(k,"NSNumber")));}
template<class R> R send(Obj o,const char* selector){
 if constexpr(std::is_same_v<R,Obj>){if(!deck::equal(selector,"allKeys"))std::abort();Obj keys=array();for(auto& f:o->fields)add(keys,str(f.first.c_str()));return keys;}
 else{if(!deck::equal(selector,"length"))std::abort();return static_cast<R>(o->text.size());}
}
template<class R> R send(Obj o,const char* selector,Obj p){if(!deck::equal(selector,"containsObject:"))std::abort();for(Obj x:o->children)if(x==p||(x->kind==Node::String&&p->kind==Node::String&&x->text==p->text))return static_cast<R>(true);return static_cast<R>(false);}
#include "../src/workspace_filter.hpp"
Obj project(const char* id,const char* name,const char* path){Obj e=dict();put(e,"id",str(id));put(e,"name",str(name));Obj r=array();add(r,str(path));put(e,"rootPaths",r);put(e,"createdAt",number());put(e,"updatedAt",number());return e;}
bool has(Obj list,const char* text){for(Obj x:list->children)if(x->text==text)return true;return false;}
int main(){
 unsigned tests=0;auto check=[&](bool b){++tests;if(!b){std::cerr<<"FAIL filter assertion "<<tests<<'\n';std::exit(1);}};
 check(!sanitizeWorkspaceSnapshot(nullptr));check(!sanitizeWorkspaceSnapshot(str("bad root")));check(!sanitizeWorkspaceSnapshot(dict()));
 // ---- legacy roots only (older Desktop builds) ----
 Obj d=dict(),roots=array();put(d,"electron-saved-workspace-roots",roots);
 for(auto path:{"/Users/test/one","/Users/test/two","/Users/test/one","relative","ssh://secret/remote","/tmp/new\nline"})add(roots,str(path));add(roots,number());
 Obj labels=dict();put(labels,"/Users/test/one",str("First"));put(labels,"/Users/test/two",dict());put(labels,"auth",str("sensitive"));put(d,"electron-workspace-root-labels",labels);
 Obj order=array();for(auto path:{"/Users/test/two","project-id-not-path","/Users/test/two"})add(order,str(path));put(d,"project-order",order);
 for(auto key:{"auth","access_token","electron-persisted-atom-state","thread-workspace-root-hints","thread-project-assignments","history"})put(d,key,str("DO_NOT_COPY"));
 Obj out=sanitizeWorkspaceSnapshot(d);check(out);check(out->fields.size()==3);check(count(get(out,"electron-saved-workspace-roots"))==2);
 check(get(get(out,"electron-workspace-root-labels"),"/Users/test/one")->text=="First");check(get(get(out,"electron-workspace-root-labels"),"/Users/test/two")==nullptr);
 check(count(get(out,"project-order"))==2);check(at(get(out,"project-order"),0)->text=="/Users/test/two");check(at(get(out,"project-order"),1)->text=="/Users/test/one");
 for(auto& item:out->fields)check(deck::workspaceKey(item.first.c_str()));
 for(auto key:{"auth","access_token","electron-persisted-atom-state","thread-workspace-root-hints","thread-project-assignments","history"})check(!get(out,key));
 check(get(d,"auth"));check(count(roots)==7); // Input is untouched.
 Obj many=dict(),a=array();put(many,"electron-saved-workspace-roots",a);for(int i=0;i<4097;++i)add(a,str("/same"));check(!sanitizeWorkspaceSnapshot(many));
 Obj bad=dict();put(bad,"electron-saved-workspace-roots",str("not-array"));check(!sanitizeWorkspaceSnapshot(bad));
 Obj onlyRemote=dict(),remote=array();add(remote,str("ssh://host/path"));put(onlyRemote,"electron-saved-workspace-roots",remote);check(!sanitizeWorkspaceSnapshot(onlyRemote));
 Obj tooLong=dict(),ar=array();std::string path(5000,'x');path[0]='/';add(ar,str(path.c_str()));put(tooLong,"electron-saved-workspace-roots",ar);check(!sanitizeWorkspaceSnapshot(tooLong));
 // ---- current registry: local-projects + id order ----
 Obj m=dict(),reg=dict();put(m,"local-projects",reg);
 put(reg,"local-aaaa",project("local-aaaa","Atlas","/Users/test/Atlas"));
 put(reg,"749d7688-afe1-47cd-b737-16b46098971f",project("749d7688-afe1-47cd-b737-16b46098971f","mobile-app","/Users/test/mobile-app"));
 put(reg,"cloud:someone@example.com:p1",project("cloud:someone@example.com:p1","Cloud thing","/Users/test/cloud"));   // account-bound id
 put(reg,"local-mismatch",project("local-other","Mismatch","/Users/test/x"));                                          // id != key
 put(reg,"local-remote",project("local-remote","Remote","ssh://host/repo"));                                           // not a local path
 put(reg,"local-noname",project("local-noname","","/Users/test/y"));put(reg,"local-string",str("not a dictionary"));
 Obj extra=project("local-extra","Extra","/Users/test/extra");put(extra,"accessToken",str("DO_NOT_COPY"));put(reg,"local-extra",extra);
 Obj idOrder=array();for(auto id:{"749d7688-afe1-47cd-b737-16b46098971f","cloud:someone@example.com:p1","unknown-id","local-aaaa","749d7688-afe1-47cd-b737-16b46098971f"})add(idOrder,str(id));put(m,"project-order",idOrder);
 for(auto key:{"app-server-projects-migration-by-host","app-server-project-id-by-legacy-project-id-by-host","thread-project-assignments","selected-project","pinned-thread-ids"})put(m,key,str("DO_NOT_COPY"));
 Obj modern=sanitizeWorkspaceSnapshot(m);check(modern);for(auto& item:modern->fields)check(deck::workspaceKey(item.first.c_str()));
 Obj cleanReg=get(modern,"local-projects");check(cleanReg&&cleanReg->fields.size()==3);
 check(get(cleanReg,"local-aaaa")&&get(cleanReg,"749d7688-afe1-47cd-b737-16b46098971f")&&get(cleanReg,"local-extra"));
 for(auto gone:{"cloud:someone@example.com:p1","local-mismatch","local-remote","local-noname","local-string"})check(!get(cleanReg,gone));
 check(!get(get(cleanReg,"local-extra"),"accessToken"));check(get(get(cleanReg,"local-extra"),"createdAt"));check(get(cleanReg,"local-extra")->fields.size()==5);
 Obj cleanOrder=get(modern,"project-order");check(count(cleanOrder)==3);check(at(cleanOrder,0)->text=="749d7688-afe1-47cd-b737-16b46098971f");check(at(cleanOrder,1)->text=="local-aaaa");check(at(cleanOrder,2)->text=="local-extra");
 check(!has(cleanOrder,"cloud:someone@example.com:p1"));check(!has(cleanOrder,"unknown-id"));
 Obj wrongType=dict();put(wrongType,"local-projects",array());check(!sanitizeWorkspaceSnapshot(wrongType));
 Obj onlyJunk=dict(),junk=dict();put(onlyJunk,"local-projects",junk);put(junk,"cloud:x",project("cloud:x","X","/x"));check(!sanitizeWorkspaceSnapshot(onlyJunk));
 // ---- merge into a copy's existing state: union, nothing of the copy is lost ----
 Obj copy=dict(),copyReg=dict(),copyOrder=array();put(copy,"local-projects",copyReg);put(copy,"project-order",copyOrder);put(copy,"selected-project",str("keep-me"));put(copy,"electron-persisted-atom-state",str("keep-me-too"));
 put(copyReg,"local-aaaa",project("local-aaaa","Atlas OLD NAME","/Users/test/Atlas"));put(copyReg,"local-copyonly",project("local-copyonly","Made in the copy","/Users/test/copyonly"));
 for(auto id:{"local-copyonly","cloud:copy-account:own","local-aaaa"})add(copyOrder,str(id));
 check(mergeWorkspaceSnapshot(copy,modern));
 check(copyReg->fields.size()==4);check(get(get(copyReg,"local-aaaa"),"name")->text=="Atlas");check(get(copyReg,"local-copyonly"));check(get(copyReg,"749d7688-afe1-47cd-b737-16b46098971f"));
 Obj mergedOrder=get(copy,"project-order");check(count(mergedOrder)==5);check(at(mergedOrder,0)->text=="749d7688-afe1-47cd-b737-16b46098971f");check(at(mergedOrder,1)->text=="local-aaaa");check(at(mergedOrder,2)->text=="local-extra");
 check(has(mergedOrder,"local-copyonly"));check(has(mergedOrder,"cloud:copy-account:own")); // the copy's own account-bound entry stays
 check(get(copy,"selected-project")->text=="keep-me");check(get(copy,"electron-persisted-atom-state")->text=="keep-me-too");
 check(!mergeWorkspaceSnapshot(copy,modern)); // idempotent: a second pass changes nothing, so the file is not rewritten
 check(!mergeWorkspaceSnapshot(nullptr,modern));check(!mergeWorkspaceSnapshot(copy,nullptr));check(!mergeWorkspaceSnapshot(str("x"),modern));
 Obj hostile=dict();put(hostile,"local-projects",str("corrupted"));put(hostile,"project-order",str("corrupted"));check(mergeWorkspaceSnapshot(hostile,modern));check(get(hostile,"local-projects")->fields.size()==3);
 std::cout<<tests<<" production workspace-filter assertions passed\n";
}
