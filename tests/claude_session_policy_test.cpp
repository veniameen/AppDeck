#include "../src/claude_session_policy.hpp"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
int main(){
 unsigned checks=0;auto check=[&](bool pass){++checks;if(!pass){std::cerr<<"FAIL Claude policy "<<checks<<"\n";std::exit(1);}};
 const char* id="01234567-89AB-CDEF-0123-456789abcdef";
 check(deck::claudeResumeVersion("2.31226.1"));for(const char* v:{"","2.31226.2","2.99999.0","3.0.0","1.31226.1"})check(!deck::claudeResumeVersion(v));
 check(deck::claudeMetadataLeaf("local_01234567-89ab-cdef-0123-456789abcdef.json"));
 for(const char* n:{"scheduled-tasks.json","auth.json","Cookies","local_../other.json","local_01234567-89ab-cdef-0123-456789abcdef.json.previous","local_01234567-89ab-cdef-0123-456789abcdeg.json"})check(!deck::claudeMetadataLeaf(n));
 check(deck::claudeProjectPath("/Users/example/Project with spaces"));check(deck::claudeProjectPath("/Users/example/.projects/repo"));
 for(const char* p:{"/","relative","/a/../b","/a/./b","/a/..","/a/.","/a\nb"})check(!deck::claudeProjectPath(p));
 check(!deck::claudeProjectPath(std::string(4097,'/').c_str()));
 char out[80];check(deck::claudeResumeURL(id,out,sizeof out));check(!strcmp(out,"claude://resume?session=01234567-89ab-cdef-0123-456789abcdef"));
 for(const char* bad:{"","../file","01234567-89ab-cdef-0123-456789abcdef&account=other","01234567-89ab-cdef-0123-456789abcdeg"})check(!deck::claudeResumeURL(bad,out,sizeof out));
 for(unsigned size=0;size<80;++size){memset(out,'!',sizeof out);bool ok=deck::claudeResumeURL(id,out,size);check(ok==(size>=61));if(size<80)check(out[size]=='!');if(ok)check(strlen(out)==60);}
 for(bool live:{false,true})for(bool same:{false,true})for(bool other:{false,true})check(deck::claudeLeaseAllows(live,same,other)==(!other&&(!live||same)));
 std::cout<<checks<<" Claude session policy assertions passed\n";
}
