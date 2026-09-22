// Included where mac:: is in scope (main.cpp's private namespace, the API smoke test).
// The worker-thread half of the account-limit feature: start the vendor's own helper, exchange a few
// lines with it, return plain data. No AppDeck state is touched here, so the smoke test runs the very
// same code against a scratch configuration.
//   request: {kind:"codex"|"claude", binary, scratch (cwd), environment}
//   result : {account:{signedIn,kind,email,plan}, limits:{windows,plan,…}, error, detail}
#include "usage_parse.hpp"
struct PollFd{int fd;short events,revents;};
double usageUptime(){return send<double>(send(cls("NSProcessInfo"),"processInfo"),"systemUptime");}
// Waits for readable data and appends it; false on EOF, error, full buffer or deadline.
bool usageFill(int fd,char* buf,Size cap,Size& used,double deadline){
 for(;;){double left=deadline-usageUptime();if(left<=0||used+1>=cap)return false;PollFd pf{fd,1,0};int ready=poll(&pf,1,left>.5?500:(int)(left*1000)+1);
  if(ready<0){send<void>(cls("NSThread"),"sleepForTimeInterval:",.05);continue;}if(!ready)continue;long n=read(fd,buf+used,cap-used-1);if(n<=0)return false;used+=(Size)n;return true;}
}
// Pops one complete line (without '\n') as JSON; nil when no full line is buffered. `more` tells the two apart.
Obj usageLine(char* buf,Size& used,bool& more){
 Size i=0;while(i<used&&buf[i]!='\n')++i;if(i>=used){more=false;return nullptr;}more=true;
 Obj msg=usageJSON(buf,i);Size rest=used-i-1;for(Size k=0;k<rest;++k)buf[k]=buf[i+1+k];used=rest;return msg;
}
Obj usageTask(Obj request,Obj arguments,Obj in,Obj out){
 Obj task=make("NSTask");if(!task)return nullptr;
 send<void>(task,"setExecutableURL:",send(cls("NSURL"),"fileURLWithPath:",get(request,"binary")));send<void>(task,"setArguments:",arguments);
 send<void>(task,"setEnvironment:",get(request,"environment"));send<void>(task,"setCurrentDirectoryURL:",send(cls("NSURL"),"fileURLWithPath:",get(request,"scratch")));
 send<void>(task,"setStandardInput:",in?in:send(cls("NSFileHandle"),"fileHandleWithNullDevice"));send<void>(task,"setStandardOutput:",out);send<void>(task,"setStandardError:",send(cls("NSFileHandle"),"fileHandleWithNullDevice"));
 Obj error=nullptr;if(!send<bool>(task,"launchAndReturnError:",&error)){drop(task);return nullptr;}return task;
}
// EOF on stdin makes both helpers leave on their own; a lingering one gets an ordinary terminate.
void usageReap(Obj task){
 for(int i=0;i<30&&send<bool>(task,"isRunning");++i)send<void>(cls("NSThread"),"sleepForTimeInterval:",.1);
 if(send<bool>(task,"isRunning")){send<void>(task,"terminate");for(int i=0;i<10&&send<bool>(task,"isRunning");++i)send<void>(cls("NSThread"),"sleepForTimeInterval:",.1);}
 drop(task);
}
bool usageSay(int fd,const char* text){Size n=strlen(text);return write(fd,text,n)==(long)n;}

// Codex: the app bundle's `codex app-server`, newline-delimited JSON-RPC. refreshToken:false and
// excludeResetCreditDetails:true are the protocol's own settings for a background poll.
void usageProbeCodex(Obj request,Obj result){
 Obj in=send(cls("NSPipe"),"pipe"),out=send(cls("NSPipe"),"pipe");Obj args=send(cls("NSArray"),"arrayWithObject:",str("app-server"));
 Obj task=in&&out?usageTask(request,args,in,out):nullptr;if(!task){put(result,"error",str("spawn"));return;}
 Obj writer=send(in,"fileHandleForWriting");int wfd=send<int>(writer,"fileDescriptor"),rfd=send<int>(send(out,"fileHandleForReading"),"fileDescriptor");
 fcntl(wfd,73,1); // F_SETNOSIGPIPE: a helper that died early must not take AppDeck down with SIGPIPE
 const Size cap=256*1024;char* buf=(char*)calloc(cap,1);Size used=0;double deadline=usageUptime()+deck::usageProbeTimeout;Obj replies=dict();const char* failure=nullptr;
 auto collect=[&](UInt want){while(count(replies)<want){bool more=true;while(more){Obj msg=usageLine(buf,used,more);
    // Replies only: notifications have no id, server-side requests carry a method.
    if(msg&&!get(msg,"method")&&usageKind(get(msg,"id"),"NSNumber"))send<void>(replies,"setObject:forKey:",msg,formatInt("%ld",integer(get(msg,"id"))));}
   if(count(replies)>=want||!usageFill(rfd,buf,cap,used,deadline))return;}};
 if(!buf)failure="error";
 else if(!usageSay(wfd,"{\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"appdeck\",\"title\":\"AppDeck limits\",\"version\":\"0.7.2\"}}}\n"))failure="spawn";
 else{collect(1);Obj hello=get(replies,"1");
  if(!hello)failure="timeout";else if(!get(hello,"result"))failure="unsupported";
  else if(!usageSay(wfd,"{\"method\":\"initialized\"}\n{\"id\":2,\"method\":\"account/read\",\"params\":{\"refreshToken\":false}}\n{\"id\":3,\"method\":\"account/rateLimits/read\",\"params\":{\"excludeResetCreditDetails\":true}}\n"))failure="timeout";
  else{collect(3);Obj who=get(replies,"2"),limits=get(replies,"3");
   if(who&&get(who,"result"))put(result,"account",usageAccount(get(who,"result")));
   if(limits&&get(limits,"result"))put(result,"limits",usageRecord(get(limits,"result")));
   else if(limits&&get(limits,"error")){Obj problem=get(limits,"error");failure=usageErrorKind(problem);Obj m=usageKind(problem,"NSDictionary")?get(problem,"message"):nullptr;if(usageKind(m,"NSString"))put(result,"detail",m);}
   else failure="timeout";}}
 free(buf);send<void>(writer,"closeFile");usageReap(task);if(failure)put(result,"error",str(failure));
}

// Claude Code: `auth status` (who is signed in to this config folder), then ONE `get_usage` control
// request in --safe-mode (no hooks, plugins, MCP servers or CLAUDE.md; sign-in works normally). No user
// message is sent, so no model turn happens and nothing is billed. `requireSignIn=false` exists for
// the smoke test, which exercises the exchange on a signed-out scratch folder.
void usageProbeClaude(Obj request,Obj result,bool requireSignIn){
 const Size cap=256*1024;char* buf=(char*)calloc(cap,1);if(!buf){put(result,"error",str("error"));return;}
 Obj out=send(cls("NSPipe"),"pipe");Obj who=array();add(who,str("auth"));add(who,str("status"));Obj task=out?usageTask(request,who,nullptr,out):nullptr;
 if(!task){free(buf);put(result,"error",str("spawn"));return;}
 Size n=0;double deadline=usageUptime()+deck::usageProbeTimeout;while(usageFill(send<int>(send(out,"fileHandleForReading"),"fileDescriptor"),buf,cap,n,deadline)){}usageReap(task);
 Obj account=usageAccountClaude(usageJSON(buf,n));put(result,"account",account);if(requireSignIn&&!truth(get(account,"signedIn"))){free(buf);return;} // the caller reports "auth"
 Obj in=send(cls("NSPipe"),"pipe");out=send(cls("NSPipe"),"pipe");Obj args=array();
 for(const char* a:{"-p","--safe-mode","--input-format","stream-json","--output-format","stream-json","--verbose","--no-session-persistence"})add(args,str(a));
 task=in&&out?usageTask(request,args,in,out):nullptr;if(!task){free(buf);put(result,"error",str("spawn"));return;}
 Obj writer=send(in,"fileHandleForWriting");int wfd=send<int>(writer,"fileDescriptor"),rfd=send<int>(send(out,"fileHandleForReading"),"fileDescriptor");fcntl(wfd,73,1);
 Obj reply=nullptr;Size used=0;deadline=usageUptime()+deck::usageProbeTimeout;
 if(usageSay(wfd,"{\"type\":\"control_request\",\"request_id\":\"appdeck-usage\",\"request\":{\"subtype\":\"get_usage\",\"skip_behaviors\":true}}\n"))
  while(!reply){bool more=true;while(more&&!reply){Obj msg=usageLine(buf,used,more);if(msg&&usageKind(get(msg,"type"),"NSString")&&!strcmp(utf8(get(msg,"type")),"control_response"))reply=msg;}
   if(reply||!usageFill(rfd,buf,cap,used,deadline))break;}
 send<void>(writer,"closeFile");usageReap(task);free(buf);
 Obj response=reply?get(reply,"response"):nullptr;
 if(!usageKind(response,"NSDictionary")){put(result,"error",str("timeout"));return;}
 if(!usageKind(get(response,"subtype"),"NSString")||strcmp(utf8(get(response,"subtype")),"success")){put(result,"error",str("unsupported"));Obj m=get(response,"error");if(usageKind(m,"NSString"))put(result,"detail",m);return;}
 Obj record=usageRecordClaude(get(response,"response"));put(result,"limits",record);
 if(!count(get(record,"windows")))put(result,"error",str(truth(get(record,"available"))?"nodata":"noplan"));
}
