// Included inside main.cpp's private namespace, before buildUI(): the Proxy page, its editor, the
// default and per-profile choice menus and the result of a check. Data and bridges: proxy_store.hpp.
Obj proxyTrim(Obj s){return send(s,"stringByTrimmingCharactersInSet:",send(cls("NSCharacterSet"),"whitespaceAndNewlineCharacterSet"));}
// One line on the row: scheme, number of addresses and what the last check found.
Obj proxySummary(Obj x){
 Obj s=nullptr;if(proxyIsVless(x)){deck::VlessEndpoint endpoint{};Obj addresses=get(x,"lines");Obj uri=count(addresses)==1?at(addresses,0):nullptr;
  bool valid=uri&&deck::vlessParse(utf8(uri),send<UInt>(uri,"lengthOfBytesUsingEncoding:",(UInt)4),endpoint)==deck::VlessParseError::Ok;
  s=str(valid?(endpoint.security==deck::VlessSecurity::Reality?T("VLESS · REALITY · TCP","VLESS · REALITY · TCP"):T("VLESS · TLS · TCP","VLESS · TLS · TCP")):T("VLESS · invalid link","VLESS · некорректная ссылка"));
 }else{Int n=0;Obj addresses=get(x,"lines");for(UInt i=0;i<count(addresses);++i){deck::ProxyEndpoint e;if(deck::proxyParse(utf8(at(addresses,i)),e))++n;}
  s=cat(str(same(get(x,"scheme"),str("socks5"))?"SOCKS5":"HTTP"),formatInt(n==1?T(" · 1 address"," · 1 адрес"):T(" · %ld addresses"," · адресов: %ld"),n));}
 if(proxyChecking&&send<bool>(proxyChecking,"containsObject:",get(x,"id")))return cat(s,str(T(" · checking…"," · проверяю…")));
 Obj c=get(x,"check");if(!c)return cat(s,str(T(" · not checked yet"," · ещё не проверялся")));
 Int ok=integer(get(c,"ok")),total=integer(get(c,"total"));Obj result;
 if(ok>0){char b[160];snprintf(b,sizeof b,T(" · works: %ld of %ld, fastest %ld ms"," · работает: %ld из %ld, быстрее всего %ld мс"),ok,total,integer(get(c,"fastest")));result=str(b);}
 else result=str(proxyIsVless(x)?T(" · connection not confirmed"," · соединение не подтверждено"):truth(get(c,"auth"))?T(" · login or password rejected"," · логин или пароль отклонены"):T(" · no address answers"," · ни один адрес не отвечает"));
 return cat(cat(cat(s,result),str(" · ")),usageAgeText(send<double>(get(c,"at"),"doubleValue")));
}
Obj proxyEditorFields=nullptr;
void proxyKindUpdate(Obj fields){
 bool vless=send<Int>(get(fields,"kind"),"indexOfSelectedItem")==2,wasVless=truth(get(fields,"wasVless"));
 Obj user=get(fields,"user"),pass=get(fields,"pass");
 if(vless&&!wasVless){put(fields,"httpUser",send(user,"stringValue"));put(fields,"httpPass",send(pass,"stringValue"));send<void>(user,"setStringValue:",str(""));send<void>(pass,"setStringValue:",str(""));}
 if(!vless&&wasVless){send<void>(user,"setStringValue:",get(fields,"httpUser")?get(fields,"httpUser"):str(""));send<void>(pass,"setStringValue:",get(fields,"httpPass")?get(fields,"httpPass"):str(""));}
 put(fields,"wasVless",boolean(vless));send<void>(user,"setEnabled:",!vless);send<void>(pass,"setEnabled:",!vless);
 send<void>(user,"setPlaceholderString:",str(vless?T("From the VLESS link","Из ссылки VLESS"):T("for addresses without their own","для адресов без своего")));
 send<void>(pass,"setPlaceholderString:",str(vless?T("From the VLESS link","Из ссылки VLESS"):""));
 send<void>(get(fields,"addressLabel"),"setStringValue:",str(vless?T("VLESS link","Ссылка VLESS"):T("Addresses","Адреса")));
 send<void>(get(fields,"hint"),"setStringValue:",str(vless?T("Paste one vless:// link.\nTCP with TLS or REALITY.\nThe link already contains the credentials.","Вставьте одну ссылку vless://.\nTCP с TLS или REALITY.\nДанные подключения уже находятся в ссылке."):T("One address per line:\nhost:port or host:port:login:password.\nThe first address that answers is used.","По одному адресу в строке:\nhost:port или host:port:логин:пароль.\nИспользуется первый отвечающий адрес.")));
 send<void>(get(fields,"credentialHint"),"setStringValue:",str(vless?T("Login and Password are taken from the link.\nOnly this saved connection is changed.","Данные подключения берутся из ссылки.\nМеняется только это сохранённое подключение."):T("Optional common credentials for addresses\nthat do not include their own login and password.","Общие данные для адресов, в которых\nне указаны свои логин и пароль.")));
}
void proxyKindChangedAction(Obj,Sel,Obj){if(proxyEditorFields)proxyKindUpdate(proxyEditorFields);}
// Share links are visible only in this editor; summaries and errors never expose them.
Obj proxyEditorForm(Obj name,Obj text,Obj user,Obj pass,Int scheme,Obj fields){
 Obj box=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,440,350));
 label(box,T("Name","Название"),rect(0,4,110,18));Obj nameField=send(send(cls("NSTextField"),"alloc"),"initWithFrame:",rect(116,0,324,24));send<void>(nameField,"setStringValue:",name);send<void>(box,"addSubview:",nameField);put(fields,"name",nameField);drop(nameField);
 label(box,T("Type","Тип"),rect(0,38,110,18));const char* kinds[]={T("HTTP (CONNECT)","HTTP (CONNECT)"),T("SOCKS5","SOCKS5"),T("VLESS (TLS / REALITY)","VLESS (TLS / REALITY)")};Obj kind=popup(box,rect(114,32,326,28),kinds,3,scheme);put(fields,"kind",kind);send<void>(kind,"setTarget:",controller);send<void>(kind,"setAction:",sel("proxyKindChanged:"));
 put(fields,"addressLabel",lines(box,"",rect(0,72,108,38),12,muted(),18));
 Obj scroll=send(send(cls("NSScrollView"),"alloc"),"initWithFrame:",rect(116,70,324,96));send<void>(scroll,"setHasVerticalScroller:",true);send<void>(scroll,"setBorderType:",(UInt)2);
 Obj view=send(send(cls("NSTextView"),"alloc"),"initWithFrame:",rect(0,0,320,96));send<void>(view,"setRichText:",false);send<void>(view,"setFont:",codeFont(12,0));
 for(const char* off:{"setAutomaticQuoteSubstitutionEnabled:","setAutomaticDashSubstitutionEnabled:","setAutomaticTextReplacementEnabled:","setAutomaticSpellingCorrectionEnabled:","setContinuousSpellCheckingEnabled:"})send<void>(view,off,false);
 send<void>(view,"setString:",text);send<void>(view,"setAutoresizingMask:",(UInt)2);send<void>(scroll,"setDocumentView:",view);put(fields,"text",view);drop(view);send<void>(box,"addSubview:",scroll);drop(scroll);
 put(fields,"hint",lines(box,"",rect(116,170,324,58),11,faint(),17));
 label(box,T("Login","Логин"),rect(0,246,110,18));Obj userField=send(send(cls("NSTextField"),"alloc"),"initWithFrame:",rect(116,242,324,24));send<void>(userField,"setStringValue:",user);send<void>(userField,"setPlaceholderString:",str(T("HTTP / SOCKS5 only","Только HTTP / SOCKS5")));send<void>(box,"addSubview:",userField);put(fields,"user",userField);drop(userField);
 label(box,T("Password","Пароль"),rect(0,280,110,18));Obj passField=send(send(cls("NSSecureTextField"),"alloc"),"initWithFrame:",rect(116,276,324,24));send<void>(passField,"setStringValue:",pass);send<void>(box,"addSubview:",passField);put(fields,"pass",passField);drop(passField);
 put(fields,"credentialHint",lines(box,"",rect(116,310,324,38),11,faint(),17));proxyKindUpdate(fields);
 return box;
}
const char* proxyVlessParseProblem(deck::VlessParseError error){
 using Error=deck::VlessParseError;
 switch(error){
  case Error::TooLong:return T("The VLESS link or one of its fields is too long.","Ссылка VLESS или одно из её полей слишком длинное.");
  case Error::RealityKey:return T("REALITY requires a valid 32-byte public key (pbk) in base64url format.","Для REALITY нужен корректный открытый ключ pbk: 32 байта в формате base64url.");
  case Error::ShortId:return T("The REALITY short ID (sid) must contain 0 to 8 bytes written as pairs of hexadecimal digits.","Короткий ID REALITY (sid) должен содержать от 0 до 8 байт, записанных парами шестнадцатеричных цифр.");
  case Error::ServerName:return T("Use a valid DNS name for sni. REALITY requires an explicit sni.","Укажите корректное DNS-имя в sni. Для REALITY параметр sni обязателен.");
  case Error::DuplicateOption:return T("The VLESS link repeats a connection option. Keep each option only once.","В ссылке VLESS повторяется параметр подключения. Каждый параметр должен встречаться один раз.");
  case Error::Security:case Error::Transport:case Error::UnsupportedOption:case Error::Encryption:case Error::Flow:case Error::Fingerprint:case Error::IncompatibleOption:
   return T("Only VLESS TCP with TLS or REALITY is supported. Unsupported options and insecure certificate settings are rejected.","Поддерживается только VLESS TCP с TLS или REALITY. Неподдерживаемые параметры и отключение проверки сертификатов не допускаются.");
  default:return T("The VLESS link is malformed. Check its UUID, server address, port and percent-encoded values.","Некорректная ссылка VLESS. Проверьте UUID, адрес сервера, порт и значения с процентным кодированием.");
 }
}
Obj proxyEditorProblem(Obj name,Obj text,Obj user,Obj pass,Int scheme,Obj* resultLines){
 *resultLines=nullptr;if(!send<UInt>(name,"length")||send<UInt>(name,"length")>64)return str(T("Enter a name of 1 to 64 characters.","Введите название от 1 до 64 символов."));
 if(scheme<0||scheme>2)return str(T("Choose a supported connection type.","Выберите поддерживаемый тип подключения."));
 Obj addresses=array(),bad=array(),raw=send(text,"componentsSeparatedByCharactersInSet:",send(cls("NSCharacterSet"),"newlineCharacterSet"));
 for(UInt i=0;i<count(raw);++i){Obj line=proxyTrim(at(raw,i));if(!send<UInt>(line,"length"))continue;
  if(scheme==2)add(addresses,line);else{deck::ProxyEndpoint endpoint;if(deck::proxyParse(utf8(line),endpoint))add(addresses,line);else add(bad,formatInt("%ld",(Int)i+1));}}
 if(scheme==2){
  if(count(addresses)!=1)return str(T("Paste exactly one vless:// link.","Вставьте ровно одну ссылку vless://."));
  if(send<UInt>(user,"length")||send<UInt>(pass,"length"))return str(T("Leave Login and Password empty for VLESS; its link contains the credentials.","Для VLESS оставьте логин и пароль пустыми: данные подключения находятся в ссылке."));
  Obj uri=at(addresses,0);deck::VlessEndpoint endpoint;auto error=deck::vlessParse(utf8(uri),send<UInt>(uri,"lengthOfBytesUsingEncoding:",(UInt)4),endpoint);
  if(error!=deck::VlessParseError::Ok)return str(proxyVlessParseProblem(error));
 }else{
  if(count(bad))return cat(str(T("These lines are not host:port or host:port:login:password: ","Эти строки не в формате host:port или host:port:логин:пароль: ")),send(bad,"componentsJoinedByString:",str(", ")));
  if(!count(addresses))return str(T("Add at least one address.","Добавьте хотя бы один адрес."));
  if(count(addresses)>32)return str(T("At most 32 addresses.","Не больше 32 адресов."));
  if(send<UInt>(user,"lengthOfBytesUsingEncoding:",(UInt)4)>255||send<UInt>(pass,"lengthOfBytesUsingEncoding:",(UInt)4)>255)return str(T("The login and the password are limited to 255 bytes (a non-Latin letter takes two).","Логин и пароль — не длиннее 255 байт (буква кириллицы занимает два)."));
  if(send<UInt>(user,"length")&&!send<UInt>(pass,"length"))return str(T("Enter the password for the common login, or leave both empty.","Введите пароль для общего логина или оставьте оба поля пустыми."));
 }
 *resultLines=addresses;return nullptr;
}
// Keep live routes/check revisions intact until the private store has been saved successfully.
bool proxyCommitEdit(Obj existing,Obj name,Int scheme,Obj addresses,Obj user,Obj pass,bool (*persist)()=proxySave){
 Obj target=existing?existing:dict(),previous=existing?send(existing,"copy"):nullptr;
 if(!existing){put(target,"id",uuid());add(proxies,target);}
 put(target,"name",name);put(target,"scheme",str(scheme==2?"vless":scheme==1?"socks5":"http"));put(target,"lines",addresses);
 if(scheme!=2&&send<UInt>(user,"length")){put(target,"user",user);put(target,"pass",pass);}else{erase(target,"user");erase(target,"pass");}
 erase(target,"check");put(target,"rev",uuid());
 if(!persist()){if(existing)send<void>(target,"setDictionary:",previous);else send<void>(proxies,"removeObjectIdenticalTo:",target);drop(previous);return false;}
 drop(previous);proxyForget(target);note("Proxy saved; credentials stay in proxies.plist (0600).");return true;
}
// The editor accepts ordinary proxy endpoints or one VLESS share link.
bool proxyEdit(Obj x){
 if(previewMode)return false;frontForModal();
 Obj name=x?get(x,"name"):formatInt(T("Proxy %ld","Прокси %ld"),(Int)count(proxies)+1);
 Obj text=x?send(get(x,"lines"),"componentsJoinedByString:",str("\n")):str("");
 Obj user=x&&isClass(get(x,"user"),"NSString")?get(x,"user"):str(""),pass=x&&isClass(get(x,"pass"),"NSString")?get(x,"pass"):str("");
 Int scheme=proxyIsVless(x)?2:x&&same(get(x,"scheme"),str("socks5"))?1:0;
 for(;;){
  Obj a=make("NSAlert");send<void>(a,"setMessageText:",str(x?T("Edit proxy","Изменить прокси"):T("Add proxy","Добавить прокси")));
  send<void>(a,"setInformativeText:",str(T("Selected profiles send supported requests through a local proxy. VLESS uses the same profile settings; it does not enable a system VPN.","Выбранные профили отправляют поддерживаемые запросы через локальный прокси. VLESS использует те же настройки профиля и не включает системный VPN.")));
  Obj fields=dict(),box=proxyEditorForm(name,text,user,pass,scheme,fields),nameField=get(fields,"name"),kind=get(fields,"kind"),view=get(fields,"text"),userField=get(fields,"user"),passField=get(fields,"pass");
  send<void>(a,"setAccessoryView:",box);send(a,"addButtonWithTitle:",str(T("Save","Сохранить")));send(a,"addButtonWithTitle:",str(T("Cancel","Отмена")));
  send<void>(send(a,"window"),"setInitialFirstResponder:",nameField);
  proxyEditorFields=keep(fields);bool accepted=send<Int>(a,"runModal")==1000;drop(proxyEditorFields);proxyEditorFields=nullptr;
  name=proxyTrim(send(nameField,"stringValue"));text=autoRelease(send(send(view,"string"),"copy"));user=proxyTrim(send(userField,"stringValue"));pass=send(passField,"stringValue");scheme=send<Int>(kind,"indexOfSelectedItem");
  keep(name);keep(user);keep(pass);drop(box);drop(a);autoRelease(name);autoRelease(user);autoRelease(pass);
  if(!accepted)return false;
  Obj addresses=nullptr;Obj problem=proxyEditorProblem(name,text,user,pass,scheme,&addresses);
  if(problem){showError(str(T("The proxy was not saved","Прокси не сохранён")),problem);continue;}
  if(proxyCommitEdit(x,name,scheme,addresses,user,pass))return true;
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
 Obj request=dict();put(request,"id",get(x,"id"));put(request,"config",config);put(request,"vless",boolean(proxyIsVless(x)));put(request,"rev",get(x,"rev")?get(x,"rev"):str(""));send<void>(proxyChecking,"addObject:",get(x,"id"));
 send<void>(cls("NSThread"),"detachNewThreadSelector:toTarget:withObject:",sel("proxyCheckWorker:"),controller,request);buildUI();
}
// "ok <i> <ms>" / "auth <i>" / "fail <i> <reason>" per address → a small record on the proxy.
void proxyCheckFinished(Obj,Sel,Obj result){
 Obj x=proxyById(get(result,"id"));send<void>(proxyChecking,"removeObject:",get(result,"id"));
 if(!x||!same(get(result,"rev"),get(x,"rev")?get(x,"rev"):str(""))){buildUI();return;} // removed or edited meanwhile: the result is for other addresses
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
void appProxyMenuAction(Obj,Sel,Obj sender){
 Obj p=fromSender(sender);if(!p)return;Obj menu=make("NSMenu");proxyProfileMenu(menu,p,profileIndex(p));
 Obj sub=send(send(menu,"itemAtIndex:",(Int)0),"submenu");send<bool>(sub,"popUpMenuPositioningItem:atLocation:inView:",(Obj)nullptr,Point{0,36},sender);drop(menu);
}
void copyAppProxyAction(Obj,Sel,Obj sender){
 Obj address=proxyLocalAddress(fromSender(sender));if(!address)return;
 Obj pasteboard=send(cls("NSPasteboard"),"generalPasteboard");send<Int>(pasteboard,"clearContents");send<bool>(pasteboard,"setString:forType:",address,str("public.utf8-plain-text"));
}
// The Proxy page.
void buildProxyPage(double x,double w,double footY){
 double titleW=headerButtons(x+w,{{T("Add proxy","Добавить прокси"),"proxyAdd:","plus",true,nullptr}})-x-16;
 pageHeader(x,w,T("NETWORK","СЕТЬ"),str(T("Proxy","Прокси")),str(T("HTTP, SOCKS5 or VLESS connections for individual profiles.","HTTP, SOCKS5 или VLESS отдельно для каждого профиля.")),titleW);
 if(proxiesDamaged){Obj warn=label(rootView,T("proxies.plist could not be read: AppDeck keeps the file and does not save changes. Profiles that use a proxy from it are not started.","proxies.plist не прочитан: AppDeck сохраняет файл и не записывает изменения. Профили, использующие прокси из него, не запускаются."),rect(x,116,w,16),12,warnColor(),.23);send<void>(warn,"setLineBreakMode:",(Int)4);}
 caption(rootView,T("FOR ALL PROFILES","ДЛЯ ВСЕХ ПРОФИЛЕЙ"),rect(x,138,w,14));
 Obj box=card(rootView,rect(x,160,w,settingRowH),16);Obj d=proxyDefault();
 const char* current=d?utf8(get(d,"name")):T("No proxy","Без прокси");
 double bw=fitWidth(current,true);if(bw<160)bw=160;if(bw>260)bw=260;
 Obj pick=settingRow(box,0,w,"globe",T("Default proxy","Прокси по умолчанию"),str(T("Profiles set to “Default” use it; each profile can choose its own in its ••• menu.","Его используют профили с выбором «По умолчанию»; свой прокси профиль выбирает в меню •••.")),current,"proxyDefaultMenu:",-1,bw,"chevron.up.chevron.down");
 send<void>(pick,"setImagePosition:",(UInt)8); // NSImageTrailing: the chevron after the name
 caption(rootView,T("PROXIES","ПРОКСИ"),rect(x,160+settingRowH+24,w,14));double top=160+settingRowH+46;
 if(!count(proxies)){Obj c=card(rootView,rect(x,top,w,104),16);iconTile(c,rect(20,24,40,40),"network");
  label(c,T("No proxies yet","Прокси пока нет"),rect(76,28,w-96,20),15,ink(),.3);
  label(c,T("Add your provider's HTTP / SOCKS5 addresses or one vless:// link.","Добавьте HTTP / SOCKS5-адреса провайдера или одну ссылку vless://."),rect(76,52,w-96,18),13,muted(),0);}
 else{double listH=count(proxies)*settingRowH,room=footY-16-top-44;Obj doc=scrollDocument(rootView,rect(x,top,w,listH<room?listH:room),listH);Obj list=card(doc,rect(0,0,w,listH),16);
  const char* checkTitle=T("Check","Проверить");const char* editTitle=T("Edit…","Изменить…");double cw=fitWidth(checkTitle,true),ew=fitWidth(editTitle,false);
  for(UInt i=0;i<count(proxies);++i){Obj x2=at(proxies,i);double y=i*settingRowH;if(i)settingDivider(list,y,w);
   iconTile(list,rect(16,y+12,32,32),"network");double bx=w-16-32,textW=bx-8-ew-8-cw-16-60;
   labelObj(list,get(x2,"name"),rect(60,y+10,textW,18),13,ink(),.3);Obj s=labelObj(list,proxySummary(x2),rect(60,y+29,textW,16),12,muted(),0);send<void>(s,"setLineBreakMode:",(Int)4);
   iconButton(list,"trash","proxyRemove:",rect(bx,y+12,32,32),(Int)i,T("Remove","Удалить"));
   bx-=8+ew;button(list,editTitle,"proxyEdit:",rect(bx,y+12,ew,32),(Int)i,false);
   bx-=8+cw;Obj check=button(list,checkTitle,"proxyCheck:",rect(bx,y+12,cw,32),(Int)i,false,"checkmark.shield");
   send<void>(check,"setToolTip:",str(proxyIsVless(x2)?T("Verify a TLS handshake to api.openai.com:443 through VLESS; no account or model request.","Проверить TLS-соединение с api.openai.com:443 через VLESS; без запроса аккаунта или модели."):T("Open a tunnel to api.openai.com:443 through every address (no data is sent)","Открыть туннель к api.openai.com:443 через каждый адрес (данные не отправляются)")));
   if(send<bool>(proxyChecking,"containsObject:",get(x2,"id")))disable(check);}
  lines(rootView,T("Changes apply when a profile starts; running windows keep their connection until they are restarted.","Изменения применяются при запуске профиля; работающие окна сохраняют соединение до перезапуска."),rect(x,top+(listH<room?listH:room)+20,w,18),12,muted(),18);}
 footerNote(x,footY,w,"lock.shield",str(T("Passwords and VLESS links stay in a private file; each bridge receives its configuration through a pipe, never on the command line.","Пароли и ссылки VLESS хранятся в закрытом файле; мост получает настройки через канал, а не в командной строке.")));
}
