#pragma once
#include "core.hpp"
#include "edge_policy.hpp"
namespace deck {
// This is Claude's own resume handler, inspected in this exact Desktop build. Unknown builds
// keep their ordinary profile launches, but do not receive an undocumented resume URL.
inline bool claudeResumeVersion(const char* version){return equal(version,"2.31226.1");}
inline bool claudeMetadataLeaf(const char* name){
 if(length(name)!=47||!prefix(name,"local_")||!suffix(name,".json"))return false;
 char id[37]{};for(Size i=0;i<36;++i)id[i]=name[6+i];return uuid(id);
}
inline bool claudeProjectPath(const char* path){
 if(!absoluteLocalPath(path)||length(path)>4096||equal(path,"/"))return false;
 for(const char* p=path;*p;++p)if(*p=='/'&&(p[1]=='.'&&(p[2]=='/'||!p[2]||(p[2]=='.'&&(p[3]=='/'||!p[3])))))return false;
 return true;
}
inline bool claudeResumeURL(const char* id,char* out,Size capacity){
 const char* head="claude://resume?session=";Size n=length(head);
 if(!out||capacity<n+37||!uuid(id))return false;
 for(Size i=0;i<n;++i)out[i]=head[i];for(Size i=0;i<36;++i){char c=id[i];out[n+i]=c>='A'&&c<='F'?c+('a'-'A'):c;}out[n+36]=0;return true;
}
inline bool claudeLeaseAllows(bool liveOwner,bool sameProfile,bool competingDesktop){return !competingDesktop&&(!liveOwner||sameProfile);}
} // namespace deck
