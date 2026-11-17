// Native editor interactions, transactional edits and EN/RU renders with synthetic data only.
#define main appdeckApplicationMain
#include "../src/main.cpp"
#undef main
extern "C" int dprintf(int,const char*,...);
extern "C" int setenv(const char*,const char*,int);
namespace {
unsigned vlessUiChecks=0,vlessPersistCalls=0;
void checkVlessUi(bool ok,const char* label){++vlessUiChecks;if(!ok){dprintf(2,"FAIL: %s\n",label);exit(1);}}
bool vlessPersistFailure(){++vlessPersistCalls;return false;}
bool vlessPersistSuccess(){++vlessPersistCalls;return true;}
bool containsVlessUi(Obj value,const char* text){return value&&strstr(utf8(value),text);}
const char* fixtureUuid="01234567-89ab-cdef-0123-456789abcdef";
const char* fixtureTls="vless://01234567-89ab-cdef-0123-456789abcdef@vpn.example.net:443?security=tls&type=tcp#Fixture";
const char* fixtureReality="vless://01234567-89ab-cdef-0123-456789abcdef@vpn.example.net:443?security=reality&sni=example.net&pbk=BAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8";
Obj vlessUiRecord(const char* name,const char* uri){Obj item=dict(),addresses=array();put(item,"id",uuid());put(item,"name",str(name));put(item,"scheme",str("vless"));add(addresses,str(uri));put(item,"lines",addresses);return item;}
void chooseVlessUi(Obj fields,Int index){send<void>(get(fields,"kind"),"selectItemAtIndex:",index);send<void>(controller,"proxyKindChanged:",get(fields,"kind"));}
bool renderVlessUi(Obj view,Obj path){
 send<void>(view,"setWantsLayer:",true);send<void>(send(view,"layer"),"setBackgroundColor:",send(hex(0x202024),"CGColor"));
 send<void>(window,"setContentView:",view);send<void>(view,"layoutSubtreeIfNeeded");send<void>(window,"display");
 send<void>(send(cls("NSRunLoop"),"currentRunLoop"),"runUntilDate:",send(cls("NSDate"),"dateWithTimeIntervalSinceNow:",.05));
 return renderView(view,path);
}
void checkCorruptVlessRoute(){
 // Save and corrupt only the disposable root. A missing selected route must not become direct.
 Obj previousProxies=proxies,previousBridges=proxyProbeBridges,previousTasks=proxyTasks,previousChecking=proxyChecking;
 bool previousDamaged=proxiesDamaged;
 Obj entry=vlessUiRecord("Corruption fixture",fixtureTls),stored=array();add(stored,entry);
 checkVlessUi(send<bool>(stored,"writeToFile:atomically:",proxyPath(),true)&&chmod(utf8(proxyPath()),0600)==0,"write valid VLESS entry only in disposable store");
 Obj disk=mutablePlist(proxyPath());checkVlessUi(count(disk)==1&&proxyValid(at(disk,0)),"saved fixture VLESS entry is initially valid");
 Obj profile=dict();put(profile,"id",uuid());put(profile,"proxy",get(entry,"id"));put(profile,"proxyRequired",boolean(false));
 Obj invalidLines=array();add(invalidLines,str("vless://invalid-credential@vpn.example.net:443?security=tls"));put(at(disk,0),"lines",invalidLines);
 checkVlessUi(send<bool>(disk,"writeToFile:atomically:",proxyPath(),true),"corrupt only the saved VLESS URI");
 Obj corrupted=keep(send(cls("NSData"),"dataWithContentsOfFile:",proxyPath()));proxyLoad();
 checkVlessUi(proxiesDamaged&&!count(proxies)&&!proxyById(get(profile,"proxy"))&&proxyUnresolved(profile),"reload drops invalid entry and retains unresolved damaged-store state");
 Obj environment=dict();put(environment,"FIXTURE_UNCHANGED",str("clean"));Obj before=keep(send(environment,"copy"));
 Obj previousInFlight=usageInFlight;double previousStarted=usageStarted;
 checkVlessUi(!usageProxy(profile,environment),"damaged selected VLESS route blocks probe even when proxyRequired is false");
 checkVlessUi(send<bool>(environment,"isEqualToDictionary:",before),"blocked probe preserves its clean environment without direct fallback");
 checkVlessUi(same(get(usageOf(profile),"error"),str("proxy")),"blocked probe records only categorical route failure");
 checkVlessUi(!count(proxyTasks)&&!count(proxyProbeBridges)&&usageInFlight==previousInFlight&&usageStarted==previousStarted,"blocked route starts neither helper nor vendor probe");
 checkVlessUi(proxyBlocked(profile),"same damaged selection blocks GUI launch without requiring the switch");
 checkVlessUi(send<bool>(corrupted,"isEqualToData:",send(cls("NSData"),"dataWithContentsOfFile:",proxyPath())),"blocked launch and probe leave corrupted input intact");
 Obj error=nullptr;checkVlessUi(send<bool>(fileManager,"removeItemAtPath:error:",proxyPath(),&error),"remove only fixture-owned proxy store");
 drop(corrupted);drop(before);drop(proxies);drop(proxyProbeBridges);drop(proxyTasks);drop(proxyChecking);
 proxies=previousProxies;proxyProbeBridges=previousBridges;proxyTasks=previousTasks;proxyChecking=previousChecking;proxiesDamaged=previousDamaged;
}
}
int main(int argc,char** argv){
 if(argc!=2)return 2;char scratch[]="/private/tmp/appdeck-vless-ui-XXXXXX";if(!mkdtemp(scratch))return 3;
 if(setenv("APPDECK_DATA_ROOT",scratch,1))return 3;
 appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);if(!appKit)return 4;Pool pool;
 app=send(cls("NSApplication"),"sharedApplication");send<bool>(app,"setActivationPolicy:",(Int)2);
 fileManager=send(cls("NSFileManager"),"defaultManager");dataRoot=keep(str(scratch));home=keep(join(dataRoot,"fake-home"));previewMode=true;
 state=keep(dict());apps=keep(array());profiles=keep(array());proxies=keep(array());events=keep(array());proxyProbeBridges=keep(dict());proxyChecking=keep(send(cls("NSMutableSet"),"set"));
 registerClasses();Obj dark=publicConstant(appKit,"NSAppearanceNameDarkAqua");if(dark)send<void>(app,"setAppearance:",send(cls("NSAppearance"),"appearanceNamed:",dark));
 window=send(send(cls("NSWindow"),"alloc"),"initWithContentRect:styleMask:backing:defer:",rect(-10000,-10000,480,390),(UInt)0,(UInt)2,false);send<void>(window,"setReleasedWhenClosed:",false);
 Obj out=str(argv[1]);checkVlessUi(mkdirPrivate(out),"create only synthetic render directory");
 for(bool russian:{false,true}){
  ruUI=russian;Obj fields=dict(),form=proxyEditorForm(str("Office VLESS"),str(fixtureTls),str(""),str(""),2,fields);proxyEditorFields=keep(fields);
  checkVlessUi(send<Int>(get(fields,"kind"),"numberOfItems")==3&&send<Int>(get(fields,"kind"),"indexOfSelectedItem")==2,"VLESS is the third protocol option");
  checkVlessUi(!send<bool>(get(fields,"user"),"isEnabled")&&!send<bool>(get(fields,"pass"),"isEnabled"),"VLESS disables separate credentials");
  checkVlessUi(containsVlessUi(send(get(fields,"hint"),"stringValue"),"vless://")&&!containsVlessUi(send(get(fields,"hint"),"stringValue"),"host:port"),"VLESS hint shows only its own input format");
  Obj holder=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,480,390));send<void>(form,"setFrameOrigin:",Point{20,20});send<void>(holder,"addSubview:",form);
  checkVlessUi(renderVlessUi(holder,join(out,russian?"vless-editor-ru.png":"vless-editor-en.png")),"render actual VLESS editor");
  chooseVlessUi(fields,0);checkVlessUi(send<bool>(get(fields,"user"),"isEnabled")&&containsVlessUi(send(get(fields,"hint"),"stringValue"),"host:port"),"HTTP restores endpoint hint and credentials");
  send<void>(get(fields,"user"),"setStringValue:",str("fixture-user"));send<void>(get(fields,"pass"),"setStringValue:",str("fixture-password"));
  chooseVlessUi(fields,2);checkVlessUi(!send<UInt>(send(get(fields,"user"),"stringValue"),"length")&&!send<UInt>(send(get(fields,"pass"),"stringValue"),"length"),"VLESS cannot inherit separate HTTP credentials");
  chooseVlessUi(fields,1);checkVlessUi(same(send(get(fields,"user"),"stringValue"),str("fixture-user"))&&same(send(get(fields,"pass"),"stringValue"),str("fixture-password")),"switching back preserves unsaved ordinary credentials");
  send<void>(get(fields,"text"),"setString:",str("proxy.example.net:8080"));checkVlessUi(renderVlessUi(holder,join(out,russian?"socks-editor-ru.png":"socks-editor-en.png")),"render unchanged ordinary protocol inputs");
  Obj addresses=nullptr,problem=proxyEditorProblem(str("Fixture"),str(fixtureTls),str(""),str(""),2,&addresses);
  checkVlessUi(!problem&&count(addresses)==1&&same(at(addresses,0),str(fixtureTls)),"editor retains one complete VLESS link");
  problem=proxyEditorProblem(str("Fixture"),cat(cat(str(fixtureTls),str("\n")),str(fixtureTls)),str(""),str(""),2,&addresses);checkVlessUi(problem&&!addresses&&!containsVlessUi(problem,fixtureUuid),"multiple links rejected without exposing UUID");
  problem=proxyEditorProblem(str("Fixture"),str(fixtureTls),str("unexpected"),str(""),2,&addresses);checkVlessUi(problem&&!addresses,"separate VLESS login rejected by validator");
  problem=proxyEditorProblem(str("Fixture"),str(fixtureTls),str(""),str("unexpected"),2,&addresses);checkVlessUi(problem&&!addresses,"separate VLESS password rejected by validator");
  for(const char* extra:{"&allowInsecure=1","&type=ws","&unknown=secret"}){
   Obj uri=cat(str("vless://01234567-89ab-cdef-0123-456789abcdef@vpn.example.net:443?security=tls"),str(extra));
   problem=proxyEditorProblem(str("Fixture"),uri,str(""),str(""),2,&addresses);
   checkVlessUi(problem&&!addresses&&!containsVlessUi(problem,fixtureUuid)&&!containsVlessUi(problem,"vpn.example.net")&&!containsVlessUi(problem,"secret"),"unsupported options yield only categorical errors");
  }
  const char nulUri[]="vless://01234567-89ab-cdef-0123-456789abcdef@vpn.example.net:443?security=tls\0ignored";
  Obj embedded=send(send(cls("NSString"),"alloc"),"initWithBytes:length:encoding:",nulUri,(UInt)(sizeof nulUri-1),(UInt)4);
  problem=proxyEditorProblem(str("Fixture"),embedded,str(""),str(""),2,&addresses);checkVlessUi(problem&&!addresses,"NSString embedded NUL rejected with length-aware parser");drop(embedded);
  problem=proxyEditorProblem(str("Fixture"),str("proxy.example.net:8080:own:password\nother.example.net:1080"),str("common"),str("secret"),0,&addresses);checkVlessUi(!problem&&count(addresses)==2,"ordinary endpoint validation stays supported");
  Obj tls=vlessUiRecord("Office VLESS",fixtureTls),reality=vlessUiRecord("Backup REALITY",fixtureReality);add(proxies,tls);add(proxies,reality);
  for(Obj item:{tls,reality}){Obj summary=proxySummary(item);checkVlessUi(containsVlessUi(summary,"VLESS")&&!containsVlessUi(summary,fixtureUuid)&&!containsVlessUi(summary,"vpn.example.net")&&!containsVlessUi(summary,"BAECAwQ"),"row summary excludes all link fields");}
  send<void>(window,"setContentSize:",Extent{1050,560});rootView=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,1050,560));buildProxyPage(20,1010,510);
  checkVlessUi(renderVlessUi(rootView,join(out,russian?"vless-proxy-page-ru.png":"vless-proxy-page-en.png")),"render actual Proxy page with synthetic VLESS records");drop(rootView);rootView=nullptr;send<void>(window,"setContentSize:",Extent{480,390});
  send<void>(proxies,"removeAllObjects");drop(proxyEditorFields);proxyEditorFields=nullptr;drop(form);drop(holder);
 }
 Obj original=dict(),oldAddresses=array(),oldCheck=dict();put(original,"id",uuid());put(original,"name",str("Original"));put(original,"scheme",str("http"));add(oldAddresses,str("proxy.example.net:8080"));put(original,"lines",oldAddresses);put(original,"user",str("old-user"));put(original,"pass",str("old-pass"));put(original,"check",oldCheck);put(original,"rev",str("old-revision"));add(proxies,original);
 Obj before=keep(send(original,"copy")),newAddresses=array();add(newAddresses,str(fixtureTls));Obj bridge=dict();put(proxyProbeBridges,utf8(get(original,"id")),bridge);
 checkVlessUi(!proxyCommitEdit(original,str("Updated"),2,newAddresses,str(""),str(""),vlessPersistFailure),"failed write rejects existing edit");
 checkVlessUi(send<bool>(original,"isEqualToDictionary:",before)&&count(proxies)==1&&get(proxyProbeBridges,utf8(get(original,"id")))==bridge,"failed edit restores record and preserves live bridge");
 checkVlessUi(!proxyCommitEdit(nullptr,str("New"),2,newAddresses,str(""),str(""),vlessPersistFailure)&&count(proxies)==1,"failed new write removes unsaved record");
 checkVlessUi(proxyCommitEdit(original,str("Updated"),2,newAddresses,str(""),str(""),vlessPersistSuccess),"successful persistence commits route");
 checkVlessUi(proxyIsVless(original)&&!get(original,"user")&&!get(original,"pass")&&!get(original,"check")&&!same(get(original,"rev"),get(before,"rev"))&&!get(proxyProbeBridges,utf8(get(original,"id"))),"committed VLESS clears old credentials/check and retires probe bridge");
 checkVlessUi(vlessPersistCalls==3,"each edit attempts persistence once");drop(before);
 checkVlessUi(!exists(join(dataRoot,"state.plist"))&&!exists(join(dataRoot,"proxies.plist"))&&!exists(join(dataRoot,"Profiles"))&&!exists(home),"fixture never writes real configuration or starts profile storage");
 checkCorruptVlessRoute();
 checkVlessUi(!exists(proxyPath())&&!proxiesDamaged&&count(proxies)==1&&proxyIsVless(original),"corruption regression restores scratch objects and leaves no store behind");
 Obj error=nullptr;checkVlessUi(send<bool>(fileManager,"removeItemAtPath:error:",dataRoot,&error),"remove only empty fixture root");
 dprintf(1,"%u VLESS UI assertions passed; synthetic EN/RU renders; no managed app or network request.\n",vlessUiChecks);return 0;
}
