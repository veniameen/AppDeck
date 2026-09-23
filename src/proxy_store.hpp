// Included inside main.cpp's private namespace, after usage_limits.hpp.
// Proxies: the owner's list of upstream HTTP or SOCKS5 proxies, a default for every profile and a
// per-profile choice ("default", "none" or a proxy id). The list lives in proxies.plist (0600) next to
// state.plist and never in state.plist, the menus or the diagnostics: its address lines may carry
// logins and passwords. Chromium cannot send proxy credentials, so a profile that uses a proxy is
// started with its own local bridge, `Contents/MacOS/appdeck-proxy` (src/proxy_bridge.cpp): a loopback
// HTTP proxy that forwards through the upstream and adds the credentials. The bridge receives its
// configuration on stdin (never argv or the environment) and exits when the window's process exits;
// the bridges for limit probes watch AppDeck itself.

Obj proxies=nullptr;           // [{id,name,scheme,lines[],user,pass,check{at,ok,total,fastest,auth}}]
Obj proxyProbeBridges=nullptr; // proxy id -> {task,writer,port}: bridges for limit probes
Obj proxyTasks=nullptr;        // every bridge started, so their processes are reaped
Obj proxyChecking=nullptr;     // ids with a check in flight

Obj proxyPath(){return join(dataRoot,"proxies.plist");}
bool proxyValid(Obj x){
 deck::ProxyScheme s;
 return isClass(x,"NSDictionary")&&isClass(get(x,"id"),"NSString")&&deck::uuid(utf8(get(x,"id")))&&isClass(get(x,"name"),"NSString")&&
  isClass(get(x,"scheme"),"NSString")&&deck::proxySchemeParse(utf8(get(x,"scheme")),s)&&isClass(get(x,"lines"),"NSArray");
}
void proxyLoad(){
 Obj p=proxyPath();Obj loaded=exists(p)&&!symlink(p)?mutablePlist(p):nullptr;Obj list=array();
 if(isClass(loaded,"NSArray"))for(UInt i=0;i<count(loaded);++i){Obj x=at(loaded,i);if(!proxyValid(x))continue;
  for(const char* k:{"user","pass"})if(get(x,k)&&!isClass(get(x,k),"NSString"))erase(x,k);
  if(get(x,"check")&&!isClass(get(x,"check"),"NSDictionary"))erase(x,"check");
  Obj lines=get(x,"lines");for(UInt k=count(lines);k>0;--k)if(!isClass(at(lines,k-1),"NSString"))send<void>(lines,"removeObjectAtIndex:",k-1);
  add(list,x);}
 proxies=keep(list);proxyProbeBridges=keep(dict());proxyTasks=keep(array());proxyChecking=keep(send(cls("NSMutableSet"),"set"));
 if(get(state,"defaultProxy")&&!isClass(get(state,"defaultProxy"),"NSString"))erase(state,"defaultProxy");
}
bool proxySave(){
 if(previewMode||!proxies)return true;Obj p=proxyPath();if(symlink(p))return false;
 if(!send<bool>(proxies,"writeToFile:atomically:",p,true)){showError(str(T("Could not save the proxies","Не удалось сохранить прокси")),str(T("Check the permissions of the AppDeck folder in Library/Application Support.","Проверьте права на папку AppDeck в Library/Application Support.")));return false;}
 chmod(utf8(p),0600);return true;
}
Obj proxyById(Obj id){if(!isClass(id,"NSString"))return nullptr;for(UInt i=0;i<count(proxies);++i)if(same(get(at(proxies,i),"id"),id))return at(proxies,i);return nullptr;}
Obj proxyDefault(){return proxyById(get(state,"defaultProxy"));}
// The proxy a profile uses: its own choice, else the default. A choice whose proxy was removed means none.
Obj proxyFor(Obj p){
 Obj choice=get(p,"proxy");if(same(choice,str("none")))return nullptr;
 if(isClass(choice,"NSString")&&!same(choice,str("default")))return proxyById(choice);
 return proxyDefault();
}
// The name to show on a card: what the running window was started with, else what the next launch will use.
Obj proxyShownName(Obj p){if(running(p)){Obj used=get(p,"proxyUsed");return isClass(used,"NSString")?used:nullptr;}Obj x=proxyFor(p);return x?get(x,"name"):nullptr;}

// ---- the bridge's configuration: scheme, then one endpoint per usable address (credentials hex-encoded) ----
void proxyHex(const char* s,char* out,deck::Size cap){
 const char* digits="0123456789abcdef";deck::Size o=0;if(!s||!*s){if(cap>1){out[0]='-';out[1]=0;}return;}
 for(const unsigned char* p=(const unsigned char*)s;*p&&o+2<cap;++p){out[o++]=digits[*p>>4];out[o++]=digits[*p&15];}out[o]=0;
}
Obj proxyConfig(Obj x,Obj* why){
 Obj text=cat(cat(str("scheme "),get(x,"scheme")),str("\n"));Obj lines=get(x,"lines");Int usable=0;
 const char* user=isClass(get(x,"user"),"NSString")?utf8(get(x,"user")):"";const char* pass=isClass(get(x,"pass"),"NSString")?utf8(get(x,"pass")):"";
 for(UInt i=0;i<count(lines)&&usable<32;++i){deck::ProxyEndpoint e;if(!deck::proxyParse(utf8(at(lines,i)),e))continue;deck::proxyCredentials(e,user,pass);
  char u[2*deck::proxyFieldCap+2],pw[2*deck::proxyFieldCap+2],line[4*deck::proxyFieldCap+300];proxyHex(e.user,u,sizeof u);proxyHex(e.pass,pw,sizeof pw);
  snprintf(line,sizeof line,"endpoint %s %u %s %s\n",e.host,e.port,u,pw);text=cat(text,str(line));++usable;memset(line,0,sizeof line);memset(pw,0,sizeof pw);}
 if(!usable){if(why)*why=str(T("The proxy has no valid address. Edit it on the Proxy page.","У прокси нет корректного адреса. Исправьте его на странице «Прокси».")) ;return nullptr;}
 return cat(text,str("\n"));
}
Obj proxyHelper(){Obj p=send(send(cls("NSBundle"),"mainBundle"),"pathForAuxiliaryExecutable:",str("appdeck-proxy"));return p&&send<bool>(fileManager,"isExecutableFileAtPath:",p)?p:nullptr;}
// Starts the helper in `serve` or `check` mode with the configuration on its stdin.
Obj proxySpawn(const char* mode,Obj config,Obj* why,Obj* writer,int* readFd,bool track=true){
 Obj helper=proxyHelper();if(!helper){if(why)*why=str(T("The proxy bridge is missing from AppDeck.app. Reinstall AppDeck.","В AppDeck.app нет моста прокси. Переустановите AppDeck."));return nullptr;}
 Obj in=send(cls("NSPipe"),"pipe"),out=send(cls("NSPipe"),"pipe");Obj task=make("NSTask");if(!in||!out||!task){if(task)drop(task);return nullptr;}
 send<void>(task,"setExecutableURL:",send(cls("NSURL"),"fileURLWithPath:",helper));send<void>(task,"setArguments:",send(cls("NSArray"),"arrayWithObject:",str(mode)));
 send<void>(task,"setEnvironment:",dict());send<void>(task,"setStandardInput:",in);send<void>(task,"setStandardOutput:",out);send<void>(task,"setStandardError:",send(cls("NSFileHandle"),"fileHandleWithNullDevice"));
 Obj error=nullptr;if(!send<bool>(task,"launchAndReturnError:",&error)){drop(task);if(why)*why=str(T("The proxy bridge did not start.","Мост прокси не запустился."));return nullptr;}
 Obj w=send(in,"fileHandleForWriting");int wfd=send<int>(w,"fileDescriptor");fcntl(wfd,73,1); // F_SETNOSIGPIPE: a bridge that died must not take AppDeck down
 const char* bytes=utf8(config);deck::Size n=strlen(bytes);write(wfd,bytes,n);
 *writer=w;*readFd=send<int>(send(out,"fileHandleForReading"),"fileDescriptor");
 if(track){add(proxyTasks,task);drop(task);}else autoRelease(task); // tracked tasks are reaped on the main thread
 return task;
}
// A bridge serving one window (or AppDeck's own limit probes): {task, writer, port}.
Obj proxyServe(Obj x,Obj* why){
 Obj config=proxyConfig(x,why);if(!config)return nullptr;Obj writer=nullptr;int rfd=-1;Obj task=proxySpawn("serve",config,why,&writer,&rfd);if(!task)return nullptr;
 char buf[128];deck::Size used=0;buf[0]=0;double deadline=usageUptime()+5;
 while(!strchr(buf,'\n')&&usageFill(rfd,buf,sizeof buf,used,deadline))buf[used]=0;
 int port=0;if(deck::prefix(buf,"port "))for(const char* p=buf+5;*p>='0'&&*p<='9';++p)port=port*10+(*p-'0');
 if(port<=0||port>65535){send<void>(writer,"closeFile");send<void>(task,"terminate");if(why)*why=str(T("The proxy bridge did not answer.","Мост прокси не ответил."));return nullptr;}
 Obj b=dict();put(b,"task",task);put(b,"writer",writer);put(b,"port",num(port));return b;
}
// The bridge serves until this process exits; AppDeck's end of the pipe closes (the bridge keeps running).
void proxyWatch(Obj b,int pid){char line[32];snprintf(line,sizeof line,"watch %d\n",pid);Obj w=get(b,"writer");write(send<int>(w,"fileDescriptor"),line,strlen(line));send<void>(w,"closeFile");}
void proxyAbandon(Obj b){if(b)send<void>(get(b,"writer"),"closeFile");} // no watch line: the bridge leaves at once
Obj proxyURL(int port){return formatInt("http://127.0.0.1:%ld",port);}
// What an app is started with: Chromium's switches, and the environment for Node and CLI helpers.
void proxyApplyEnv(Obj env,int port){
 Obj url=proxyURL(port);for(const char* k:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"})put(env,k,url);
 put(env,"NO_PROXY",str(deck::proxyBypassEnv));put(env,"no_proxy",str(deck::proxyBypassEnv));put(env,"NODE_USE_ENV_PROXY",str("1"));
}
void proxyApply(Obj env,Obj args,int port){
 add(args,cat(str("--proxy-server="),proxyURL(port)));add(args,cat(str("--proxy-bypass-list="),str(deck::proxyBypassChromium)));
 add(args,str("--disable-quic"));add(args,str("--force-webrtc-ip-handling-policy=disable_non_proxied_udp")); // no traffic around the proxy
 proxyApplyEnv(env,port);
}
// Limit probes: one bridge per proxy for AppDeck's own lifetime.
int proxyProbePort(Obj x,Obj* why){
 const char* key=utf8(get(x,"id"));Obj b=get(proxyProbeBridges,key);
 if(b&&send<bool>(get(b,"task"),"isRunning"))return (int)integer(get(b,"port"));
 b=proxyServe(x,why);if(!b)return 0;proxyWatch(b,send<int>(send(cls("NSProcessInfo"),"processInfo"),"processIdentifier"));put(proxyProbeBridges,key,b);return (int)integer(get(b,"port"));
}
// After an edit or removal the probe bridge restarts with the new configuration on its next use.
void proxyForget(Obj x){const char* key=utf8(get(x,"id"));Obj b=get(proxyProbeBridges,key);if(b){send<void>(get(b,"task"),"terminate");erase(proxyProbeBridges,key);}}
void proxyReap(){for(UInt i=count(proxyTasks);i>0;--i)if(!send<bool>(at(proxyTasks,i-1),"isRunning"))send<void>(proxyTasks,"removeObjectAtIndex:",i-1);}
// Limit probes of a profile behind a proxy go through it too; false = the proxy is unavailable (cached numbers stay).
bool usageProxy(Obj p,Obj env){
 Obj x=proxyFor(p);if(!x)return true;Obj why=nullptr;int port=proxyProbePort(x,&why);
 if(port<=0){Obj old=usageOf(p);Obj u=old?send(old,"mutableCopy"):make("NSMutableDictionary");put(u,"error",str("proxy"));put(u,"checked",real(nowUnix()));put(p,"usage",u);drop(u);return false;}
 proxyApplyEnv(env,port);return true;
}
// ---- checks, on a worker thread: `appdeck-proxy check` proves each address with a tunnel to api.openai.com:443 ----
void proxyCheckWorker(Obj,Sel,Obj request){
 Pool pool;Obj result=dict();put(result,"id",get(request,"id"));Obj lines=array();put(result,"lines",lines);
 Obj writer=nullptr;int rfd=-1;Obj task=proxySpawn("check",get(request,"config"),nullptr,&writer,&rfd,false);
 if(task){send<void>(writer,"closeFile");char buf[4096];deck::Size used=0;double deadline=usageUptime()+32*40; // endpoints are probed one after another, up to ~40 s each
  while(usageFill(rfd,buf,sizeof buf,used,deadline)){}buf[used]=0;
  for(char* p=buf;*p;){char* e=strchr(p,'\n');if(e)*e=0;if(*p)add(lines,str(p));if(!e)break;p=e+1;}
  if(send<bool>(task,"isRunning"))send<void>(task,"terminate");}
 else put(result,"failed",boolean(true));
 send<void>(controller,"performSelectorOnMainThread:withObject:waitUntilDone:",sel("proxyCheckFinished:"),result,false);
}
