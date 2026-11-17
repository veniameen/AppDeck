// Synthetic form interactions and renders; no state loading, managed launch or network request.
#define main appdeckApplicationMain
#include "../src/main.cpp"
#undef main
extern "C" int dprintf(int,const char*,...);
extern "C" int setenv(const char*,const char*,int);
namespace {
unsigned checks=0;
void checkConnection(bool ok,const char* message){++checks;if(!ok){dprintf(2,"FAIL: %s\n",message);exit(1);}}
bool containsConnection(Obj text,const char* part){return strstr(utf8(text),part)!=nullptr;}
Obj formChoice(Obj fields){return send(send(get(fields,"proxy"),"selectedItem"),"representedObject");}
void chooseConnection(Obj fields,Obj choice){
 claudeProxyChoices(get(fields,"proxy"),choice);
 send<void>(controller,"claudeProxyChanged:",get(fields,"proxy"));
}
bool renderConnection(Obj view,Obj path){
 send<void>(window,"setContentView:",view);send<void>(view,"layoutSubtreeIfNeeded");send<void>(window,"display");return renderView(view,path);
}
}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 char scratch[]="/private/tmp/appdeck-connection-ui-XXXXXX";if(!mkdtemp(scratch))return 3;
 if(setenv("APPDECK_DATA_ROOT",scratch,1))return 3;
 appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);if(!appKit)return 4;Pool pool;
 app=send(cls("NSApplication"),"sharedApplication");send<bool>(app,"setActivationPolicy:",(Int)2);
 fileManager=send(cls("NSFileManager"),"defaultManager");dataRoot=keep(str(scratch));home=keep(join(dataRoot,"fake-home"));previewMode=true;
 state=keep(dict());apps=keep(array());profiles=keep(array());proxies=keep(array());registerClasses();controller=make("AppDeckController");
 Obj dark=publicConstant(appKit,"NSAppearanceNameDarkAqua");if(dark)send<void>(app,"setAppearance:",send(cls("NSAppearance"),"appearanceNamed:",dark));
 window=send(send(cls("NSWindow"),"alloc"),"initWithContentRect:styleMask:backing:defer:",rect(-10000,-10000,520,476),(UInt)0,(UInt)2,false);send<void>(window,"setReleasedWhenClosed:",false);
 Obj synthetic=dict();put(synthetic,"id",str("10000000-0000-4000-8000-000000000001"));put(synthetic,"name",str("Office fixture proxy"));add(proxies,synthetic);put(state,"defaultProxy",get(synthetic,"id"));
 Obj fixtureApp=dict();put(fixtureApp,"id",str("30000000-0000-4000-8000-000000000001"));put(fixtureApp,"adapter",str("claude"));add(apps,fixtureApp);
 Obj existing=dict();put(existing,"appId",get(fixtureApp,"id"));put(existing,"id",str("20000000-0000-4000-8000-000000000001"));put(existing,"name",str("Existing fixture"));put(existing,"proxy",str("none"));put(existing,"claudeClean",boolean(true));put(existing,"proxyRequired",boolean(false));
 Obj snapshot=keep(send(existing,"copy"));Obj out=str(argv[1]);checkConnection(mkdirPrivate(out),"create only test render directory");
 for(bool russian:{false,true}){
  ruUI=russian;Obj fields=dict(),name=nullptr,mode=nullptr,form=claudeProfileForm(str("New fixture"),&name,&mode,fields);claudeFormFields=keep(fields);
  checkConnection(same(formChoice(fields),str("default")),"configured default stays selected");
  checkConnection(containsConnection(send(send(get(fields,"proxy"),"selectedItem"),"title"),"Office fixture proxy"),"default selector identifies actual proxy");
  checkConnection(containsConnection(send(get(fields,"connectionSummary"),"stringValue"),"Office fixture proxy"),"initial summary identifies inherited route");
  checkConnection(renderConnection(form,join(out,russian?"connection-proxy-ru.png":"connection-proxy-en.png")),"render actual default-proxy form");
  chooseConnection(fields,str("none"));
  checkConnection(send<Int>(get(fields,"required"),"state")==0&&send<Int>(get(fields,"dns"),"state")==0,"ordinary connection clears proxy-only switches");
  checkConnection(containsConnection(send(get(fields,"connectionSummary"),"stringValue"),"VPN"),"ordinary connection explains router and system VPN");
  checkConnection(!containsConnection(send(get(fields,"connectionSummary"),"stringValue"),"Office fixture proxy"),"changing selection updates summary immediately");
  checkConnection(renderConnection(form,join(out,russian?"connection-system-ru.png":"connection-system-en.png")),"render actual ordinary-connection form");
  chooseConnection(fields,get(synthetic,"id"));checkConnection(send<Int>(get(fields,"required"),"state")==1,"explicit proxy selection enables required route");
  Obj requiredText=keep(send(get(fields,"connectionSummary"),"stringValue"));send<void>(get(fields,"required"),"setState:",(Int)0);send<void>(controller,"claudeConnectionSummaryChanged:",get(fields,"required"));
  checkConnection(!same(requiredText,send(get(fields,"connectionSummary"),"stringValue")),"required-route toggle updates explanation");drop(requiredText);
  chooseConnection(fields,str("missing-fixture-proxy"));checkConnection(same(formChoice(fields),str("missing-fixture-proxy")),"unavailable saved selection is not replaced silently");
  checkConnection(!containsConnection(send(get(fields,"connectionSummary"),"stringValue"),"Office fixture proxy"),"unavailable selection never describes the default route");
  checkConnection(renderConnection(form,join(out,russian?"connection-missing-ru.png":"connection-missing-en.png")),"render unavailable route guidance");drop(claudeFormFields);claudeFormFields=nullptr;drop(form);
  Obj oldFields=dict();form=claudeProfileForm(get(existing,"name"),&name,&mode,oldFields,existing);
  checkConnection(same(formChoice(oldFields),str("none"))&&send<Int>(mode,"indexOfSelectedItem")==2&&!send<bool>(mode,"isEnabled"),"existing desktop-only profile retains route and immutable mode");
  checkConnection(send<bool>(existing,"isEqualToDictionary:",snapshot),"opening the form preserves every existing profile field");drop(form);
  Obj grid=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,1000,788));unsigned index=0;
  for(const char* error:{"network","denied","expired","quota","ratelimit","suspended","region","proxy","service","protocol","timeout","network"}){
   Obj profile=dict(),usage=dict();put(profile,"appId",get(fixtureApp,"id"));put(profile,"id",uuid());put(profile,"usage",usage);put(usage,"error",str(error));put(usage,"checked",real(nowUnix()));
   if(index==11){Obj windows=array(),w=dict();put(w,"used",real(42));put(w,"minutes",num(10080));put(w,"resets",real(nowUnix()+3600));add(windows,w);put(usage,"windows",windows);put(usage,"windowsAt",real(nowUnix()-1800));}
   Obj tile=card(grid,rect(12+(index%2)*494,12+(index/2)*128,482,116));label(tile,index==11?"Cached data + current error":error,rect(14,12,452,20),13,ink());
   Obj strip=usageStrip(tile,14,44,452,-1);usageApplyStrip(strip,profile);
   checkConnection(!send<bool>(get(strip,"message"),"isHidden"),"current error stays visible with or without cached windows");
   checkConnection(containsConnection(send(get(strip,"hit"),"toolTip"),utf8(usageMessage(profile))),"tooltip retains the full localized next action");++index;
  }
  send<void>(window,"setContentSize:",Extent{1000,788});checkConnection(renderConnection(grid,join(out,russian?"usage-errors-ru.png":"usage-errors-en.png")),"render production error strips in both languages");drop(grid);send<void>(window,"setContentSize:",Extent{520,476});
 }
 checkConnection(!exists(join(dataRoot,"state.plist"))&&!exists(join(dataRoot,"Profiles"))&&!exists(home),"form interactions never save state or prepare profile data");
 Obj error=nullptr;checkConnection(send<bool>(fileManager,"removeItemAtPath:error:",dataRoot,&error),"remove only empty synthetic data root");drop(snapshot);
 dprintf(1,"%u connection UI assertions passed; EN/RU renders saved; no managed app or network request.\n",checks);return 0;
}
