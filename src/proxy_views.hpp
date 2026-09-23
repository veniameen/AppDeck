// Included inside main.cpp's private namespace, before buildUI(): the Proxy page, its editor, the
// default and per-profile choice menus and the result of a check. Data and bridges: proxy_store.hpp.
Obj proxyTrim(Obj s){return send(s,"stringByTrimmingCharactersInSet:",send(cls("NSCharacterSet"),"whitespaceAndNewlineCharacterSet"));}
// One line on the row: scheme, number of addresses and what the last check found.
Obj proxySummary(Obj x){
 Int n=0;Obj lines=get(x,"lines");for(UInt i=0;i<count(lines);++i){deck::ProxyEndpoint e;if(deck::proxyParse(utf8(at(lines,i)),e))++n;}
 Obj s=cat(str(same(get(x,"scheme"),str("socks5"))?"SOCKS5":"HTTP"),formatInt(n==1?T(" · 1 address"," · 1 адрес"):T(" · %ld addresses"," · адресов: %ld"),n));
 if(proxyChecking&&send<bool>(proxyChecking,"containsObject:",get(x,"id")))return cat(s,str(T(" · checking…"," · проверяю…")));
 Obj c=get(x,"check");if(!c)return cat(s,str(T(" · not checked yet"," · ещё не проверялся")));
 Int ok=integer(get(c,"ok")),total=integer(get(c,"total"));Obj result;
 if(ok>0){char b[160];snprintf(b,sizeof b,T(" · works: %ld of %ld, fastest %ld ms"," · работает: %ld из %ld, быстрее всего %ld мс"),ok,total,integer(get(c,"fastest")));result=str(b);}
 else result=str(truth(get(c,"auth"))?T(" · login or password rejected"," · логин или пароль отклонены"):T(" · no address answers"," · ни один адрес не отвечает"));
 return cat(cat(cat(s,result),str(" · ")),usageAgeText(send<double>(get(c,"at"),"doubleValue")));
}
// The editor: name, type, addresses (one per line, as the provider gives them) and an optional common login.
bool proxyEdit(Obj x){
 if(previewMode)return false;frontForModal();
 Obj name=x?get(x,"name"):formatInt(T("Proxy %ld","Прокси %ld"),(Int)count(proxies)+1);
 Obj text=x?send(get(x,"lines"),"componentsJoinedByString:",str("\n")):str("");
 Obj user=x&&isClass(get(x,"user"),"NSString")?get(x,"user"):str(""),pass=x&&isClass(get(x,"pass"),"NSString")?get(x,"pass"):str("");
 Int scheme=x&&same(get(x,"scheme"),str("socks5"))?1:0;
 for(;;){
  Obj a=make("NSAlert");send<void>(a,"setMessageText:",str(x?T("Edit proxy","Изменить прокси"):T("Add proxy","Добавить прокси")));
  send<void>(a,"setInformativeText:",str(T("AppDeck starts a local bridge for each window of a profile that uses this proxy; the first address that answers is used.","Для каждого окна профиля с этим прокси AppDeck запускает локальный мост; используется первый отвечающий адрес.")));
  Obj box=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,440,262));
  label(box,T("Name","Название"),rect(0,4,110,18));Obj nameField=send(send(cls("NSTextField"),"alloc"),"initWithFrame:",rect(116,0,324,24));send<void>(nameField,"setStringValue:",name);send<void>(box,"addSubview:",nameField);drop(nameField);
  label(box,T("Type","Тип"),rect(0,38,110,18));const char* kinds[]={"HTTP (CONNECT)","SOCKS5"};Obj kind=popup(box,rect(114,32,200,28),kinds,2,scheme);
  label(box,T("Addresses","Адреса"),rect(0,72,110,18));
  Obj scroll=send(send(cls("NSScrollView"),"alloc"),"initWithFrame:",rect(116,70,324,96));send<void>(scroll,"setHasVerticalScroller:",true);send<void>(scroll,"setBorderType:",(UInt)2);
  Obj view=send(send(cls("NSTextView"),"alloc"),"initWithFrame:",rect(0,0,320,96));send<void>(view,"setRichText:",false);send<void>(view,"setFont:",codeFont(12,0));
  for(const char* off:{"setAutomaticQuoteSubstitutionEnabled:","setAutomaticDashSubstitutionEnabled:","setAutomaticTextReplacementEnabled:","setAutomaticSpellingCorrectionEnabled:","setContinuousSpellCheckingEnabled:"})send<void>(view,off,false);
  send<void>(view,"setString:",text);send<void>(view,"setAutoresizingMask:",(UInt)2);send<void>(scroll,"setDocumentView:",view);drop(view);send<void>(box,"addSubview:",scroll);drop(scroll);
  label(box,T("One per line: host:port or host:port:login:password.","По одному в строке: host:port или host:port:логин:пароль."),rect(116,170,324,16),11,faint());
  label(box,T("Login","Логин"),rect(0,202,110,18));Obj userField=send(send(cls("NSTextField"),"alloc"),"initWithFrame:",rect(116,198,324,24));send<void>(userField,"setStringValue:",user);send<void>(userField,"setPlaceholderString:",str(T("for addresses without their own","для адресов без своего")));send<void>(box,"addSubview:",userField);drop(userField);
  label(box,T("Password","Пароль"),rect(0,236,110,18));Obj passField=send(send(cls("NSSecureTextField"),"alloc"),"initWithFrame:",rect(116,232,324,24));send<void>(passField,"setStringValue:",pass);send<void>(box,"addSubview:",passField);drop(passField);
  send<void>(a,"setAccessoryView:",box);send(a,"addButtonWithTitle:",str(T("Save","Сохранить")));send(a,"addButtonWithTitle:",str(T("Cancel","Отмена")));
  send<void>(send(a,"window"),"setInitialFirstResponder:",nameField);
  bool accepted=send<Int>(a,"runModal")==1000;
  name=proxyTrim(send(nameField,"stringValue"));text=autoRelease(send(send(view,"string"),"copy"));user=proxyTrim(send(userField,"stringValue"));pass=send(passField,"stringValue");scheme=send<Int>(kind,"indexOfSelectedItem");
  keep(name);keep(user);keep(pass);drop(box);drop(a);autoRelease(name);autoRelease(user);autoRelease(pass);
  if(!accepted)return false;
  // Validate every line; report the ones that do not parse (without echoing passwords).
  Obj lines=array(),bad=array();Obj raw=send(text,"componentsSeparatedByCharactersInSet:",send(cls("NSCharacterSet"),"newlineCharacterSet"));
  for(UInt i=0;i<count(raw);++i){Obj line=proxyTrim(at(raw,i));if(!send<UInt>(line,"length"))continue;deck::ProxyEndpoint e;if(deck::proxyParse(utf8(line),e))add(lines,line);else add(bad,formatInt("%ld",(Int)i+1));}
  const char* problem=nullptr;bool listBad=false;
  if(!send<UInt>(name,"length")||send<UInt>(name,"length")>64)problem=T("Enter a name of 1 to 64 characters.","Введите название от 1 до 64 символов.");
  else if(count(bad)){problem=T("These lines are not host:port or host:port:login:password: ","Эти строки не в формате host:port или host:port:логин:пароль: ");listBad=true;}
  else if(!count(lines))problem=T("Add at least one address.","Добавьте хотя бы один адрес.");
  else if(count(lines)>32)problem=T("At most 32 addresses.","Не больше 32 адресов.");
  else if(send<UInt>(user,"length")>255||send<UInt>(pass,"length")>255)problem=T("The login and the password are limited to 255 characters.","Логин и пароль — не длиннее 255 символов.");
  else if(send<UInt>(user,"length")&&!send<UInt>(pass,"length"))problem=T("Enter the password for the common login, or leave both empty.","Введите пароль для общего логина или оставьте оба поля пустыми.");
  if(problem){showError(str(T("The proxy was not saved","Прокси не сохранён")),listBad?cat(str(problem),send(bad,"componentsJoinedByString:",str(", "))):str(problem));continue;}
  Obj target=x;if(!target){target=dict();put(target,"id",uuid());add(proxies,target);}
  put(target,"name",name);put(target,"scheme",str(scheme==1?"socks5":"http"));put(target,"lines",lines);
  if(send<UInt>(user,"length")){put(target,"user",user);put(target,"pass",pass);}else{erase(target,"user");erase(target,"pass");}
  erase(target,"check");proxyForget(target);proxySave();
  note("Proxy saved; credentials stay in proxies.plist (0600).");return true;
 }
}
void proxyAddAction(Obj,Sel,Obj){if(proxyEdit(nullptr)){buildUI();buildMenus();}}
Obj proxyFromSender(Obj sender){Int i=send<Int>(sender,"tag");return i>=0&&(UInt)i<count(proxies)?at(proxies,(UInt)i):nullptr;}
void proxyEditAction(Obj,Sel,Obj sender){Obj x=proxyFromSender(sender);if(x&&proxyEdit(x)){buildUI();buildMenus();}}
void proxyRemoveAction(Obj,Sel,Obj sender){
 Obj x=proxyFromSender(sender);if(!x)return;
 Obj msg=cat(cat(str(T("Remove “","Удалить «")),get(x,"name")),str(T("”?","»?")));
 if(!confirm(utf8(msg),T("Profiles that chose it go back to the default proxy. Running windows keep their connection until they are closed.","Профили, выбравшие его, вернутся к прокси по умолчанию. Работающие окна сохранят соединение, пока их не закроют."),T("Remove","Удалить")))return;
 for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);if(same(get(p,"proxy"),get(x,"id")))erase(p,"proxy");}
 if(same(get(state,"defaultProxy"),get(x,"id")))erase(state,"defaultProxy");
 proxyForget(x);send<void>(proxies,"removeObjectIdenticalTo:",x);proxySave();save();buildUI();buildMenus();
}
void proxyCheckAction(Obj,Sel,Obj sender){
 Obj x=proxyFromSender(sender);if(!x||send<bool>(proxyChecking,"containsObject:",get(x,"id")))return;Obj why=nullptr;Obj config=proxyConfig(x,&why);
 if(!config){showError(str(T("Nothing to check","Нечего проверять")),why);return;}
 Obj request=dict();put(request,"id",get(x,"id"));put(request,"config",config);send<void>(proxyChecking,"addObject:",get(x,"id"));
 send<void>(cls("NSThread"),"detachNewThreadSelector:toTarget:withObject:",sel("proxyCheckWorker:"),controller,request);buildUI();
}
// "ok <i> <ms>" / "auth <i>" / "fail <i> <reason>" per address → a small record on the proxy.
void proxyCheckFinished(Obj,Sel,Obj result){
 Obj x=proxyById(get(result,"id"));send<void>(proxyChecking,"removeObject:",get(result,"id"));if(!x){buildUI();return;}
 Obj lines=get(result,"lines");Int ok=0,total=0,fastest=-1;bool auth=false;
 for(UInt i=0;i<count(lines);++i){const char* l=utf8(at(lines,i));++total;
  if(deck::prefix(l,"ok ")){++ok;const char* ms=strchr(l+3,' ');Int v=0;if(ms)for(++ms;*ms>='0'&&*ms<='9';++ms)v=v*10+(*ms-'0');if(fastest<0||v<fastest)fastest=v;}
  else if(deck::prefix(l,"auth "))auth=true;}
 Obj c=dict();put(c,"at",real(nowUnix()));put(c,"ok",num(ok));put(c,"total",num(total));put(c,"fastest",num(fastest<0?0:fastest));put(c,"auth",boolean(auth&&!ok));put(x,"check",c);
 proxySave();note(ok?"Proxy check: at least one address works.":"Proxy check: no address works.");buildUI();
}
// The default for every profile, chosen from a menu under its button.
void proxyDefaultPickAction(Obj,Sel,Obj sender){Obj id=send(sender,"representedObject");if(isClass(id,"NSString")&&proxyById(id))put(state,"defaultProxy",id);else erase(state,"defaultProxy");save();buildUI();}
void proxyDefaultMenuAction(Obj,Sel,Obj sender){
 Obj m=make("NSMenu");send<void>(m,"setAutoenablesItems:",false);Obj current=proxyDefault();
 Obj none=menuItem(m,T("No proxy","Без прокси"),"proxyDefaultPick:");send<void>(none,"setState:",(Int)(current?0:1));
 if(count(proxies))separator(m);
 for(UInt i=0;i<count(proxies);++i){Obj x=at(proxies,i);Obj it=menuItem(m,utf8(get(x,"name")),"proxyDefaultPick:");send<void>(it,"setRepresentedObject:",get(x,"id"));send<void>(it,"setState:",(Int)(x==current?1:0));}
 send<bool>(m,"popUpMenuPositioningItem:atLocation:inView:",(Obj)nullptr,Point{0,36},sender);drop(m);
}
// A profile's own choice, from its ••• menu: the default, no proxy, or one of the list.
void profileProxyAction(Obj,Sel,Obj sender){
 Obj p=fromSender(sender);if(!p)return;Obj choice=send(sender,"representedObject");
 if(isClass(choice,"NSString")&&(same(choice,str("none"))||proxyById(choice)))put(p,"proxy",choice);else erase(p,"proxy");
 save();buildUI();buildMenus();
}
void proxyProfileMenu(Obj m,Obj p,Int tag){
 Obj top=menuItem(m,T("Proxy","Прокси"),nullptr);Obj sub=make("NSMenu");send<void>(sub,"setAutoenablesItems:",false);send<void>(top,"setSubmenu:",sub);
 Obj choice=get(p,"proxy");bool none=same(choice,str("none")),own=isClass(choice,"NSString")&&!none&&!same(choice,str("default"))&&proxyById(choice);
 Obj d=proxyDefault();Obj title=cat(str(T("Default — ","По умолчанию — ")),d?get(d,"name"):str(T("no proxy","без прокси")));
 Obj it=menuItem(sub,utf8(title),"profileProxy:",tag);send<void>(it,"setState:",(Int)(!none&&!own?1:0));
 it=menuItem(sub,T("No proxy","Без прокси"),"profileProxy:",tag);send<void>(it,"setRepresentedObject:",str("none"));send<void>(it,"setState:",(Int)(none?1:0));
 if(count(proxies))separator(sub);
 for(UInt i=0;i<count(proxies);++i){Obj x=at(proxies,i);it=menuItem(sub,utf8(get(x,"name")),"profileProxy:",tag);send<void>(it,"setRepresentedObject:",get(x,"id"));send<void>(it,"setState:",(Int)(own&&same(choice,get(x,"id"))?1:0));}
 if(running(p)){separator(sub);Obj hint=menuItem(sub,T("Applies at the next launch","Применится при следующем запуске"),nullptr);send<void>(hint,"setEnabled:",false);}
 separator(sub);menuItem(sub,T("Proxy settings…","Настройки прокси…"),"changePage:",3);drop(sub);
}
// The Proxy page.
void buildProxyPage(double x,double w,double footY){
 double titleW=headerButtons(x+w,{{T("Add proxy","Добавить прокси"),"proxyAdd:","plus",true,nullptr}})-x-16;
 pageHeader(x,w,T("NETWORK","СЕТЬ"),str(T("Proxy","Прокси")),str(T("Route profiles through an HTTP or SOCKS5 proxy — for example past the VPN of your router.","Направляйте профили через HTTP- или SOCKS5-прокси — например, в обход VPN роутера.")),titleW);
 caption(rootView,T("FOR ALL PROFILES","ДЛЯ ВСЕХ ПРОФИЛЕЙ"),rect(x,138,w,14));
 Obj box=card(rootView,rect(x,160,w,settingRowH),16);Obj d=proxyDefault();
 const char* current=d?utf8(get(d,"name")):T("No proxy","Без прокси");
 double bw=fitWidth(current,true);if(bw<160)bw=160;if(bw>260)bw=260;
 Obj pick=settingRow(box,0,w,"globe",T("Default proxy","Прокси по умолчанию"),str(T("Profiles set to “Default” use it; each profile can choose its own in its ••• menu.","Его используют профили с выбором «По умолчанию»; свой прокси профиль выбирает в меню •••.")),current,"proxyDefaultMenu:",-1,bw,"chevron.up.chevron.down");
 send<void>(pick,"setImagePosition:",(UInt)8); // NSImageTrailing: the chevron after the name
 caption(rootView,T("PROXIES","ПРОКСИ"),rect(x,160+settingRowH+24,w,14));double top=160+settingRowH+46;
 if(!count(proxies)){Obj c=card(rootView,rect(x,top,w,104),16);iconTile(c,rect(20,24,40,40),"network");
  label(c,T("No proxies yet","Прокси пока нет"),rect(76,28,w-96,20),15,ink(),.3);
  label(c,T("Add the addresses your provider gave you — host:port or host:port:login:password, one per line.","Добавьте адреса от провайдера — host:port или host:port:логин:пароль, по одному в строке."),rect(76,52,w-96,18),13,muted(),0);}
 else{double listH=count(proxies)*settingRowH,room=footY-16-top-44;Obj doc=scrollDocument(rootView,rect(x,top,w,listH<room?listH:room),listH);Obj list=card(doc,rect(0,0,w,listH),16);
  const char* checkTitle=T("Check","Проверить");const char* editTitle=T("Edit…","Изменить…");double cw=fitWidth(checkTitle,true),ew=fitWidth(editTitle,false);
  for(UInt i=0;i<count(proxies);++i){Obj x2=at(proxies,i);double y=i*settingRowH;if(i)settingDivider(list,y,w);
   iconTile(list,rect(16,y+12,32,32),"network");double bx=w-16-32,textW=bx-8-ew-8-cw-16-60;
   labelObj(list,get(x2,"name"),rect(60,y+10,textW,18),13,ink(),.3);Obj s=labelObj(list,proxySummary(x2),rect(60,y+29,textW,16),12,muted(),0);send<void>(s,"setLineBreakMode:",(Int)4);
   iconButton(list,"trash","proxyRemove:",rect(bx,y+12,32,32),(Int)i,T("Remove","Удалить"));
   bx-=8+ew;button(list,editTitle,"proxyEdit:",rect(bx,y+12,ew,32),(Int)i,false);
   bx-=8+cw;Obj check=button(list,checkTitle,"proxyCheck:",rect(bx,y+12,cw,32),(Int)i,false,"checkmark.shield");
   send<void>(check,"setToolTip:",str(T("Open a tunnel to api.openai.com:443 through every address (no data is sent)","Открыть туннель к api.openai.com:443 через каждый адрес (данные не отправляются)")));
   if(send<bool>(proxyChecking,"containsObject:",get(x2,"id")))disable(check);}
  lines(rootView,T("Changes apply when a profile starts; running windows keep their connection until they are restarted.","Изменения применяются при запуске профиля; работающие окна сохраняют соединение до перезапуска."),rect(x,top+(listH<room?listH:room)+20,w,18),12,muted(),18);}
 footerNote(x,footY,w,"lock.shield",str(T("Passwords stay in a private file in AppDeck’s data folder; each bridge gets them through a pipe, never on the command line.","Пароли хранятся в закрытом файле в папке данных AppDeck; мост получает их через канал, а не в командной строке.")));
}
