// Included inside main.cpp's private namespace.
// Public sysctl(KERN_PROCARGS2): the argument list `ps` prints for the user's own processes.
// Only AppDeck's own --user-data-dir marker is extracted; the environment block is never read.
Obj processUserDataDir(int pid){
 if(pid<=0)return nullptr;
 static char* buffer=nullptr;static Size capacity=0;
 if(!buffer){int argmax=0;Size n=sizeof argmax;int query[2]={1,8}; // CTL_KERN, KERN_ARGMAX
  if(sysctl(query,2,&argmax,&n,nullptr,0)!=0||argmax<4096||argmax>16*1024*1024)return nullptr;
  buffer=(char*)calloc((Size)argmax,1);if(!buffer)return nullptr;capacity=(Size)argmax;
 }
 int mib[3]={1,49,pid};Size size=capacity; // CTL_KERN, KERN_PROCARGS2
 if(sysctl(mib,3,buffer,&size,nullptr,0)!=0||size<=sizeof(int)||size>capacity)return nullptr;
 int argc=0;memcpy(&argc,buffer,sizeof argc);const char* p=buffer+sizeof(int),*end=buffer+size;
 while(p<end&&*p)++p;   // executable path
 while(p<end&&!*p)++p;  // alignment padding
 const char* marker="--user-data-dir=";Size m=strlen(marker);
 for(int i=0;i<argc&&p<end;++i){Size len=0;while(p+len<end&&p[len])++len;
  if(p+len>=end)break;  // unterminated tail: not a complete argument
  if(len>m&&strncmp(p,marker,m)==0)return str(p+m);
  p+=len+1;
 }
 return nullptr;
}
