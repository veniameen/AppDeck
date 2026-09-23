#include "mac.hpp"
#include "core.hpp"
#include "edge_policy.hpp"
#include "usage_policy.hpp"
using namespace mac;
namespace {
constexpr const char* VERSION="0.11.0";
void* appKit=nullptr;
Obj app=nullptr,window=nullptr,controller=nullptr,rootView=nullptr;
Obj state=nullptr,apps=nullptr,profiles=nullptr,projects=nullptr,selected=nullptr;
Obj dataRoot=nullptr,home=nullptr,fileManager=nullptr,workspace=nullptr;
Obj cardRefs=nullptr,footer=nullptr,usageFooter=nullptr,statusItem=nullptr,statusMenu=nullptr,profileMenu=nullptr;
Obj timer=nullptr,events=nullptr;
Class deckViewClass=nullptr,controllerClass=nullptr,deckButtonClass=nullptr,edgeButtonClass=nullptr;
Int page=0;
bool building=false,quitting=false,previewMode=false;
// Interface language, decided once at launch: Russian when macOS picks the bundle's ru localization
// (Russian ahead of English among the preferred languages, or chosen for AppDeck in System Settings),
// English otherwise. AppKit's own panels follow the same choice. Every visible string is T(en, ru).
bool ruUI=false;
inline const char* T(const char* en,const char* ru){return ruUI?ru:en;}
int lockFd=-1;
const unsigned colors[6]={0x89C5F7,0x8BD0A3,0xDEB775,0xF3A98F,0xEFA4C1,0x6CD1D1};
void buildUI();void buildMenus();void save();void refresh();
void showError(Obj title,Obj message);
Obj currentApp();
Obj edgeTargetScreen();void setEdgeVisible(bool);
void edgeRefresh();void invalidateEdge();void connectBaseAction(Obj,Sel,Obj);
void showWindowAction(Obj,Sel,Obj);void focus(Obj);bool isMaster(Obj);
void syncBaseProjectsAction(Obj,Sel,Obj);void toggleHistoryAction(Obj,Sel,Obj);void toggleUsageAction(Obj,Sel,Obj);void usageSanitize(Obj);void usagePump();Obj cleanEnvironment();bool askUsage(Obj);void usageForce(Obj);
bool groupSyncEnabled(Obj);Obj syncGroup(Obj,bool);void groupSyncPump(bool);void automationOwnerAction(Obj,Sel,Obj);
Obj color(double r,double g,double b,double a=1){return send(cls("NSColor"),"colorWithSRGBRed:green:blue:alpha:",r,g,b,a);}
// Design tokens ("Graphite glass"): warm bone ink on dark glass. Colour appears only for state (live,
// warn, danger) and for the profile's identity badge; controls are neutral glass.
Obj hex(unsigned v,double a=1){return color(((v>>16)&255)/255.0,((v>>8)&255)/255.0,(v&255)/255.0,a);}
Obj ink(){return hex(0xF5F3EF);}Obj inkSoft(){return hex(0xE4E2DE);}Obj muted(){return hex(0xB9B7B3);}Obj faint(){return hex(0xA09E9A);}
Obj dim(){return hex(0x6E6C68);}Obj onInk(){return hex(0x17171A);}
Obj liveColor(){return hex(0x5EDB81);}Obj warnColor(){return hex(0xF9B64F);}Obj dangerColor(){return hex(0xFF7A6B);}
Obj fill(double a){return color(1,1,1,a);}Obj surface(){return fill(.045);}Obj hairline(){return fill(.08);}
Obj accentFor(Obj p){Int i=integer(get(p,"color"))%6;if(i<0)i=0;return hex(colors[i]);}
Obj canonical(Obj p){return send(send(p,"stringByStandardizingPath"),"stringByResolvingSymlinksInPath");}
Obj url(Obj p){return send(cls("NSURL"),"fileURLWithPath:",p);}
bool exists(Obj path){return send<bool>(fileManager,"fileExistsAtPath:",path);}
Obj attrs(Obj path){Obj e=nullptr;return send(fileManager,"attributesOfItemAtPath:error:",path,&e);}
bool symlink(Obj path){return same(get(attrs(path),"NSFileType"),str("NSFileTypeSymbolicLink"));}
bool directory(Obj path){return same(get(attrs(path),"NSFileType"),str("NSFileTypeDirectory"));}
bool mkdirPrivate(Obj path){
 if(symlink(path))return false;
 if(exists(path)){if(!directory(path))return false;return chmod(utf8(path),0700)==0;}
 Obj err=nullptr;Obj a=dict();put(a,"NSFilePosixPermissions",num(0700));
 return send<bool>(fileManager,"createDirectoryAtPath:withIntermediateDirectories:attributes:error:",path,true,a,&err);
}
bool writeText(Obj path,Obj text){
 if(symlink(path))return false;Obj e=nullptr;
 bool ok=send<bool>(text,"writeToFile:atomically:encoding:error:",path,true,(UInt)4,&e);
 if(ok)chmod(utf8(path),0600);return ok;
}
Obj readText(Obj path){
 if(!exists(path)||symlink(path))return nullptr;
 Obj a=attrs(path);if(integer(get(a,"NSFileSize"))>2*1024*1024)return nullptr;
 Obj e=nullptr;return send(cls("NSString"),"stringWithContentsOfFile:encoding:error:",path,(UInt)4,&e);
}
Obj uuid(){return send(send(cls("NSUUID"),"UUID"),"UUIDString");}
Obj shortPath(Obj p){return replace(p,utf8(home),"~");}
Obj profileRoot(Obj p){if(!deck::uuid(utf8(get(p,"id"))))return nullptr;return join(join(dataRoot,"Profiles"),get(p,"id"));}
Obj sharedRoot(Obj a){if(!a||!deck::uuid(utf8(get(a,"id"))))return nullptr;return join(join(dataRoot,"Shared"),get(a,"id"));}
Obj appFor(Obj p){for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);if(same(get(a,"id"),get(p,"appId")))return a;}return nullptr;}
Obj currentApp(){for(UInt i=0;i<count(apps);++i)if(same(get(at(apps,i),"id"),selected))return at(apps,i);return nullptr;}
Obj visibleProfiles(){Obj r=array();for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);if(!selected||same(get(p,"appId"),selected))add(r,p);}return r;}
// The user's order of all profiles, one list for the side panel, ⌃⌥1…8 and the menu bar menu; the main
// window shows the same order per app. It is the order of the profiles array in state.plist.
Obj dockProfiles(){Obj r=array();for(UInt i=0;i<count(profiles);++i)if(appFor(at(profiles,i)))add(r,at(profiles,i));return r;}
Int profileIndex(Obj p){return (Int)send<UInt>(profiles,"indexOfObjectIdenticalTo:",p);}
Obj fromSender(Obj sender){Int n=send<Int>(sender,"tag");return n>=0&&(UInt)n<count(profiles)?at(profiles,(UInt)n):nullptr;}
Obj running(Obj p){
 int pid=(int)integer(get(p,"pid"));if(pid<=0)return nullptr;
 Obj r=send(cls("NSRunningApplication"),"runningApplicationWithProcessIdentifier:",pid);
 if(!r||send<bool>(r,"isTerminated"))return nullptr;
 Obj a=appFor(p);Obj path=send(send(r,"bundleURL"),"path");
 if(!a||!path||!same(canonical(path),canonical(get(a,"path"))))return nullptr;
 Obj launched=send(r,"launchDate");double expected=send<double>(get(p,"started"),"doubleValue");
 if(!launched||expected<=0)return nullptr;
 double dt=send<double>(launched,"timeIntervalSince1970")-expected;
 return dt>-0.1&&dt<0.1?r:nullptr;
}
void note(const char* text){
 Obj t=send(send(cls("NSDate"),"date"),"description");add(events,cat(cat(t,str("  ")),str(text)));
 while(count(events)>100)send<void>(events,"removeObjectAtIndex:",(UInt)0);
}
void frontForModal(){if(app&&!previewMode)send<void>(app,"activateIgnoringOtherApps:",true);}
void showError(Obj title,Obj message){
 if(previewMode){note(utf8(title));return;}frontForModal();
 Obj a=make("NSAlert");send<void>(a,"setMessageText:",title);send<void>(a,"setInformativeText:",message?message:str(T("Unknown error.","Неизвестная ошибка.")));
 send<void>(a,"setAlertStyle:",(Int)1);send(a,"addButtonWithTitle:",str(T("OK","Понятно")));send<Int>(a,"runModal");drop(a);
}
bool confirm(const char* title,const char* message,const char* ok){
 if(previewMode)return false;frontForModal();
 Obj a=make("NSAlert");send<void>(a,"setMessageText:",str(title));send<void>(a,"setInformativeText:",str(message));
 send(a,"addButtonWithTitle:",str(ok));send(a,"addButtonWithTitle:",str(T("Cancel","Отмена")));bool result=send<Int>(a,"runModal")==1000;drop(a);return result;
}
Obj prompt(const char* title,const char* info,Obj initial){
 if(previewMode)return nullptr;frontForModal();
 Obj a=make("NSAlert");send<void>(a,"setMessageText:",str(title));send<void>(a,"setInformativeText:",str(info));
 Obj field=send(send(cls("NSTextField"),"alloc"),"initWithFrame:",rect(0,0,390,26));send<void>(field,"setStringValue:",initial?initial:str(""));
 send<void>(a,"setAccessoryView:",field);send(a,"addButtonWithTitle:",str(T("Save","Сохранить")));send(a,"addButtonWithTitle:",str(T("Cancel","Отмена")));
 send<void>(send(a,"window"),"setInitialFirstResponder:",field);Obj result=nullptr;
 if(send<Int>(a,"runModal")==1000){Obj trimmed=send(send(field,"stringValue"),"stringByTrimmingCharactersInSet:",send(cls("NSCharacterSet"),"whitespaceAndNewlineCharacterSet"));if(send<UInt>(trimmed,"length")>0&&send<UInt>(trimmed,"length")<=64)result=keep(trimmed);else showError(str(T("Name not saved","Название не сохранено")),str(T("Enter 1 to 64 characters.","Введите от 1 до 64 символов.")));}
 drop(field);drop(a);return autoRelease(result);
}
void save(){
 if(!state||previewMode)return;put(state,"schema",num(2));if(selected)put(state,"selected",selected);else erase(state,"selected");
 Obj p=join(dataRoot,"state.plist");if(symlink(p)){showError(str(T("Data protection","Защита данных")),str(T("state.plist must not be a symbolic link.","state.plist не должен быть символической ссылкой.")));return;}
 if(!send<bool>(state,"writeToFile:atomically:",p,true))showError(str(T("Could not save","Не удалось сохранить")),str(T("Check the permissions of the AppDeck folder in Library/Application Support.","Проверьте права на папку AppDeck в Library/Application Support.")));
 else chmod(utf8(p),0600);
}
Obj mutablePlist(Obj path){
 Obj d=send(cls("NSData"),"dataWithContentsOfFile:",path);if(!d)return nullptr;
 Obj err=nullptr;Int format=0;
 return send(cls("NSPropertyListSerialization"),"propertyListWithData:options:format:error:",d,(UInt)1,&format,&err);
}
bool isClass(Obj o,const char* name){return o&&send<bool>(o,"isKindOfClass:",cls(name));}
void load(){
 Obj p=join(dataRoot,"state.plist");Obj loaded=exists(p)&&!symlink(p)?mutablePlist(p):nullptr;
 if(loaded&&(!isClass(loaded,"NSDictionary")||(integer(get(loaded,"schema"))!=1&&integer(get(loaded,"schema"))!=2)))loaded=nullptr;
 if(exists(p)&&!loaded){showError(str(T("Settings file not read","Файл настроек не прочитан")),str(T("AppDeck will not overwrite damaged data. Check state.plist in the AppDeck folder.","AppDeck не будет перезаписывать повреждённые данные. Проверьте state.plist в папке AppDeck.")));exit(2);}
 if(loaded&&integer(get(loaded,"schema"))==1){
  Obj backup=join(dataRoot,"state-v1.before-0.2.plist");
  if(symlink(backup)){showError(str(T("Unsafe backup path","Небезопасный путь резервной копии")),str(T("state-v1.before-0.2.plist must not be a link.","Файл state-v1.before-0.2.plist не должен быть ссылкой.")));exit(2);}
  if(!exists(backup)&&!previewMode){Obj bytes=send(cls("NSData"),"dataWithContentsOfFile:",p);if(!bytes||!send<bool>(bytes,"writeToFile:atomically:",backup,true)){showError(str(T("Could not back up the original settings","Не удалось сохранить исходные настройки")),str(T("Migration stopped. state.plist is unchanged.","Миграция остановлена. state.plist не изменён.")));exit(2);}chmod(utf8(backup),0600);}
 }
 state=keep(loaded?loaded:dict());
 for(const char* key:{"apps","profiles","projects"}){Obj x=get(state,key);if(!x)put(state,key,array());else if(!isClass(x,"NSArray")){showError(str(T("Invalid settings","Некорректные настройки")),str(T("Expected a list of apps, profiles and projects.","Ожидался список приложений, профилей и проектов.")));exit(2);}}
 apps=get(state,"apps");profiles=get(state,"profiles");projects=get(state,"projects");
 for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);if(!isClass(a,"NSDictionary")||!isClass(get(a,"id"),"NSString")||!deck::uuid(utf8(get(a,"id")))||!isClass(get(a,"path"),"NSString")||!isClass(get(a,"name"),"NSString")||!isClass(get(a,"adapter"),"NSString")){showError(str(T("Invalid app","Некорректное приложение")),str(T("Check state.plist. Nothing was changed.","Проверьте файл state.plist. Ничего не изменено.")));exit(2);}}
 for(UInt i=0;i<count(profiles);++i){Obj p2=at(profiles,i);if(!isClass(p2,"NSDictionary")||!isClass(get(p2,"id"),"NSString")||!deck::uuid(utf8(get(p2,"id")))||!isClass(get(p2,"name"),"NSString")||!appFor(p2)){showError(str(T("Invalid profile","Некорректный профиль")),str(T("Check state.plist. Nothing was changed.","Проверьте файл state.plist. Ничего не изменено.")));exit(2);}}
 for(UInt i=0;i<count(profiles);++i){Obj p3=at(profiles,i);
  for(const char* key:{"pid","started","color","share","launchCheck","uiRunning","master"})if(get(p3,key)&&!isClass(get(p3,key),"NSNumber")){showError(str(T("Invalid profile parameters","Некорректные параметры профиля")),str(T("Expected numeric parameters in state.plist.","Ожидались числовые параметры в state.plist.")));exit(2);}
  if(!get(p3,"color"))put(p3,"color",num(0));usageSanitize(p3);
 }
 for(UInt i=0;i<count(projects);++i){Obj p4=at(projects,i);if(!isClass(p4,"NSDictionary")||!isClass(get(p4,"path"),"NSString")||!isClass(get(p4,"name"),"NSString")){showError(str(T("Invalid project folder","Некорректная папка проекта")),str(T("Check state.plist.","Проверьте state.plist.")));exit(2);}}
 // Up to 0.8 the dock grouped profiles by app. Adopt that order once, so nothing moves on upgrade;
 // from then on the array is the user's order.
 if(!get(state,"profileOrder")){Obj ordered=array();for(UInt i=0;i<count(apps);++i)for(UInt j=0;j<count(profiles);++j)if(same(get(at(profiles,j),"appId"),get(at(apps,i),"id")))add(ordered,at(profiles,j));
  if(count(ordered)==count(profiles))send<void>(profiles,"setArray:",ordered);put(state,"profileOrder",num(1));}
 Obj s=get(state,"selected");selected=isClass(s,"NSString")?keep(s):nullptr;
 if(!currentApp()&&count(apps)){drop(selected);selected=keep(get(at(apps,0),"id"));}
 for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);
  if(get(a,"baseSource")&&(!isClass(get(a,"baseSource"),"NSString")||!deck::absoluteLocalPath(utf8(get(a,"baseSource")))||deck::inside(utf8(dataRoot),utf8(get(a,"baseSource"))))){showError(str(T("Invalid settings source","Некорректный источник настроек")),str(T("baseSource must point to an absolute path outside AppDeck's data.","baseSource должен указывать на внешний абсолютный путь, не на данные AppDeck.")));exit(2);}
  if(get(a,"usageLimits")&&!isClass(get(a,"usageLimits"),"NSNumber")){showError(str(T("Invalid limits setting","Некорректная настройка лимитов")),str(T("usageLimits must be a boolean.","usageLimits должен быть логическим значением.")));exit(2);}
  if(get(a,"sharedHistory")&&!isClass(get(a,"sharedHistory"),"NSNumber")){showError(str(T("Invalid history setting","Некорректная настройка истории")),str(T("sharedHistory must be a boolean.","sharedHistory должен быть логическим значением.")));exit(2);}
  if(get(a,"seedWorkspace")&&!isClass(get(a,"seedWorkspace"),"NSNumber")){showError(str(T("Invalid import setting","Некорректная настройка импорта")),str(T("seedWorkspace must be a boolean.","seedWorkspace должен быть логическим значением.")));exit(2);}
 }
 events=keep(array());cardRefs=keep(dict());
}
// Foundation's NSFileManager performs all copying, not shell commands.
// Only these explicit settings files are ever copied; never an entire CODEX_HOME.
bool copySettingsFile(Obj source,Obj dest){
 if(!exists(source))return true;if(symlink(source)||symlink(dest))return false;
 Obj data=send(cls("NSData"),"dataWithContentsOfFile:",source);if(!data||send<UInt>(data,"length")>2*1024*1024)return false;
 if(exists(dest)){
  Obj previous=send(cls("NSData"),"dataWithContentsOfFile:",dest);
  if(same(previous,data))return true;
  Obj backup=cat(dest,str(".previous"));if(symlink(backup))return false;
  if(previous&&!send<bool>(previous,"writeToFile:atomically:",backup,true))return false;
  if(previous)chmod(utf8(backup),0600);
 }
 bool ok=send<bool>(data,"writeToFile:atomically:",dest,true);if(ok)chmod(utf8(dest),0600);return ok;
}
bool sharedLink(Obj source,Obj dest){
 if(symlink(dest)){Obj e=nullptr;Obj link=send(fileManager,"destinationOfSymbolicLinkAtPath:error:",dest,&e);return same(link,source);}
 if(exists(dest))return false;Obj e=nullptr;return send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",dest,source,&e);
}
bool ensureShared(Obj a){
 Obj s=sharedRoot(a);if(!s||!mkdirPrivate(s))return false;
 if(deck::adapter(utf8(get(a,"adapter")))!=deck::Adapter::Codex)return true;
 if(!mkdirPrivate(join(s,"skills"))||!mkdirPrivate(join(s,"rules")))return false;
 if(!exists(join(s,"config.toml"))&&!writeText(join(s,"config.toml"),str(
   "# AppDeck shared Codex settings.\n# Copied to each stopped profile before its next launch.\n# Do not put login tokens here. Authentication is profile-specific.\n# Existing workspace restrictions are preserved on import.\n# Example (uncomment and choose a model available to your account):\n# model_reasoning_effort = \"high\"\n")))return false;
 if(!exists(join(s,"AGENTS.md"))&&!writeText(join(s,"AGENTS.md"),str("# Shared agent instructions\n\nAdd instructions used by all Codex profiles in this app group.\nProject AGENTS.md files remain in their repositories.\n")))return false;
 return true;
}
#include "process_args.hpp"
#include "window_control.hpp"
#include "workspace_base.hpp"
#include "usage_limits.hpp"
#include "project_sync.hpp"
#include "automation_sync.hpp"
#include "project_server_sync.hpp"
#include "group_sync.hpp"
Obj prepare(Obj p){
 Obj a=appFor(p),base=profileRoot(p);if(!a||!base)return str(T("App not found or invalid profile ID.","Не найдено приложение или некорректный ID профиля."));
 if(!mkdirPrivate(base)||!mkdirPrivate(join(base,"user-data")))return str(T("Could not create the private profile folders (0700). Symbolic links are not allowed.","Не удалось создать закрытые папки профиля (0700). Символические ссылки не допускаются."));
 if(deck::adapter(utf8(get(a,"adapter")))!=deck::Adapter::Codex)return nullptr;
 Obj ch=join(base,"codex");if(!mkdirPrivate(ch)||!ensureShared(a))return str(T("Could not prepare the Codex folders.","Не удалось подготовить папки Codex."));
 bool shared=truth(get(p,"share"));Obj cfg=nullptr;
 if(shared&&baseSource(a)&&!directory(baseSource(a)))return str(T("The source workspace is unavailable. Launch cancelled without resetting settings.","Исходная рабочая среда недоступна. Запуск отменён без сброса настроек."));
 if(shared){Obj cfgPath=join(settingsSource(a),"config.toml");
  cfg=exists(cfgPath)?readText(canonical(cfgPath)):str("");if(!cfg)return str(T("The source config.toml is unavailable or larger than 2 MB.","config.toml источника недоступен или больше 2 МБ."));}
 else if(exists(join(ch,"config.toml"))){cfg=readText(join(ch,"config.toml"));if(!cfg)return str(T("The local config.toml is unavailable or too large.","Локальный config.toml недоступен или слишком велик."));}
 else cfg=str("");
 Size cap=strlen(utf8(cfg))+512;char* text=(char*)calloc(cap,1);if(!text)return str(T("Out of memory.","Недостаточно памяти."));
 auto result=deck::codexConfig(utf8(cfg),text,cap);
 if(result!=deck::ConfigResult::Ok){free(text);return str(T("config.toml contains an ambiguous sign-in setting or a multi-line string. Automatic editing cancelled. Use a simple shared config.toml; move complex configuration into the project's .codex/config.toml.","config.toml содержит неоднозначную запись настройки авторизации или многострочную строку. Автоматическая правка отменена. Используйте простой общий config.toml; перенесите сложную конфигурацию в .codex/config.toml проекта."));}
 Obj dest=join(ch,"config.toml");Obj old=readText(dest);bool ok=true;
 if(old&&!same(old,str(text)))ok=writeText(cat(dest,str(".previous")),old);
 if(ok)ok=writeText(dest,str(text));free(text);if(!ok)return str(T("Could not write config.toml safely; launch cancelled.","Не удалось безопасно записать config.toml; запуск отменён."));
 if(shared){
  Obj s=settingsSource(a);if(!copySettingsFile(canonical(join(s,"AGENTS.md")),join(ch,"AGENTS.md")))return str(T("Could not sync AGENTS.md.","Не удалось синхронизировать AGENTS.md."));
  for(const char* name:{"skills","rules"})if(!linkSettingDirectory(join(s,name),join(ch,name),a,name))return str(T("A skills or rules folder already exists in the profile and is not linked to the shared one. Move it manually; AppDeck never deletes or replaces such folders.","Папка skills или rules уже существует в профиле и не связана с общей. Перенесите её вручную; AppDeck не удаляет и не заменяет такие папки."));
 }
 syncHistoryLinks(p);
 return groupSyncEnabled(a)?nullptr:seedWorkspace(p);
}
// Neither a profile nor a limits probe may inherit a terminal/API identity or another profile's homes.
Obj cleanEnvironment(){
 Obj env=autoRelease(send(send(send(cls("NSProcessInfo"),"processInfo"),"environment"),"mutableCopy"));
 for(const char* k:{"OPENAI_API_KEY","CODEX_API_KEY","CODEX_ACCESS_TOKEN","OPENAI_ACCESS_TOKEN","CODEX_CHATGPT_ACCESS_TOKEN","CODEX_HOME","CODEX_SQLITE_HOME","CODEX_ELECTRON_USER_DATA_PATH","ELECTRON_RUN_AS_NODE","NODE_OPTIONS","CODEX_AUTH_JSON","CHATGPT_ACCESS_TOKEN","OPENAI_ORG_ID","OPENAI_ORGANIZATION","OPENAI_PROJECT_ID","CLAUDE_USER_DATA_DIR","ANTHROPIC_API_KEY","ANTHROPIC_AUTH_TOKEN","CLAUDE_CODE_OAUTH_TOKEN"})erase(env,k);
 return env;
}
bool launch(Obj p,bool activate=true){
 if(!p)return false;if(Obj r=running(p)){syncGroup(appFor(p),false);if(activate)focus(r);return true;}
 // A copy that AppDeck lost track of still owns its user-data folder: adopt it, never start a twin.
 if(Obj r=ownedProcess(p)){if(adopt(p,r)){save();refresh();if(activate)focus(r);return true;}}
 Obj a=appFor(p),path=get(a,"path");
 if(!a||!exists(join(path,"Contents/Info.plist"))){showError(str(T("App not found","Приложение не найдено")),str(T("The path has changed. Choose the new .app with “App location…”.","Путь изменился. Выберите новый .app через кнопку «Путь приложения».")));return false;}
 if(isMaster(p)){if(Obj failure=syncGroup(a,true)){showError(str(T("Sync before launch","Синхронизация перед запуском")),failure);return false;}
  if(truth(get(a,"syncUnsafe"))){showError(str(T("Two automation owners","Два исполнителя автоматизации")),str(T("Close this group's instances: AppDeck will pause the extra copies of the schedule before launch.","Закройте экземпляры этой группы: AppDeck приостановит дополнительные копии расписания перед запуском.")));return false;}return attachMaster(p,activate);}
 auto adapter=deck::adapter(utf8(get(a,"adapter")));
 if(adapter==deck::Adapter::Unsupported){showError(str(T("No adapter","Нет адаптера")),str(T("Isolating arbitrary native apps is not supported.","Изоляция произвольных нативных приложений не поддерживается.")));return false;}
 if(adapter==deck::Adapter::Codex&&truth(get(p,"share")))ensureHistoryChoice(a);
 Obj failure=prepare(p);if(failure){showError(str(T("Preparing the profile","Подготовка профиля")),failure);return false;}
 if(truth(get(p,"share"))){failure=syncGroup(a,true);if(failure){showError(str(T("Sync before launch","Синхронизация перед запуском")),failure);return false;}
  if(truth(get(a,"syncUnsafe"))){showError(str(T("Two automation owners","Два исполнителя автоматизации")),str(T("An active automation was found in several profiles. Close those instances: AppDeck keeps only the assigned owner active.","Активная автоматизация найдена в нескольких профилях. Закройте соответствующие экземпляры: AppDeck оставит активным только назначенного исполнителя.")));return false;}}
 Obj env=cleanEnvironment();
 Obj args=array(),base=profileRoot(p),ud=join(base,"user-data");
 add(args,cat(str("--user-data-dir="),ud));
 if(adapter==deck::Adapter::Codex){put(env,"CODEX_HOME",join(base,"codex"));put(env,"CODEX_ELECTRON_USER_DATA_PATH",ud);
  // Codex's own switch for the thread/project databases; everything else stays in the copy's CODEX_HOME.
  if(sharesHistory(p)){put(env,"CODEX_SQLITE_HOME",canonical(baseSource(a)));put(p,"historyMode",str("shared"));}else erase(p,"historyMode");}
 if(adapter==deck::Adapter::Claude)put(env,"CLAUDE_USER_DATA_DIR",ud);
 if(adapter==deck::Adapter::VSCode){add(args,str("--new-window"));add(args,cat(str("--extensions-dir="),join(base,"extensions")));}
 Obj config=dict();Obj ek=publicConstant(appKit,"NSWorkspaceLaunchConfigurationEnvironment");Obj ak=publicConstant(appKit,"NSWorkspaceLaunchConfigurationArguments");
 if(!ek||!ak){showError(str(T("macOS API unavailable","API macOS недоступен")),str(T("System launch constants not found. Please send diagnostics for this macOS version.","Не найдены системные константы запуска. Отправьте диагностику этой версии macOS.")));return false;}
 send<void>(config,"setObject:forKey:",env,ek);send<void>(config,"setObject:forKey:",args,ak);
 Obj alreadyRunning=array();
 Obj allRunning=send(workspace,"runningApplications");
 for(UInt i=0;i<count(allRunning);++i){Obj r0=at(allRunning,i);Obj p0=send(send(r0,"bundleURL"),"path");
  if(p0&&same(canonical(p0),canonical(path)))add(alreadyRunning,num(send<int>(r0,"processIdentifier")));
 }
 Obj err=nullptr;
 UInt options=(1UL<<19)|(activate?0:(1UL<<9)); // NSWorkspaceLaunchNewInstance / WithoutActivation
 Obj r=send(workspace,"launchApplicationAtURL:options:configuration:error:",url(path),options,config,&err);
 if(!r){put(p,"lastError",err?send(err,"localizedDescription"):str(T("LaunchServices returned no process.","LaunchServices не вернул процесс.")));save();showError(str(T("Could not launch","Не удалось запустить")),get(p,"lastError"));return false;}
 int pid=send<int>(r,"processIdentifier");
 for(UInt i=0;i<count(alreadyRunning);++i)if(integer(at(alreadyRunning,i))==pid){
  put(p,"lastError",str(T("The app returned an already running process instead of a new instance. Isolation not confirmed.","Вместо нового экземпляра приложение вернуло уже работавший процесс. Изоляция не подтверждена.")));save();
  showError(str(T("No separate instance was created","Отдельный экземпляр не создан")),str(T("An already running window opened, possibly outside AppDeck. Do not sign out or change settings in it. This app version did not accept an isolated launch.","Открыто уже работавшее окно, возможно вне AppDeck. Не выходите из аккаунта и не меняйте настройки в нём. Эта версия приложения не приняла изолированный запуск.")));return false;
 }
 for(UInt i=0;i<count(profiles);++i){Obj other=at(profiles,i);if(other!=p&&running(other)&&integer(get(other,"pid"))==pid){
  showError(str(T("The app merged the instances","Приложение объединило экземпляры")),str(T("The adapter could not create a separate process. Do not change the account in this window: it belongs to another profile. This app version is not compatible.","Адаптер не смог создать отдельный процесс. Не меняйте аккаунт в этом окне: оно принадлежит другому профилю. Эта версия приложения несовместима.")));return false;}}
 Obj date=send(r,"launchDate");if(!date){showError(str(T("Could not confirm the process","Не удалось подтвердить процесс")),str(T("The app started, but macOS did not return its launch time. Check the window manually.","Приложение запущено, но macOS не вернула время запуска. Проверьте окно вручную.")));return false;}
 put(p,"pid",num(pid));put(p,"started",real(send<double>(date,"timeIntervalSince1970")));erase(p,"lastError");
 put(p,"launchCheck",real(send<double>(send(cls("NSDate"),"date"),"timeIntervalSince1970")+8));
 note(sharesHistory(p)?"Profile launched with the shared thread database; credentials are not inspected.":"Profile launched; credentials are not inspected.");save();refresh();return true;
}
void launchAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);launch(p);}
void stopAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);Obj r=running(p);if(!r)return;
 if(confirm(T("Close this instance?","Закрыть этот экземпляр?"),T("A running agent task may be interrupted. AppDeck sends a normal quit request and never force-kills the process.","Текущая задача агента может прерваться. AppDeck отправит обычный запрос завершения, без принудительного убийства процесса."),T("Close instance","Закрыть экземпляр"))){
 erase(p,"launchCheck");erase(p,"lastError");save();
 if(!send<bool>(r,"terminate"))showError(str(T("Quit refused","Завершение отклонено")),str(T("Close the app window manually after saving your work.","Закройте окно приложения вручную после сохранения работы.")));note("Graceful termination requested.");}}
// Restart = the same polite terminate request, then a normal launch once the process is really gone.
Obj restartPending=nullptr; // profile id -> uptime deadline
double uptimeNow(){return send<double>(send(cls("NSProcessInfo"),"processInfo"),"systemUptime");}
bool restarting(Obj p){return restartPending&&get(restartPending,utf8(get(p,"id")))!=nullptr;}
void restartAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(!p)return;Obj r=running(p);if(!r){launch(p);return;}
 if(!confirm(T("Restart this instance?","Перезапустить этот экземпляр?"),T("A running agent task will be interrupted. AppDeck sends a normal quit request and launches the profile again once the window has closed. Unsent input in the window is lost.","Текущая задача агента прервётся. AppDeck отправит обычный запрос завершения и запустит профиль заново, когда окно закроется. Незавершённый ввод в окне будет потерян."),T("Restart","Перезапустить")))return;
 erase(p,"launchCheck");erase(p,"lastError");save();
 if(!send<bool>(r,"terminate")){showError(str(T("Quit refused","Завершение отклонено")),str(T("Close the app window manually, then click Launch.","Закройте окно приложения вручную, затем нажмите «Запустить».")));return;}
 if(!restartPending)restartPending=keep(dict());put(restartPending,utf8(get(p,"id")),real(uptimeNow()+25));note("Restart requested: graceful termination, relaunch when the process has exited.");refresh();
}
void restartPump(){
 if(!restartPending||!count(restartPending))return;Obj ids=send(restartPending,"allKeys");
 for(UInt i=0;i<count(ids);++i){Obj id=at(ids,i),p=nullptr;for(UInt j=0;j<count(profiles);++j)if(same(get(at(profiles,j),"id"),id)){p=at(profiles,j);break;}
  double deadline=send<double>(get(restartPending,utf8(id)),"doubleValue");
  if(!p){erase(restartPending,utf8(id));continue;}
  if(!running(p)){erase(restartPending,utf8(id));launch(p);continue;}
  if(uptimeNow()>deadline){erase(restartPending,utf8(id));showError(str(T("The window did not close","Окно не закрылось")),cat(get(p,"name"),str(T(": the app did not quit within 25 seconds — it may be asking for confirmation. Close the window manually and click Launch.",": приложение не завершилось за 25 секунд — возможно, оно спрашивает подтверждение. Закройте окно вручную и нажмите «Запустить»."))));}
 }
}
void renameAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(!p)return;Obj name=prompt(T("Profile name","Имя профиля"),T("This is only a label in AppDeck, not the account address.","Это только метка в AppDeck, не адрес аккаунта."),get(p,"name"));if(name){put(p,"name",name);save();buildUI();buildMenus();}}
// A new profile joins the profiles of its app (a duplicate sits right after its source); it can be moved later.
Obj newProfile(Obj a,Obj name,Int colorIndex,bool share,Obj after=nullptr){Obj p=dict();put(p,"id",uuid());put(p,"appId",get(a,"id"));put(p,"name",name);put(p,"color",num(colorIndex%6));put(p,"share",num(share));
 UInt place=count(profiles),source=after?send<UInt>(profiles,"indexOfObjectIdenticalTo:",after):place;
 if(source<count(profiles))place=source+1;else for(UInt i=0;i<count(profiles);++i)if(same(get(at(profiles,i),"appId"),get(a,"id")))place=i+1;
 send<void>(profiles,"insertObject:atIndex:",p,place);return p;}
// Changing the order: `p` goes right before or after `other`; every other profile keeps its relative place.
void moveProfileNextTo(Obj p,Obj other,bool after){
 if(!p||!other||p==other)return;keep(p);send<void>(profiles,"removeObjectIdenticalTo:",p);
 UInt i=send<UInt>(profiles,"indexOfObjectIdenticalTo:",other);if(i>count(profiles))i=count(profiles);else if(after)++i;
 send<void>(profiles,"insertObject:atIndex:",p,i);drop(p);put(state,"profileOrder",num(1));
 note("Profile order changed.");save();buildUI();buildMenus();
}
// Moves `p` to place `to` of `list`, the list the owner is looking at (the dock, or one app in the main window).
bool moveProfileInList(Obj p,Obj list,Int to){
 UInt from=p?send<UInt>(list,"indexOfObjectIdenticalTo:",p):count(list);if(from>=count(list))return false;
 if(to<0)to=0;if(to>=(Int)count(list))to=(Int)count(list)-1;if((UInt)to==from)return false;
 moveProfileNextTo(p,at(list,(UInt)to),(UInt)to>from);return true;
}
void profileMoveUpAction(Obj,Sel,Obj sender){Obj p=fromSender(sender),ps=visibleProfiles();if(p)moveProfileInList(p,ps,(Int)send<UInt>(ps,"indexOfObjectIdenticalTo:",p)-1);}
void profileMoveDownAction(Obj,Sel,Obj sender){Obj p=fromSender(sender),ps=visibleProfiles();if(p)moveProfileInList(p,ps,(Int)send<UInt>(ps,"indexOfObjectIdenticalTo:",p)+1);}
void duplicateAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(!p)return;
 Obj name=prompt(T("New independent profile","Новый независимый профиль"),T("Only the profile settings are copied. Sign-in, history and cookies are not.","Копируются только параметры профиля. Вход, история и cookies не переносятся."),cat(get(p,"name"),str(T(" · copy"," · копия"))));
 if(name){newProfile(appFor(p),name,integer(get(p,"color"))+1,isMaster(p)||truth(get(p,"share")),p);save();buildUI();buildMenus();}}
void profileErrorAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(!p||!get(p,"lastError")||previewMode)return;frontForModal();
 Obj a=make("NSAlert");send<void>(a,"setMessageText:",cat(get(p,"name"),str(T(" — last launch"," — последний запуск"))));send<void>(a,"setInformativeText:",get(p,"lastError"));
 send(a,"addButtonWithTitle:",str(T("OK","Понятно")));send(a,"addButtonWithTitle:",str(T("Clear the mark","Сбросить отметку")));bool clear=send<Int>(a,"runModal")==1001;drop(a);
 if(clear){erase(p,"lastError");save();buildUI();}
}
void reveal(Obj path){if(path)send<void>(workspace,"activateFileViewerSelectingURLs:",send(cls("NSArray"),"arrayWithObject:",url(path)));}
Obj masterFolder(Obj a){
 if(deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Codex)return baseSource(a)?baseSource(a):join(home,".codex");
 if(deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Claude)return join(home,"Library/Application Support/Claude");
 return get(a,"path");
}
void profileFolderAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(!p)return;if(isMaster(p)){reveal(masterFolder(appFor(p)));return;}Obj r=profileRoot(p);if(mkdirPrivate(r))reveal(r);}
void removeProfileAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(!p)return;if(running(p)||ownedProcess(p)){showError(str(T("Close the profile first","Сначала закройте профиль")),str(T("A running profile cannot be removed.","Удаление запущенного профиля недоступно.")));return;}
 if(Obj failure=groupOwnerRemovalError(p)){showError(str(T("The profile owns automations","Профиль выполняет автоматизации")),failure);return;}
 if(confirm(T("Remove the profile from AppDeck?","Убрать профиль из AppDeck?"),T("The entry is removed from the manager. Files, history and sign-in stay in the Profiles folder; you can delete them separately in Finder.","Запись будет удалена из менеджера. Файлы, история и авторизация останутся в папке Profiles; их можно удалить отдельно через Finder."),T("Remove profile","Убрать профиль"))){send<void>(profiles,"removeObjectIdenticalTo:",p);save();buildUI();buildMenus();}}
void shareAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(!p)return;if(isMaster(p)){showError(str(T("Primary profile","Основной профиль")),str(T("It uses the existing Codex workspace. Shared projects and automations are reconciled when it fully restarts.","Он использует существующую среду Codex. Общие проекты и автоматизации согласуются при его полном перезапуске.")));return;}if(running(p)||ownedProcess(p)){showError(str(T("Close the profile first","Сначала закройте профиль")),str(T("Shared settings cannot be changed while the profile is running.","Изменять схему общих настроек во время работы нельзя.")));return;}
 bool was=truth(get(p,"share"));Obj ch=join(profileRoot(p),"codex");
 if(was){
  if(Obj failure=groupOwnerRemovalError(p)){showError(str(T("The profile owns automations","Профиль выполняет автоматизации")),failure);return;}
  if(!confirm(T("Detach this profile's settings?","Отделить настройки профиля?"),T("config.toml and AGENTS.md stay local. Shared skills/rules are copied into the profile. The shared folders are not deleted.","config.toml и AGENTS.md останутся локальными. Общие skills/rules будут скопированы в профиль. Общие папки не удаляются."),T("Detach","Отделить")))return;
  for(const char* n:{"skills","rules"}){Obj dst=join(ch,n);if(symlink(dst)){Obj e=nullptr;Obj src=send(fileManager,"destinationOfSymbolicLinkAtPath:error:",dst,&e);
   if(!same(src,join(sharedRoot(appFor(p)),n))&&!same(src,canonical(join(settingsSource(appFor(p)),n)))){showError(str(T("Unexpected link","Неожиданная ссылка")),str(T("Check the profile folder manually.","Проверьте папку профиля вручную.")));return;}
   Obj tmp=cat(dst,str(".detached"));if(exists(tmp)||symlink(tmp)){showError(str(T("An unfinished copy exists","Есть незавершённая копия")),str(T("Remove the .detached folder manually before retrying.","Уберите папку .detached вручную перед повтором.")));return;}
   if(!send<bool>(fileManager,"copyItemAtPath:toPath:error:",src,tmp,&e)){showError(str(T("Could not copy","Не удалось скопировать")),send(e,"localizedDescription"));return;}
   if(!send<bool>(fileManager,"removeItemAtPath:error:",dst,&e)||!send<bool>(fileManager,"moveItemAtPath:toPath:error:",tmp,dst,&e)){showError(str(T("Could not detach the folder","Не удалось отделить папку")),send(e,"localizedDescription"));return;}
  }}
 }else{
  // Do not overwrite independent skills/rules when re-enabling sharing.
  for(const char* n:{"skills","rules"})if(exists(join(ch,n))&&!symlink(join(ch,n))){showError(str(T("Local folders need to be kept","Локальные папки нужно сохранить")),str(T("Move the local skills/rules out of the profile folder first. AppDeck never replaces them with a shared link automatically.","Сначала перенесите локальные skills/rules из папки профиля. AppDeck не заменяет их общей ссылкой автоматически.")));return;}
  if(!confirm(T("Use the shared settings?","Подключить общие настройки?"),T("On the next launch config.toml and AGENTS.md are updated from the shared folder. Previous versions are kept next to them with a .previous extension.","При следующем запуске config.toml и AGENTS.md будут обновлены из общей папки. Предыдущие версии сохраняются рядом с расширением .previous."),T("Connect","Подключить")))return;
 }
 put(p,"share",num(!was));save();buildUI();
}
Obj menuItem(Obj menu,const char* title,const char* action,Int tag=-1,const char* key=""){
 Obj i=send(send(cls("NSMenuItem"),"alloc"),"initWithTitle:action:keyEquivalent:",str(title),action?sel(action):nullptr,str(key));
 if(action)send<void>(i,"setTarget:",controller);send<void>(i,"setTag:",tag);send<void>(menu,"addItem:",i);drop(i);return i;
}
void separator(Obj menu){send<void>(menu,"addItem:",send(cls("NSMenuItem"),"separatorItem"));}
void moreAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(!p)return;Int tag=profileIndex(p);Obj m=make("NSMenu");send<void>(m,"setAutoenablesItems:",false);
 menuItem(m,T("Rename…","Переименовать…"),"renameProfile:",tag);menuItem(m,T("Duplicate without account…","Дублировать без аккаунта…"),"duplicateProfile:",tag);separator(m);
 // The order is shared with the side panel and ⌃⌥1…8; here it moves among this app's cards.
 {Obj ps=visibleProfiles();UInt place=send<UInt>(ps,"indexOfObjectIdenticalTo:",p);
  send<void>(menuItem(m,T("Move up","Переместить выше"),"profileMoveUp:",tag),"setEnabled:",place>0&&place<count(ps));send<void>(menuItem(m,T("Move down","Переместить ниже"),"profileMoveDown:",tag),"setEnabled:",place+1<count(ps));separator(m);}
 menuItem(m,T("Profile folder","Папка профиля"),"profileFolder:",tag);
 if(deck::adapter(utf8(get(appFor(p),"adapter")))==deck::Adapter::Codex)menuItem(m,truth(get(p,"share"))?T("Detach shared settings…","Отделить общие настройки…"):T("Use shared settings…","Подключить общие настройки…"),"toggleShare:",tag);
 separator(m);Obj again=menuItem(m,T("Restart…","Перезапустить…"),"restartProfile:",tag);send<void>(again,"setEnabled:",running(p)!=nullptr);
 Obj stop=menuItem(m,T("Close instance…","Закрыть экземпляр…"),"stopProfile:",tag);send<void>(stop,"setEnabled:",running(p)!=nullptr);
 menuItem(m,T("Remove from AppDeck…","Убрать из менеджера…"),"removeProfile:",tag);
 send<bool>(m,"popUpMenuPositioningItem:atLocation:inView:",(Obj)nullptr,Point{0,30},sender);drop(m);
}
Obj pickApp(){
 Obj p=send(cls("NSOpenPanel"),"openPanel");send<void>(p,"setTitle:",str(T("Choose an installed app","Выберите установленное приложение")));send<void>(p,"setPrompt:",str(T("Choose","Выбрать")));
 send<void>(p,"setCanChooseFiles:",true);send<void>(p,"setCanChooseDirectories:",false);send<void>(p,"setAllowsMultipleSelection:",false);send<void>(p,"setTreatsFilePackagesAsDirectories:",false);
 send<void>(p,"setAllowedFileTypes:",send(cls("NSArray"),"arrayWithObject:",str("app")));send<void>(p,"setDirectoryURL:",url(str("/Applications")));
 return send<Int>(p,"runModal")==1?canonical(send(send(p,"URL"),"path")):nullptr;
}
Obj label(Obj parent,const char* text,Rect f,double size=13,Obj c=nullptr,double weight=0){
 Obj v=send(cls("NSTextField"),"labelWithString:",str(text));send<void>(v,"setFrame:",f);send<void>(v,"setTextColor:",c?c:ink());
 send<void>(v,"setFont:",send(cls("NSFont"),"systemFontOfSize:weight:",size,weight));
 bool multi=strchr(text,'\n')||f.size.height>size*2.1;
 send<void>(v,"setMaximumNumberOfLines:",(Int)(multi?0:1));send<void>(v,"setLineBreakMode:",(Int)(multi?0:4));
 send<void>(send(v,"cell"),"setUsesSingleLineMode:",!multi);send<void>(send(v,"cell"),"setWraps:",multi);
 send<void>(parent,"addSubview:",v);return v;
}
Obj labelObj(Obj parent,Obj text,Rect f,double size=13,Obj c=nullptr,double weight=0){return label(parent,utf8(text),f,size,c,weight);}
Obj monoFont(double size,double weight){return send(cls("NSFont"),"monospacedDigitSystemFontOfSize:weight:",size,weight);}
Obj codeFont(double size,double weight){return send(cls("NSFont"),"monospacedSystemFontOfSize:weight:",size,weight);}
void continuous(Obj layer){send<void>(layer,"setCornerCurve:",str("continuous"));}
// Letter spacing in points (eyebrows, titles), keeping the label's font, colour, alignment and truncation.
Obj kern(Obj v,double points){
 Obj style=make("NSMutableParagraphStyle");send<void>(style,"setAlignment:",send<Int>(v,"alignment"));send<void>(style,"setLineBreakMode:",(Int)4);
 Obj a=dict();put(a,"NSFont",send(v,"font"));put(a,"NSColor",send(v,"textColor"));put(a,"NSKern",real(points));put(a,"NSParagraphStyle",style);drop(style);
 Obj s=send(send(cls("NSAttributedString"),"alloc"),"initWithString:attributes:",send(v,"stringValue"),a);send<void>(v,"setAttributedStringValue:",s);drop(s);return v;
}
// Small caps caption above a group ("APPS", "ACCOUNT", "WEEK").
Obj caption(Obj parent,const char* text,Rect f,double size=11){return kern(label(parent,text,f,size,faint(),.3),size*.06);}
double textWidth(const char* text,double size,double weight){
 Obj attrs=dict();send<void>(attrs,"setObject:forKey:",send(cls("NSFont"),"systemFontOfSize:weight:",size,weight),str("NSFont"));
 return send<Extent>(str(text),"sizeWithAttributes:",attrs).width;
}
double measure(const char* text,Obj font){Obj attrs=dict();put(attrs,"NSFont",font);return send<Extent>(str(text),"sizeWithAttributes:",attrs).width;}
// Width of a capsule button: 14 on each side, a 14-pt icon and a 6-pt gap when there is one.
double fitWidth(const char* title,bool icon,double size=13){return (double)(Int)(textWidth(title,size,.3)+(icon?20:0)+30);}
// One button system for the whole app: borderless capsules on layers, with hover. The style name lives in
// the view identifier so the hover handlers need no per-instance storage.
//   primary   — the one solid bone button of a page;   prominent — the main action inside a card;
//   secondary — neutral glass;  danger — destructive;  overlay/ghost — transparent hit areas and rows.
Obj buttonFill(Obj self,bool hover){
 Obj k=send(self,"identifier");
 if(!send<bool>(self,"isEnabled"))return fill(.04);
 if(same(k,str("primary")))return hover?hex(0xFFFFFF):ink();
 if(same(k,str("prominent")))return fill(hover?.19:.14);
 if(same(k,str("danger")))return hex(0xFF7A6B,hover?.22:.12);
 if(same(k,str("overlay")))return hover?fill(.06):fill(0);
 if(same(k,str("ghost")))return hover?fill(.07):fill(0);
 return fill(hover?.12:.08);
}
void buttonEntered(Obj self,Sel,Obj){if(send<bool>(self,"isEnabled"))send<void>(send(self,"layer"),"setBackgroundColor:",send(buttonFill(self,true),"CGColor"));}
void buttonExited(Obj self,Sel,Obj){send<void>(send(self,"layer"),"setBackgroundColor:",send(buttonFill(self,false),"CGColor"));}
bool firstMouse(Obj,Sel,Obj){return true;}
Obj symbol(const char* name,double size=14);
Obj styledButton(Class kind,Obj parent,const char* title,const char* action,Rect f,Int tag,const char* style,double radius=10,double size=13,const char* icon=nullptr){
 bool primary=strcmp(style,"primary")==0,prominent=strcmp(style,"prominent")==0,danger=strcmp(style,"danger")==0,flat=strcmp(style,"overlay")==0||strcmp(style,"ghost")==0;
 Obj b=send(send((Obj)kind,"alloc"),"initWithFrame:",f);send<void>(b,"setTitle:",str(title));send<void>(b,"setBordered:",false);
 send<void>(b,"setFont:",send(cls("NSFont"),"systemFontOfSize:weight:",size,primary||prominent?0.3:0.23));send<void>(b,"setTarget:",controller);send<void>(b,"setAction:",sel(action));send<void>(b,"setTag:",tag);
 send<void>(b,"setIdentifier:",str(style));send<void>(b,"setWantsLayer:",true);Obj layer=send(b,"layer");
 send<void>(layer,"setCornerRadius:",flat?radius:f.size.height/2);continuous(layer); // every control is a capsule
 send<void>(layer,"setBackgroundColor:",send(buttonFill(b,false),"CGColor"));
 if(!flat&&!primary){send<void>(layer,"setBorderWidth:",1.0);send<void>(layer,"setBorderColor:",send(danger?hex(0xFF7A6B,.28):fill(prominent?.14:.10),"CGColor"));}
 if(primary){send<void>(layer,"setShadowColor:",send(color(0,0,0,1),"CGColor"));send<void>(layer,"setShadowOpacity:",(float).35);send<void>(layer,"setShadowRadius:",2.0);send<void>(layer,"setShadowOffset:",Extent{0,-1});}
 if(icon){if(*title)send<void>(b,"setTitle:",cat(str(" "),str(title))); // a word space between icon and title
  send<void>(b,"setImage:",symbol(icon,size-1));send<void>(b,"setImagePosition:",(UInt)(*title?7:1));send<void>(b,"setImageHugsTitle:",true);} // NSImageLeading / NSImageOnly
 send<void>(b,"setContentTintColor:",primary?onInk():danger?dangerColor():ink());
 // NSTrackingMouseEnteredAndExited | ActiveAlways | InVisibleRect: hover also works in the nonactivating dock.
 Obj area=send(send(cls("NSTrackingArea"),"alloc"),"initWithRect:options:owner:userInfo:",rect(0,0,0,0),(UInt)(0x01|0x80|0x200),b,(Obj)nullptr);
 send<void>(b,"addTrackingArea:",area);drop(area);send<void>(parent,"addSubview:",b);drop(b);return b;
}
Obj button(Obj parent,const char* title,const char* action,Rect f,Int tag=-1,bool primary=false,const char* icon=nullptr){return styledButton(deckButtonClass,parent,title,action,f,tag,primary?"primary":"secondary",10,13,icon);}
// A round icon-only button with a tooltip and an accessibility name.
Obj iconButton(Obj parent,const char* icon,const char* action,Rect f,Int tag,const char* tip,const char* style="secondary"){
 Obj b=styledButton(deckButtonClass,parent,"",action,f,tag,style,f.size.height/2,13,icon);send<void>(b,"setToolTip:",str(tip));send<void>(b,"setAccessibilityLabel:",str(tip));return b;
}
// Disabled controls keep their place and shape; only the fill, rim and text fade.
void disable(Obj b){
 send<void>(b,"setEnabled:",false);Obj l=send(b,"layer");send<void>(l,"setBackgroundColor:",send(fill(.04),"CGColor"));send<void>(l,"setBorderColor:",send(fill(.06),"CGColor"));
 send<void>(b,"setContentTintColor:",dim());
}
Obj checkbox(Obj parent,const char* title,Rect f,bool on){
 Obj b=send(cls("NSButton"),"checkboxWithTitle:target:action:",str(title),controller,sel("noop:"));send<void>(b,"setFrame:",f);send<void>(b,"setState:",(Int)(on?1:0));send<void>(parent,"addSubview:",b);return b;
}
Obj panel(Obj parent,Rect f,Obj bg,double radius=14){
 Obj p=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",f);send<void>(p,"setWantsLayer:",true);Obj layer=send(p,"layer");send<void>(layer,"setBackgroundColor:",send(bg,"CGColor"));send<void>(layer,"setCornerRadius:",radius);continuous(layer);
 send<void>(parent,"addSubview:",p);drop(p);return p;
}
// A glass card: a faint fill with a hairline rim.
Obj card(Obj parent,Rect f,double radius=16,Obj bg=nullptr){
 Obj c=panel(parent,f,bg?bg:surface(),radius);Obj l=send(c,"layer");send<void>(l,"setBorderWidth:",1.0);send<void>(l,"setBorderColor:",send(hairline(),"CGColor"));return c;
}
// A key cap such as ⌘1 or ⌃⌥Space: a raised glass key with a darker bottom edge.
double keycapWidth(const char* text,double size=11){double w=textWidth(text,size,.3)+14;return w<30?30:(double)(Int)(w+.99);}
Obj keycap(Obj parent,const char* text,Rect f,double size=11){
 double w=f.size.width,h=f.size.height;Obj k=panel(parent,f,fill(.07),h>20?6:5);Obj l=send(k,"layer");
 send<void>(l,"setBorderWidth:",1.0);send<void>(l,"setBorderColor:",send(fill(.10),"CGColor"));send<void>(l,"setMasksToBounds:",true);
 panel(k,rect(0,h-1,w,1),color(0,0,0,.35),0);
 Obj t=label(k,text,rect(0,(h-15)/2,w,15),size,muted(),.3);send<void>(t,"setAlignment:",alignCenter);return k;
}
// The plan of an account (PLUS, PRO, MAX, TEAM): a neutral capsule, never coloured.
Obj planBadge(Obj parent,Rect f){Obj b=panel(parent,f,fill(.06),f.size.height/2);Obj l=send(b,"layer");send<void>(l,"setBorderWidth:",1.0);send<void>(l,"setBorderColor:",send(fill(.10),"CGColor"));return b;}
// A status light. Running: a green LED with a soft glow; stopped: an empty ring; problems: amber.
Obj statusDot(Obj parent,Rect f){return panel(parent,f,fill(0),f.size.width/2);}
void statusDotColor(Obj dot,Obj c,bool glow,bool hollow=false){
 Obj l=send(dot,"layer");send<void>(l,"setBackgroundColor:",send(hollow?fill(0):c,"CGColor"));
 send<void>(l,"setBorderWidth:",hollow?1.5:0.0);send<void>(l,"setBorderColor:",send(c,"CGColor"));
 send<void>(l,"setShadowColor:",send(c,"CGColor"));send<void>(l,"setShadowOpacity:",(float)(glow?.75:0));send<void>(l,"setShadowRadius:",4.0);send<void>(l,"setShadowOffset:",Extent{0,0});
}
// The profile's identity: a small round badge in its colour on the corner of the app icon.
void identityBadge(Obj parent,Rect icon,Obj c,double size=12,Obj ring=nullptr){
 double r=size+4;Obj outer=panel(parent,rect(icon.origin.x+icon.size.width-r+1,icon.origin.y+icon.size.height-r+1,r,r),ring?ring:hex(0x29292B),r/2);
 panel(outer,rect(2,2,size,size),c,size/2);
}
Obj popup(Obj parent,Rect f,const char* const* choices,UInt n,Int selectedIndex){
 Obj p=send(send(cls("NSPopUpButton"),"alloc"),"initWithFrame:pullsDown:",f,false);
 for(UInt i=0;i<n;++i)send<void>(p,"addItemWithTitle:",str(choices[i]));send<void>(p,"selectItemAtIndex:",selectedIndex);send<void>(parent,"addSubview:",p);drop(p);return p;
}
void chooseApplication(Obj,Sel,Obj){
 Obj path=pickApp();if(!path)return;
 if(!deck::suffix(utf8(path),".app")||!exists(join(path,"Contents/Info.plist"))){showError(str(T("This is not a macOS app","Это не приложение macOS")),str(T("Choose an .app bundle with an Info.plist.","Выберите пакет .app с Info.plist.")));return;}
 for(UInt i=0;i<count(apps);++i)if(same(canonical(get(at(apps,i),"path")),path)){drop(selected);selected=keep(get(at(apps,i),"id"));page=0;save();buildUI();return;}
 Obj bundle=send(cls("NSBundle"),"bundleWithPath:",path),info=send(bundle,"infoDictionary");
 Obj bid=get(info,"CFBundleIdentifier");Obj name=get(info,"CFBundleDisplayName");if(!name)name=get(info,"CFBundleName");if(!name)name=send(send(path,"lastPathComponent"),"stringByDeletingPathExtension");
 bool electron=exists(join(path,"Contents/Frameworks/Electron Framework.framework"));
 auto detected=deck::detect(utf8(bid),utf8(name),electron);
 bool codexCandidate=detected==deck::Adapter::Codex||(electron&&same(name,str("ChatGPT")));
 if(detected==deck::Adapter::Unsupported&&!codexCandidate){showError(str(T("No adapter for this .app","Для этого .app нет адаптера")),str(T("This version supports profiles for Codex, Claude Desktop, VS Code/Cursor and Chromium. Other Electron apps have an experimental mode. Copying an arbitrary native .app does not separate its accounts.","Эта версия поддерживает профили Codex, VS Code/Cursor и Chromium. Для прочих Electron-приложений доступен экспериментальный режим. Копирование произвольного нативного .app не разделяет его аккаунты.")));return;}
 Obj a=make("NSAlert");send<void>(a,"setMessageText:",cat(str(T("Add ","Добавить ")),name));
 send<void>(a,"setInformativeText:",str(T("The original .app is not modified. AppDeck creates profile folders, not copies of the program: each profile has its own sign-in. With the Codex adapter, tokens are kept in auth.json inside the private profile folder, not in the Keychain. Claude Desktop gets its own data folder through its CLAUDE_USER_DATA_DIR variable; the settings and history of its Code tab (~/.claude) stay shared. Check compatibility with your app version.","Исходное .app не изменяется. Создаются папки профилей, а не копии программы: у каждого профиля свой вход. В адаптере Codex токены хранятся в auth.json внутри закрытой папки профиля, а не в Keychain. Claude Desktop получает свою папку данных через штатную переменную CLAUDE_USER_DATA_DIR; настройки и история вкладки Code (~/.claude) остаются общими. Совместимость нужно проверить в вашей версии приложения.")));
 Obj accessory=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,430,143));
 label(accessory,T("Adapter","Адаптер"),rect(0,6,115,20));
 const char* choices[]={T("Codex (experimental)","Codex (экспериментальный)"),"VS Code / Cursor","Chromium / Chrome / Edge",T("Electron (experimental)","Electron (экспериментальный)"),"Claude Desktop"};
 Int index=codexCandidate?0:detected==deck::Adapter::VSCode?1:detected==deck::Adapter::Chromium?2:detected==deck::Adapter::Claude?4:3;
 Obj selector=popup(accessory,rect(120,0,305,28),choices,5,index);
 Obj current=checkbox(accessory,T("Add the existing sign-in (current window) as the primary profile","Добавить уже установленный вход (текущее окно) как основной профиль"),rect(0,112,428,22),index!=0);send<void>(current,"setEnabled:",index!=0);
 send<void>(current,"setToolTip:",str(T("The primary profile is the app itself with its usual data. For Codex it is added when you connect the base workspace.","Основной профиль — это само приложение с его обычными данными. Для Codex он добавляется при подключении базовой среды.")));
 label(accessory,T("Profiles","Профилей"),rect(0,43,110,20));const char* nums[]={"1","2","4"};Obj number=popup(accessory,rect(120,36,95,28),nums,3,codexCandidate?2:0);
 label(accessory,T("Sign in separately in each instance.","Вход выполните отдельно в каждом экземпляре."),rect(0,83,425,22),12,muted());
 send<void>(a,"setAccessoryView:",accessory);send(a,"addButtonWithTitle:",str(T("Add","Добавить")));send(a,"addButtonWithTitle:",str(T("Cancel","Отмена")));
 if(send<Int>(a,"runModal")==1000){
  const char* adapters[]={"codex","vscode","chromium","electron","claude"};Int idx=send<Int>(selector,"indexOfSelectedItem");if(idx<0||idx>4)idx=3;
  Obj item=dict();put(item,"id",uuid());put(item,"name",name);put(item,"path",path);put(item,"bundleId",bid?bid:str(""));put(item,"adapter",str(adapters[idx]));
  if(!ensureShared(item)){showError(str(T("No access to the data folder","Нет доступа к папке данных")),str(T("Could not create the shared settings.","Не удалось создать общие настройки.")));drop(accessory);drop(a);return;}
  add(apps,item);drop(selected);selected=keep(get(item,"id"));Int k=send<Int>(number,"indexOfSelectedItem");Int n=k==2?4:k==1?2:1;
  if(idx!=0&&send<Int>(current,"state")==1){Obj original=newProfile(item,str(T("Primary · current","Основной · текущий")),0,false);put(original,"master",boolean(true));}
  const char* names[]={T("Work","Рабочий"),T("Personal","Личный"),T("Account 3","Аккаунт 3"),T("Account 4","Аккаунт 4")};for(Int i=0;i<n;++i)newProfile(item,str(names[i]),i+1,idx==0);
  page=0;save();buildUI();buildMenus();note("Application registered; no application bundle was modified.");
  if(idx==0)connectBaseAction(nullptr,nullptr,nullptr);
 }
 drop(accessory);drop(a);
}
void newProfileAction(Obj,Sel,Obj){Obj a=currentApp();if(!a){chooseApplication(nullptr,nullptr,nullptr);return;}
 Obj name=prompt(T("New profile","Новый профиль"),T("The account starts empty. Sign in after launching it.","Аккаунт будет пустым. Войдите в него после запуска."),str(T("New account","Новый аккаунт")));if(!name)return;
 newProfile(a,name,(Int)count(profiles),deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Codex);page=0;save();buildUI();buildMenus();}
void selectAppAction(Obj,Sel,Obj sender){Int n=send<Int>(sender,"tag");if(n<0||(UInt)n>=count(apps))return;drop(selected);selected=keep(get(at(apps,n),"id"));page=0;save();buildUI();buildMenus();}
void changePathAction(Obj,Sel,Obj){Obj a=currentApp();if(!a)return;for(UInt i=0;i<count(profiles);++i)if(same(get(at(profiles,i),"appId"),get(a,"id"))&&running(at(profiles,i))){showError(str(T("Some profiles are running","Есть запущенные профили")),str(T("Close all instances of this group first.","Сначала закройте все экземпляры этой группы.")));return;}
 Obj path=pickApp();if(!path)return;Obj bundle=send(cls("NSBundle"),"bundleWithPath:",path);Obj bid=send(bundle,"bundleIdentifier");
 if(!bid){showError(str(T("Invalid app","Некорректное приложение")),str(T("Bundle ID not found.","Bundle ID не найден.")));return;}
 if(!same(bid,get(a,"bundleId"))&&!confirm(T("The app has a different bundle ID","У приложения другой Bundle ID"),T("Existing profile data may be incompatible. Only switch to a trusted, compatible version of the app.","Существующие данные профиля могут быть несовместимы. Меняйте путь только на доверенную совместимую версию приложения."),T("Use","Использовать")))return;
 put(a,"path",path);put(a,"bundleId",bid);save();buildUI();
}
void removeAppAction(Obj,Sel,Obj){Obj a=currentApp();if(!a)return;
 for(UInt i=0;i<count(profiles);++i)if(same(get(at(profiles,i),"appId"),get(a,"id"))&&running(at(profiles,i))){showError(str(T("Close the instances first","Сначала закройте экземпляры")),str(T("A running group cannot be removed.","Запущенную группу удалять нельзя.")));return;}
 if(!confirm(T("Remove the app and its profiles?","Убрать приложение и его профили?"),T("Only the manager's entries are removed. The app itself and its data folders stay on disk.","Удаляются только записи менеджера. Само приложение и папки данных останутся на диске."),T("Remove","Убрать")))return;
 for(Int i=(Int)count(profiles)-1;i>=0;--i)if(same(get(at(profiles,i),"appId"),get(a,"id")))send<void>(profiles,"removeObjectAtIndex:",(UInt)i);
 send<void>(apps,"removeObjectIdenticalTo:",a);drop(selected);selected=count(apps)?keep(get(at(apps,0),"id")):nullptr;save();buildUI();buildMenus();
}
void pageAction(Obj,Sel,Obj sender){page=send<Int>(sender,"tag");buildUI();}
void showWindowAction(Obj,Sel,Obj){setEdgeVisible(false);send<bool>(app,"setActivationPolicy:",(Int)0);send<void>(window,"makeKeyAndOrderFront:",(Obj)nullptr);send<void>(app,"activateIgnoringOtherApps:",true);}
void launchAllAction(Obj,Sel,Obj){Obj ps=visibleProfiles();if(count(ps)>1&&!confirm(T("Launch all profiles of this group?","Запустить все профили этой группы?"),T("Each instance uses its own memory. Do the first sign-ins one after another, not in several windows at once.","Каждый экземпляр расходует память отдельно. Первую авторизацию выполняйте последовательно, не в нескольких окнах одновременно."),T("Launch","Запустить")))return;
 for(UInt i=0;i<count(ps);++i)if(isMaster(at(ps,i))&&!launch(at(ps,i),false))return;
 for(UInt i=0;i<count(ps);++i)if(!isMaster(at(ps,i))&&!launch(at(ps,i),false))break;refresh();}
void openSharedFolder(Obj,Sel,Obj){Obj a=currentApp();if(a&&ensureShared(a))send<bool>(workspace,"openURL:",url(settingsSource(a)));}
void openSharedFile(Obj,Sel,Obj sender){Obj a=currentApp();if(!a||!ensureShared(a))return;Int tag=send<Int>(sender,"tag");const char* names[]={"config.toml","AGENTS.md","skills","rules"};if(tag>=0&&tag<4)send<bool>(workspace,"openURL:",url(join(settingsSource(a),names[tag])));}
void importSettings(Obj,Sel,Obj){
 Obj a=currentApp();if(!a||!ensureShared(a))return;
 if(baseSource(a)){showError(str(T("Source already connected","Источник уже подключён")),str(T("Linked profiles read the current workspace before they launch. Importing into the fallback shared folder is not needed.","Связанные профили читают текущую среду перед запуском. Импорт в резервную общую папку не нужен.")));return;}
 if(!confirm(T("Import settings from ~/.codex?","Импортировать настройки ~/.codex?"),T("Only config.toml and AGENTS.md are copied into this group's shared folder. auth.json, cookies, history and databases are never read or copied. config.toml itself may contain MCP keys and workspace restrictions; review it before using it with another account. The current shared files are kept as .previous.","Будут скопированы только config.toml и AGENTS.md в общую папку этой группы. auth.json, cookies, история и базы данных не читаются и не копируются. Сам config.toml может содержать ключи MCP и ограничения рабочего пространства; проверьте его перед использованием с другим аккаунтом. Текущие общие файлы сохраняются как .previous."),T("Import","Импортировать")))return;
 Obj source=join(home,".codex"),dest=sharedRoot(a);bool any=false;
 for(const char* name:{"config.toml","AGENTS.md"}){Obj s=join(source,name);if(exists(s)){any=true;if(!copySettingsFile(s,join(dest,name))){showError(str(T("Import not finished","Импорт не завершён")),str(T("A file is unavailable, too large or a symbolic link. Check the shared folder.","Файл недоступен, слишком большой или является символической ссылкой. Проверьте общую папку.")));return;}}}
 showError(str(any?T("Settings imported","Настройки импортированы"):T("Files not found","Файлы не найдены")),str(any?T("They apply to shared profiles on their next launch. Accounts and history were not touched.","Они применятся к общим профилям при следующем запуске. Аккаунты и история остались нетронутыми."):T("~/.codex has no config.toml or AGENTS.md. You can create the settings in the shared folder.","В ~/.codex нет config.toml и AGENTS.md. Можно создать настройки через общую папку.")));
}
#include "workspace_actions.hpp"
// Gatekeeper runs a quarantined, never-moved bundle from a random read-only path.
bool translocated(){return strstr(utf8(send(send(cls("NSBundle"),"mainBundle"),"bundlePath")),"/AppTranslocation/")!=nullptr;}
void openDataAction(Obj,Sel,Obj){send<bool>(workspace,"openURL:",url(dataRoot));}
void addProjectAction(Obj,Sel,Obj){
 Obj p=send(cls("NSOpenPanel"),"openPanel");send<void>(p,"setTitle:",str(T("Add a shared project folder","Добавить общую папку проекта")));send<void>(p,"setCanChooseDirectories:",true);send<void>(p,"setCanChooseFiles:",false);send<void>(p,"setAllowsMultipleSelection:",false);
 if(send<Int>(p,"runModal")!=1)return;Obj path=canonical(send(send(p,"URL"),"path"));
 if(groupSyncEnabled(currentApp())){Obj failure=groupSyncEditProject(currentApp(),path,nullptr);if(failure)showError(str(T("The project is waiting for sync","Проект ожидает синхронизации")),failure);page=2;buildUI();return;}
 for(UInt i=0;i<count(projects);++i)if(same(get(at(projects,i),"path"),path))return;
 Obj item=dict();put(item,"path",path);put(item,"name",send(path,"lastPathComponent"));add(projects,item);save();page=2;buildUI();
}
void projectAction(Obj,Sel,Obj sender){Int tag=send<Int>(sender,"tag");Int index=tag/3,action=tag%3;if(index<0||(UInt)index>=count(projects))return;Obj p=at(projects,index);
 if(action==0)send<bool>(workspace,"openURL:",url(get(p,"path")));
 else if(action==1){Obj pb=send(cls("NSPasteboard"),"generalPasteboard");send<Int>(pb,"clearContents");send<bool>(pb,"setString:forType:",get(p,"path"),str("public.utf8-plain-text"));}
 else if(get(p,"syncProjectId")){Obj a=nullptr;for(UInt i=0;i<count(apps);++i)if(same(get(at(apps,i),"id"),get(p,"syncAppId")))a=at(apps,i);
  if(a&&confirm(T("Remove the project from all profiles?","Убрать проект из всех профилей?"),T("The folder, files and tasks are kept. Running windows update the list after a full restart.","Папка, файлы и задачи сохранятся. Работающие окна обновят список после полного перезапуска."),T("Remove project","Убрать проект"))){Obj failure=groupSyncEditProject(a,nullptr,get(p,"syncProjectId"));if(failure)showError(str(T("The removal is waiting for sync","Удаление ожидает синхронизации")),failure);buildUI();}}
 else if(confirm(T("Remove the folder from the list?","Убрать папку из списка?"),T("The project folder and files are not deleted.","Папка и файлы проекта не удаляются."),T("Remove","Убрать"))){send<void>(projects,"removeObjectAtIndex:",(UInt)index);save();buildUI();}
}
void helpAction(Obj,Sel,Obj){
 Obj path=send(send(cls("NSBundle"),"mainBundle"),"pathForResource:ofType:",str("Help"),str("html"));if(path)send<bool>(workspace,"openURL:",url(path));
}
void diagnosticsAction(Obj,Sel,Obj){
 Obj lines=array();add(lines,cat(str("AppDeck "),str(VERSION)));
 add(lines,str("Build: local clang against the system AppKit."));
 add(lines,cat(str("Language: "),str(ruUI?"ru":"en")));
 add(lines,cat(str("Bundle: "),send(send(cls("NSBundle"),"mainBundle"),"bundlePath")));
 add(lines,str(translocated()?"App Translocation: YES (quarantined copy; Accessibility permission cannot persist)":"App Translocation: no"));
 add(lines,str(windowAPI.permission(false)?"Accessibility: granted":"Accessibility: not granted (launch/activate still work; tiling needs it)"));
 add(lines,send(send(cls("NSProcessInfo"),"processInfo"),"operatingSystemVersionString"));
#if defined(__aarch64__) || defined(__arm64__)
 add(lines,str("Architecture: arm64"));
#else
 add(lines,str("Architecture: x86_64"));
#endif
 add(lines,formatInt("Applications: %ld",(Int)count(apps)));add(lines,formatInt("Profiles: %ld",(Int)count(profiles)));
 add(lines,str("Launch: NSWorkspace new instance. No tokens/config contents included below."));
 for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);Obj line=cat(cat(cat(get(a,"name"),str(" | ")),get(a,"adapter")),exists(get(a,"path"))?str(" | installed"):str(" | MISSING"));
  line=cat(line,str(baseSource(a)?" | base connected":" | no base"));line=cat(line,str(!get(a,"sharedHistory")?" | history: not chosen":truth(get(a,"sharedHistory"))?" | history: shared (CODEX_SQLITE_HOME)":" | history: separate"));
  line=cat(line,str(!get(a,"usageLimits")?" | limits: not chosen":usageEnabled(a)?" | limits: on (codex app-server)":" | limits: off"));add(lines,line);}
 for(UInt i=0;i<count(profiles);++i){Obj u=usageOf(at(profiles,i));if(!u)continue;Obj e=get(u,"error");
  add(lines,cat(formatInt("Profile %ld limits: ",(Int)i+1),e?cat(str("no numbers — "),e):formatInt("ok, %ld window(s)",(Int)count(get(u,"windows")))));}
 for(UInt i=0;i<count(events);++i)add(lines,at(events,i));
 Obj p=send(cls("NSSavePanel"),"savePanel");send<void>(p,"setNameFieldStringValue:",str("AppDeck-diagnostics.txt"));
 if(send<Int>(p,"runModal")==1){Obj file=send(send(p,"URL"),"path");if(writeText(file,send(lines,"componentsJoinedByString:",str("\n"))))reveal(file);else showError(str(T("Could not save","Не удалось сохранить")),str(T("Choose another folder.","Выберите другую папку.")));}
}
// A vertical scroll area with an overlay scroller, so content keeps the page's full width (no gutter).
Obj scrollDocument(Obj parent,Rect f,double docHeight){
 Obj scroll=send(send(cls("NSScrollView"),"alloc"),"initWithFrame:",f);send<void>(scroll,"setDrawsBackground:",false);send<void>(scroll,"setHasVerticalScroller:",true);send<void>(scroll,"setAutohidesScrollers:",true);
 send<void>(scroll,"setScrollerStyle:",(Int)1);send<void>(scroll,"setScrollerKnobStyle:",(Int)2);send<void>(scroll,"setHorizontalScrollElasticity:",(Int)1);
 Obj doc=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,f.size.width,docHeight));send<void>(scroll,"setDocumentView:",doc);send<void>(parent,"addSubview:",scroll);drop(scroll);drop(doc);return doc;
}
#include "usage_views.hpp"
Obj imageView(Obj parent,Obj image,Rect f,Obj tint=nullptr){
 Obj v=send(send(cls("NSImageView"),"alloc"),"initWithFrame:",f);send<void>(v,"setImage:",image);send<void>(v,"setImageScaling:",(UInt)3);if(tint)send<void>(v,"setContentTintColor:",tint);
 send<void>(parent,"addSubview:",v);drop(v);return v;
}
void appIcon(Obj parent,Obj path,Rect f){imageView(parent,send(workspace,"iconForFile:",path),f);}
Obj symbol(const char* name,double size){
 Obj image=send(cls("NSImage"),"imageWithSystemSymbolName:accessibilityDescription:",str(name),(Obj)nullptr);
 Obj config=send(cls("NSImageSymbolConfiguration"),"configurationWithPointSize:weight:",size,0.23);return image&&config?send(image,"imageWithSymbolConfiguration:",config):image;
}
// An SF Symbol on a small glass tile (list rows). Monochrome; a problem row tints its glyph amber.
Obj iconTile(Obj parent,Rect f,const char* name,Obj tint=nullptr){
 Obj t=panel(parent,f,fill(.07),f.size.width*.28);Obj l=send(t,"layer");send<void>(l,"setBorderWidth:",1.0);send<void>(l,"setBorderColor:",send(hairline(),"CGColor"));
 double g=(double)(Int)(f.size.width/2);imageView(t,symbol(name,g*.8),rect((f.size.width-g)/2,(f.size.height-g)/2,g,g),tint?tint:muted());return t;
}
// Sidebar row: an icon, a title, an optional trailing count and a transparent hover/click layer.
Obj navRow(Obj parent,Rect f,Obj image,bool tinted,Obj title,const char* action,Int tag,bool selected,Obj trailing=nullptr){
 Obj row=panel(parent,f,selected?fill(.10):fill(0),10);double h=f.size.height;
 if(selected){Obj l=send(row,"layer");send<void>(l,"setBorderWidth:",1.0);send<void>(l,"setBorderColor:",send(fill(.06),"CGColor"));}
 if(image)imageView(row,image,rect(10,(h-16)/2,16,16),tinted?(selected?ink():muted()):nullptr);
 labelObj(row,title,rect(36,(h-17)/2,f.size.width-46-(trailing?28:0),17),13,selected?ink():inkSoft(),selected?.3:.23);
 if(trailing){Obj t=labelObj(row,trailing,rect(f.size.width-10-28,(h-15)/2,28,15),11,faint(),.23);send<void>(t,"setFont:",monoFont(11,.23));send<void>(t,"setAlignment:",alignRight);}
 Obj b=styledButton(deckButtonClass,row,"",action,rect(0,0,f.size.width,h),tag,"overlay",10);send<void>(b,"setAccessibilityLabel:",title);return b;
}
// Multi-line text with a fixed line height (notes and explanations).
Obj lines(Obj parent,const char* text,Rect f,double size,Obj c,double lineHeight){
 Obj v=label(parent,text,f,size,c,0);Obj style=make("NSMutableParagraphStyle");send<void>(style,"setMinimumLineHeight:",lineHeight);send<void>(style,"setMaximumLineHeight:",lineHeight);
 Obj a=dict();put(a,"NSFont",send(v,"font"));put(a,"NSColor",c);put(a,"NSParagraphStyle",style);drop(style);
 Obj s=send(send(cls("NSAttributedString"),"alloc"),"initWithString:attributes:",str(text),a);send<void>(v,"setAttributedStringValue:",s);drop(s);return v;
}
Obj cardFact(Obj c,const char* cap,const char* value,double x,double w){
 caption(c,cap,rect(x,92,w,14),10.5);return label(c,value,rect(x,108,w,18),13,ink(),.23);
}
// Account label, plan badge and limit strip follow the cached record; called on build and on every refresh.
void usageApplyCard(Obj ref,Obj p){
 Obj u=usageOf(p),accountLabel=get(ref,"account");if(!accountLabel)return;Obj email=get(u,"email");
 Obj twin=usageTwin(p);send<void>(accountLabel,"setTextColor:",twin?warnColor():ink());
 bool claudeCard=usageIsClaude(appFor(p));
 send<void>(accountLabel,"setStringValue:",email?(twin?cat(str("⚠ "),email):email):str(isMaster(p)?(claudeCard?T("Current sign-in","Текущий вход"):T("Current Codex","Текущий Codex")):T("Separate sign-in","Отдельный вход")));send<void>(accountLabel,"setToolTip:",!email?(Obj)nullptr:twin?cat(cat(cat(str(T("Same account as in profile “","Тот же аккаунт, что и в профиле «")),get(twin,"name")),str(T("”: they share one limit. To use another account in this profile, sign out in its window and sign in with the right one.\n","»: лимит у них общий. Чтобы профиль работал под другим аккаунтом, выйдите из аккаунта в его окне и войдите под нужным.\n"))),email):cat(str(claudeCard?T("The Claude Code sign-in AppDeck uses to ask for limits. It must match the account in this profile's window: ","Вход Claude Code, по которому AppDeck спрашивает лимиты. Он должен совпадать с аккаунтом в окне этого профиля: "):T("Account signed in to this profile: ","Аккаунт, вошедший в этот профиль: ")),email));
 Obj plan=get(u,"plan");Obj pill=get(ref,"planPill");usageHide(pill,!plan);
 if(plan&&pill){Obj upper=send(plan,"uppercaseString");double pw=(double)(Int)(textWidth(utf8(upper),10,.4)+send<UInt>(upper,"length")*.8+16.99),right=send<double>(get(ref,"planRight"),"doubleValue");
  send<void>(pill,"setFrame:",rect(right-pw,30,pw,20));Obj pl=get(ref,"planLabel");send<void>(pl,"setFrame:",rect(0,3,pw,14));send<void>(pl,"setStringValue:",upper);kern(pl,.8);}
 usageApplyStrip(get(ref,"usage"),p);
}
const char* adapterTitle(Obj a){switch(deck::adapter(utf8(get(a,"adapter")))){case deck::Adapter::Codex:return "Codex";case deck::Adapter::VSCode:return "VS Code";case deck::Adapter::Chromium:return "Chromium";case deck::Adapter::Electron:return "Electron";case deck::Adapter::Claude:return "Claude";default:return "—";}}
double cardHeightFor(Obj a){return usageEnabled(a)?268:206;}
void makeCard(Obj parent,Obj p,Rect frame,Int ordinal){
 Obj c=card(parent,frame,16);Obj a=appFor(p);double w=frame.size.width,cardHeight=frame.size.height,inner=w-40;
 bool codex=deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Codex,master=isMaster(p),limits=usageEnabled(a);Obj ref=dict();
 // Identity: the app's own icon with the profile's colour as a badge on its corner.
 appIcon(c,get(a,"path"),rect(20,20,40,40));identityBadge(c,rect(20,20,40,40),accentFor(p));
 // Right of the title: the plan and the ⌘-number, centred on the title block.
 double rx=w-20;
 if(ordinal<9){char b[16];snprintf(b,sizeof b,"⌘%ld",ordinal+1);double kw=keycapWidth(b);rx-=kw;keycap(c,b,rect(rx,29,kw,22));rx-=6;}
 if(limits){Obj planPill=planBadge(c,rect(rx-44,30,44,20));Obj planLabel=label(planPill,"",rect(0,3,44,14),10,muted(),.4);send<void>(planLabel,"setAlignment:",alignCenter);
  put(ref,"planPill",planPill);put(ref,"planLabel",planLabel);put(ref,"planRight",real(rx));rx-=50;}
 kern(labelObj(c,get(p,"name"),rect(72,20,rx-72-8,20),15,ink(),.3),-.15);
 Obj dot=statusDot(c,rect(72,47,7,7));Obj st=label(c,T("Not running","Не запущен"),rect(85,42,rx-85-8,16),12,muted(),.23);
 panel(c,rect(20,76,inner,1),hairline(),0);
 // Facts. Codex: ACCOUNT shares the first limit column, SETTINGS and HISTORY split the second; other apps: three equal columns.
 const char* accountText=master?(codex?T("Current Codex","Текущий Codex"):T("Current sign-in","Текущий вход")):T("Separate sign-in","Отдельный вход");Obj accountLabel;
 if(codex){double half=(inner-24)/2,sub=(half-16)/2,x2=20+half+24;
  accountLabel=cardFact(c,T("ACCOUNT","АККАУНТ"),accountText,20,half);
  cardFact(c,T("SETTINGS","НАСТРОЙКИ"),master?T("Original","Оригинал"):truth(get(p,"share"))?T("From base","Из базы"):T("Local","Локальные"),x2,sub);
  cardFact(c,T("HISTORY","ИСТОРИЯ"),master?T("Original","Оригинал"):sharesHistory(p)?T("Shared","Общая"):T("Own","Своя"),x2+sub+16,sub);}
 else{double third=(inner-32)/3;
  accountLabel=cardFact(c,T("ACCOUNT","АККАУНТ"),accountText,20,third);
  cardFact(c,T("DATA","ДАННЫЕ"),master?T("Original","Оригинал"):T("Own folder","Своя папка"),20+third+16,third);
  cardFact(c,T("ADAPTER","АДАПТЕР"),adapterTitle(a),20+(third+16)*2,third);}
 send<void>(accountLabel,"setLineBreakMode:",(Int)5);if(limits)put(ref,"account",accountLabel);
 if(limits)put(ref,"usage",usageStrip(c,20,148,inner,profileIndex(p)));
 // Actions: the main one as a glass capsule, the others as round icon buttons at the card's right edge.
 double by=cardHeight-52,bx=w-20;Int tag=profileIndex(p);
 auto round=[&](const char* icon,const char* action,const char* tip,const char* style){bx-=32;Obj b=iconButton(c,icon,action,rect(bx,by,32,32),tag,tip,style);bx-=8;return b;};
 round("ellipsis","profileMore:",T("More actions","Другие действия"),"secondary");
 if(running(p)){
  round("xmark","stopProfile:",T("Close — send the window a normal quit request (the process is never force-killed)","Закрыть — отправить окну обычный запрос завершения (без принудительного убийства процесса)"),"danger");
  round("arrow.clockwise","restartProfile:",T("Restart — close the window and launch the profile again, for example to pick up new projects and settings","Перезапустить — закрыть окно и сразу запустить профиль заново, например чтобы подтянуть новые проекты и настройки"),"secondary");
  Obj front=styledButton(deckButtonClass,c,T("Show window","Показать окно"),"launchProfile:",rect(20,by,bx-20,32),tag,"prominent",10,13,"macwindow");send<void>(front,"setToolTip:",str(T("Bring this profile's window to the front","Вывести окно этого профиля на передний план")));
 }else{
  Obj folder=round("folder","profileFolder:",T("Folder","Папка"),"secondary");send<void>(folder,"setToolTip:",shortPath(master?masterFolder(a):profileRoot(p)));
  if(get(p,"lastError")){Obj d=round("exclamationmark.triangle","profileError:",T("Why it did not start","Почему не запустился"),"secondary");send<void>(d,"setContentTintColor:",warnColor());}
  styledButton(deckButtonClass,c,T("Launch","Запустить"),"launchProfile:",rect(20,by,bx-20,32),tag,"prominent",10,13,"play.fill");
 }
 put(ref,"status",st);put(ref,"dot",dot);put(ref,"live",boolean(running(p)!=nullptr));put(cardRefs,utf8(get(p,"id")),ref);usageApplyCard(ref,p);
}
const double settingRowH=56;
// One row of a grouped list: a glass icon tile, a title and a description, a trailing button of a fixed width.
Obj settingRow(Obj box,double y,double w,const char* icon,const char* title,Obj description,const char* buttonTitle,const char* action,Int tag,double buttonW,const char* buttonIcon=nullptr,bool code=false){
 iconTile(box,rect(16,y+12,32,32),icon);double textW=w-60-16-buttonW-16;
 Obj t=label(box,title,rect(60,y+10,textW,18),13,ink(),.3);if(code)send<void>(t,"setFont:",codeFont(13,.3));
 Obj d=labelObj(box,description,rect(60,y+29,textW,16),12,muted(),0);send<void>(d,"setLineBreakMode:",(Int)4);
 return button(box,buttonTitle,action,rect(w-16-buttonW,y+12,buttonW,32),tag,false,buttonIcon);
}
void settingDivider(Obj box,double y,double w){panel(box,rect(60,y,w-60-16,1),hairline(),0);}
// The widest of a group's trailing buttons, so every row gets the same width (at least 112).
double groupWidth(std::initializer_list<const char*> titles,bool icon){double bw=112;for(const char* t:titles){double f=fitWidth(t,icon);if(f>bw)bw=f;}return bw;}
// A page header: small caps eyebrow, a large title and an optional subtitle, all on the content's left edge.
void pageHeader(double x,double w,const char* eyebrow,Obj title,Obj subtitle,double titleW=0){
 caption(rootView,eyebrow,rect(x,40,w,14));
 Obj t=labelObj(rootView,title,rect(x,58,titleW>0?titleW:w,32),26,ink(),.4);kern(t,-.52);
 if(subtitle){Obj sub=labelObj(rootView,subtitle,rect(x,96,w,18),13,muted(),0);send<void>(sub,"setLineBreakMode:",(Int)5);}
}
// A right-aligned row of header buttons on the title line; returns the x where the row starts.
struct HeaderButton{const char* title;const char* action;const char* icon;bool primary;const char* tip;};
double headerButtons(double right,std::initializer_list<HeaderButton> list){
 double total=0;for(auto& b:list)total+=fitWidth(b.title,b.icon!=nullptr)+8;double bx=right-total+8,start=bx;
 for(auto& b:list){double bw=fitWidth(b.title,b.icon!=nullptr);Obj v=button(rootView,b.title,b.action,rect(bx,58,bw,32),-1,b.primary,b.icon);if(b.tip)send<void>(v,"setToolTip:",str(b.tip));bx+=bw+8;}
 return start;
}
// Footer line: an optional glyph and a quiet sentence on the content's left edge.
Obj footerNote(double x,double y,double w,const char* glyph,Obj text){
 double tx=x;if(glyph){imageView(rootView,symbol(glyph,12),rect(x,y+8,16,16),faint());tx+=22;}
 Obj l=labelObj(rootView,text,rect(tx,y+8,w-(tx-x),16),12,faint(),0);send<void>(l,"setLineBreakMode:",(Int)4);return l;
}
void buildUI(){
 if(!rootView||building)return;building=true;
 Obj old=send(send(rootView,"subviews"),"copy");for(UInt i=0;i<count(old);++i)send<void>(at(old,i),"removeFromSuperview");drop(old);send<void>(cardRefs,"removeAllObjects");footer=nullptr;usageFooter=nullptr;
 Rect bounds=getRect(rootView,"bounds");double W=bounds.size.width,H=bounds.size.height;
 // Content column: one left edge L and one right edge R for every block of every page.
 const double x=264,w=W-x-32,footY=H-56;
 // Glass: the window is one behind-window blur with a warm graphite veil; the sidebar floats on it as an
 // inset pane with a hairline rim, concentric with the window's corners.
 panel(rootView,rect(0,0,W,H),color(.11,.11,.12,.42),0);
 {Obj pane=panel(rootView,rect(8,8,224,H-16),fill(.055),14);Obj l=send(pane,"layer");send<void>(l,"setBorderWidth:",1.0);send<void>(l,"setBorderColor:",send(fill(.09),"CGColor"));
  send<void>(l,"setShadowColor:",send(color(0,0,0,1),"CGColor"));send<void>(l,"setShadowOpacity:",(float).18);send<void>(l,"setShadowRadius:",12.0);send<void>(l,"setShadowOffset:",Extent{0,-8});
  panel(pane,rect(14,0,196,1),fill(.07),0);}
 kern(label(rootView,"AppDeck",rect(26,56,190,22),17,ink(),.4),-.17);label(rootView,T("One place. Many accounts.","Одно место. Разные аккаунты."),rect(26,78,190,14),11,faint(),0);
 const char* navTitles[]={T("Profiles","Профили"),T("Shared settings","Общие настройки"),T("Projects","Проекты"),T("About","О приложении")};const char* navIcons[]={"person.2","slider.horizontal.3","folder","info.circle"};
 for(Int i=0;i<4;++i){Obj b=navRow(rootView,rect(16,112+i*34,208,32),symbol(navIcons[i],13),true,str(navTitles[i]),"changePage:",i,page==i);if(page==i)send<void>(b,"setAccessibilitySelected:",true);}
 caption(rootView,T("APPS","ПРИЛОЖЕНИЯ"),rect(26,264,190,14));
 double listTop=284,listH=count(apps)*34.0,listMax=H-listTop-150;if(listMax<68)listMax=68;if(listH>listMax)listH=listMax;
 Obj sideList=scrollDocument(rootView,rect(16,listTop,208,listH),count(apps)*34.0);
 for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);Int n=0;for(UInt j=0;j<count(profiles);++j)if(same(get(at(profiles,j),"appId"),get(a,"id")))++n;
  navRow(sideList,rect(0,i*34.0,208,32),send(workspace,"iconForFile:",get(a,"path")),false,get(a,"name"),"selectApp:",(Int)i,same(get(a,"id"),selected),formatInt("%ld",n));}
 navRow(rootView,rect(16,listTop+listH,208,32),symbol("plus.circle",13),true,str(T("Add app","Добавить приложение")),"chooseApp:",-1,false);
 {Obj sp=card(rootView,rect(16,H-82,208,36),10);imageView(sp,symbol("sidebar.right",13),rect(10,10,16,16),muted());
  const char* key="⌃⌥Space";double kw=(double)(Int)(textWidth(key,10.5,.3)+9);keycap(sp,key,rect(208-8-kw,8,kw,20),10.5);
  label(sp,T("Side panel","Правая панель"),rect(36,9,208-36-8-kw-2,18),13,ink(),.23);
  Obj edgeToggle=styledButton(deckButtonClass,sp,"","toggleEdge:",rect(0,0,208,36),-1,"overlay",10);send<void>(edgeToggle,"setAccessibilityLabel:",str(T("Side panel","Правая панель")));
  send<void>(edgeToggle,"setToolTip:",str(T("A slim panel at the right edge of the screen: quick launch and account switching","Узкая панель у правого края экрана: быстрый запуск и переключение аккаунтов")));}
 labelObj(rootView,cat(str("AppDeck "),str(VERSION)),rect(26,H-32,190,14),11,faint(),0);
 Obj a=currentApp();bool codexGroup=a&&deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Codex;
 if(page==0){
  Obj visible=visibleProfiles();double titleW=w;
  if(a)titleW=headerButtons(x+w,{{T("App location…","Путь приложения…"),"changePath:","folder",false,nullptr},
   {T("2×2 grid","Сетка 2×2"),"tileWindows:","square.grid.2x2",false,T("Arrange up to four running windows of this group. Requires Accessibility access.","Разложить до четырёх запущенных окон этой группы. Нужен Универсальный доступ.")},
   {T("Launch all","Запустить все"),"launchAll:","play",false,nullptr},{T("New profile","Новый профиль"),"newProfile:","plus",true,nullptr}})-x-16;
  pageHeader(x,w,T("APP PROFILES","ПРОФИЛИ ПРИЛОЖЕНИЯ"),a?get(a,"name"):str(T("Your apps, side by side","Ваши приложения, рядом")),a?nullptr:str(T("Choose Codex (ChatGPT.app), Claude or another supported app and create profiles.","Выберите Codex (ChatGPT.app), Claude или другое поддерживаемое приложение и создайте профили.")),titleW);
  if(a){const char* pre=T("Original","Оригинал");double pw=textWidth(pre,13,0);label(rootView,pre,rect(x,96,pw+8,18),13,muted(),0);label(rootView,"·",rect(x+pw+6,96,10,18),13,dim(),0);
   Obj path=labelObj(rootView,shortPath(get(a,"path")),rect(x+pw+18,97,w-pw-18,17),12,muted(),0);send<void>(path,"setFont:",codeFont(12,0));send<void>(path,"setLineBreakMode:",(Int)5);}
  if(count(visible)){
   double gap=16,cw=(w-gap)/2.0,cardHeight=cardHeightFor(a);Int columns=2;if(cw<330){columns=1;cw=w;}
   UInt rows=(count(visible)+columns-1)/columns;
   Obj doc=scrollDocument(rootView,rect(x,138,w,footY-16-138),rows*(cardHeight+gap)-gap);
   for(UInt i=0;i<count(visible);++i)makeCard(doc,at(visible,i),rect((i%columns)*(cw+gap),(i/columns)*(cardHeight+gap),cw,cardHeight),(Int)i);
  }else{
   // First run (no app) or an empty group: one calm card with the next step.
   Obj box=card(rootView,rect(x,138,w,260),16);
   if(a)appIcon(box,get(a,"path"),rect(32,32,64,64));else imageView(box,send(app,"applicationIconImage"),rect(32,32,64,64));
   kern(label(box,T("Many accounts. One workspace.","Несколько аккаунтов. Одна рабочая среда."),rect(32,112,w-64,26),20,ink(),.4),-.3);
   label(box,a?T("This group has no profiles yet. Add the first one — you sign in inside the app itself.","В этой группе пока нет профилей. Добавьте первый — вход выполняется уже внутри приложения."):T("Choose Codex (ChatGPT.app), Claude or another supported app and create profiles.","Выберите Codex (ChatGPT.app), Claude или другое поддерживаемое приложение и создайте профили."),rect(32,146,w-64,18),13,muted(),0);
   label(box,T("Every profile is an ordinary macOS window with its own sign-in. The original .app is neither copied nor modified.","Каждый профиль — обычное окно macOS со своим входом. Исходное .app не копируется и не изменяется."),rect(32,168,w-64,16),12,faint(),0);
   const char* next=a?T("Create profile","Создать профиль"):T("Choose app…","Выбрать приложение…");button(box,next,a?"newProfile:":"chooseApp:",rect(32,204,fitWidth(next,true),32),-1,true,"plus");
  }
  if(count(visible)){
   bool limitsOn=usageEnabled(a),capable=usageCapable(a);double reserved=!capable?0:limitsOn?fitWidth(T("Limits","Лимиты"),true)+12+180+12:fitWidth(T("Account limits…","Лимиты аккаунтов…"),true)+12;
   footer=label(rootView,"",rect(x,footY+8,w-reserved,16),12,faint(),0);send<void>(footer,"setLineBreakMode:",(Int)4);
   if(capable&&limitsOn){const char* t=T("Limits","Лимиты");double bw=fitWidth(t,true);Obj again=button(rootView,t,"usageRefreshAll:",rect(x+w-bw,footY,bw,32),-1,false,"arrow.clockwise");send<void>(again,"setToolTip:",str(T("Check the limits of all profiles now. On its own, AppDeck asks each account at most once an hour, with 5 minutes between profiles.","Проверить лимиты всех профилей сейчас. Сам AppDeck спрашивает каждый аккаунт не чаще раза в час, с паузой 5 минут между профилями.")));
    usageFooter=label(rootView,"",rect(x+w-bw-12-180,footY+8,180,16),12,faint(),0);send<void>(usageFooter,"setAlignment:",alignRight);}
   else if(capable){const char* t=T("Account limits…","Лимиты аккаунтов…");double bw=fitWidth(t,true);Obj enable=button(rootView,t,"toggleUsage:",rect(x+w-bw,footY,bw,32),-1,false,"gauge.with.dots.needle.33percent");send<void>(enable,"setToolTip:",str(T("Show on the cards and in the side panel how much of the weekly limit each account has left","Показывать на карточках и в правой панели, сколько недельного лимита осталось у каждого аккаунта")));}
  }
 }else if(page==1){
  bool codexSettings=codexGroup&&!usageIsClaude(a);
  const char* eyebrow=T("SHARED WORKSPACE","ОБЩАЯ РАБОЧАЯ СРЕДА");Obj title=str(T("One workspace, separate sign-ins","Одна среда, разные входы"));
  if(usageIsClaude(a)){bool limitsOn=usageEnabled(a);
   pageHeader(x,w,eyebrow,title,str(T("Claude Desktop: each profile has its own data folder and sign-in; the settings and history of the Code tab (~/.claude) are shared.","Claude Desktop: у каждого профиля своя папка данных и свой вход; настройки и история вкладки Code (~/.claude) общие.")));
   caption(rootView,T("ACCOUNT LIMITS","ЛИМИТЫ АККАУНТОВ"),rect(x,138,w,14));
   Obj box=card(rootView,rect(x,160,w,settingRowH),16);
   double bw=groupWidth({T("Turn off","Отключить"),T("Turn on…","Включить…")},false);
   Obj t=settingRow(box,0,w,"gauge.with.dots.needle.33percent",T("Account limits","Лимиты аккаунтов"),str(limitsOn?T("On · 5 hours, week and weekly per-model limits (for example Fable)","Включено · 5 часов, неделя и недельные лимиты по моделям (например, Fable)"):!usageBinary(a)?T("Claude Code (claude) not found — limits need it","Не найден Claude Code (claude) — без него лимиты недоступны"):T("Off · AppDeck does not run Claude Code","Выключено · AppDeck не запускает Claude Code")),limitsOn?T("Turn off","Отключить"):T("Turn on…","Включить…"),"toggleUsage:",-1,bw);
   if(!limitsOn&&!usageBinary(a))disable(t);
   lines(rootView,T("Claude Desktop hands its sign-in to the embedded Claude Code in memory, so there is nothing to ask for limits “on behalf of the window”. For each profile AppDeck keeps a separate Claude Code folder (in AppDeck's data, not ~/.claude); you sign in to it once with the same account using the standard claude auth login.\nAnthropic counts limits per account, so the numbers match the Claude window. Claude Code keeps the tokens; AppDeck never sees them and makes no network requests itself.","Claude Desktop передаёт свой вход встроенному Claude Code в памяти, поэтому спросить лимиты «от имени окна» нечем. Для каждого профиля AppDeck держит отдельную служебную папку Claude Code (в данных AppDeck, не ~/.claude); вы один раз входите в неё тем же аккаунтом через штатный claude auth login.\nЛимиты у Anthropic считаются на аккаунт, поэтому цифры совпадают с окном Claude. Токены хранит Claude Code; AppDeck их не видит и в сеть сам не ходит."),rect(x,236,w,110),12,muted(),18);
   footerNote(x,footY,w,"lock.shield",str(T("Each account’s sign-in and data stay separate.","Вход и данные каждого аккаунта остаются раздельными.")));}
  else if(!codexSettings){pageHeader(x,w,eyebrow,title,nullptr);
   Obj box=card(rootView,rect(x,138,w,72),16);iconTile(box,rect(16,20,32,32),"hand.point.left");
   label(box,T("Choose a Codex or Claude group on the left. Shared settings are available for Codex, limits for both.","Выберите группу Codex или Claude слева. Общие настройки доступны для Codex, лимиты — для обеих."),rect(60,27,w-76,18),13,ink(),.23);}
  else{
   bool based=baseSource(a)!=nullptr,history=based&&truth(get(a,"sharedHistory"))&&historyAvailable(a);bool limitsOn=usageEnabled(a);
   double titleW=headerButtons(x+w,{{T("Folder","Папка"),"openSharedFolder:","folder",false,nullptr},{based?T("Base workspace…","Базовая среда…"):T("Connect Codex…","Подключить Codex…"),"connectBase:","link",!based,nullptr}})-x-16;
   pageHeader(x,w,eyebrow,title,based?cat(str(T("Source: ","Источник: ")),shortPath(baseSource(a))):str(T("No source connected — this group uses AppDeck’s shared folder","Источник не подключён — используется общая папка AppDeck этой группы")),titleW);
   caption(rootView,T("FILES FROM THE BASE WORKSPACE","ФАЙЛЫ ИЗ БАЗОВОЙ СРЕДЫ"),rect(x,138,w,14));
   Obj files=card(rootView,rect(x,160,w,settingRowH*4),16);
   const char* titles[]={"config.toml","AGENTS.md","skills/","rules/"};const char* icons[]={"gearshape","doc.text","sparkles","checkmark.shield"};
   const char* descriptions[]={T("Copied before launch · model, MCP and agent settings","Копируется перед запуском · модель, MCP и настройки агента"),T("Copied before launch · instructions for all profiles","Копируется перед запуском · инструкции для всех профилей"),T("Shared folder via link · changes are visible to all profiles","Общая папка по ссылке · изменения видны всем профилям"),T("Shared folder via link · command execution rules","Общая папка по ссылке · правила выполнения команд")};
   double openW=groupWidth({T("Open","Открыть")},true);
   for(Int i=0;i<4;++i){if(i)settingDivider(files,i*settingRowH,w);Obj b=settingRow(files,i*settingRowH,w,icons[i],titles[i],str(descriptions[i]),T("Open","Открыть"),"openSharedFile:",i,openW,"arrow.up.forward.square",true);send<void>(b,"setAccessibilityLabel:",cat(str(T("Open ","Открыть ")),str(titles[i])));}
   caption(rootView,T("SHARING","ОБЩИЙ ДОСТУП"),rect(x,160+settingRowH*4+24,w,14));
   double sy=160+settingRowH*4+46;Obj sharing=card(rootView,rect(x,sy,w,settingRowH*2),16);
   double bw=groupWidth({T("Turn off…","Отключить…"),T("Turn on…","Включить…"),T("Turn off","Отключить")},false);
   Obj hb=settingRow(sharing,0,w,"arrow.triangle.2.circlepath",T("Shared workspace","Общая среда"),str(!based?T("Unavailable until the current Codex is connected","Недоступно, пока не подключён текущий Codex"):history?T("History, projects and automations · changes from every profile","История, проекты и автоматизации · изменения из всех профилей"):historyAvailable(a)?T("Off · each copy keeps its own history","Выключено · у каждой копии своя история"):T("The source has no state_*.sqlite thread database","В источнике нет базы тредов state_*.sqlite")),history?T("Turn off…","Отключить…"):T("Turn on…","Включить…"),"toggleHistory:",-1,bw);
   if(!based||(!history&&!historyAvailable(a)))disable(hb);
   settingDivider(sharing,settingRowH,w);
   Obj lb=settingRow(sharing,settingRowH,w,"gauge.with.dots.needle.33percent",T("Account limits","Лимиты аккаунтов"),str(limitsOn?T("On · weekly limit left on the cards and in the side panel","Включено · остаток недельного лимита на карточках и в правой панели"):!usageBinary(a)?T("This app has no codex helper — limits unavailable","В этом приложении нет служебного codex — лимиты недоступны"):T("Off · AppDeck does not run the Codex helper process","Выключено · AppDeck не запускает служебный процесс Codex")),limitsOn?T("Turn off","Отключить"):T("Turn on…","Включить…"),"toggleUsage:",-1,bw);
   if(!limitsOn&&!usageBinary(a))disable(lb);
   lines(rootView,T("Projects: add, rename and remove in any profile → one shared list.\nAutomations: one shared catalogue, a single assigned owner; the other copies are paused.\nconfig.toml and AGENTS.md still come from the base workspace. Sign-in and cookies are never transferred.","Проекты: добавление, переименование и удаление из любого профиля → общий список.\nАвтоматизации: общий каталог, один назначенный исполнитель; остальные копии приостановлены.\nconfig.toml и AGENTS.md по-прежнему поступают из базовой среды. Вход и cookies не переносятся."),rect(x,sy+settingRowH*2+20,w,56),12,muted(),18);
   // Footer: the sync state on the left, the two maintenance actions at the right edge.
   double right=x+w;struct Act{const char* title;const char* action;const char* icon;};Act acts[]={{T("Automations…","Автоматизации…"),"automationOwner:","clock.arrow.circlepath"},{T("Sync","Синхронизировать"),"syncBaseProjects:","arrow.triangle.2.circlepath"}};
   for(auto& act:acts){double abw=fitWidth(act.title,true);right-=abw;button(rootView,act.title,act.action,rect(right,footY,abw,32),-1,false,act.icon);right-=8;}
   footerNote(x,footY,right-x-8,"lock.shield",history?(get(a,"syncStatus")?get(a,"syncStatus"):str(T("Reconciled before launch; running windows update after a restart","Согласование перед запуском; работающие окна обновятся после перезапуска"))):str(T("Each account’s sign-in and data stay separate.","Вход и данные каждого аккаунта остаются раздельными.")));
  }
 }else if(page==2){
  double titleW=headerButtons(x+w,{{T("Add folder","Добавить папку"),"addProject:","plus",true,nullptr}})-x-16;
  pageHeader(x,w,T("ONE LIST OF FOLDERS","ЕДИНЫЙ СПИСОК ПАПОК"),str(T("Projects","Проекты")),str(T("Shared projects are reconciled between profiles before launch. Removing one from the list keeps its folders and tasks.","Общие проекты согласуются между профилями перед запуском. Удаление из списка сохраняет папки и задачи.")));(void)titleW;
  if(!count(projects)){Obj c=card(rootView,rect(x,138,w,104),16);iconTile(c,rect(20,24,40,40),"folder.badge.plus");
   label(c,T("One repository — several profiles","Один репозиторий — несколько профилей"),rect(76,28,w-96,20),15,ink(),.3);label(c,T("Keep your working folders here to open them or copy their path quickly.","Сохраните рабочие папки здесь, чтобы быстро открыть их или скопировать путь."),rect(76,52,w-96,18),13,muted(),0);}
  else{const double rowH=60;double listH=count(projects)*rowH;Obj doc=scrollDocument(rootView,rect(x,138,w,footY-16-138),listH);Obj box=card(doc,rect(0,0,w,listH),16);
   const char* copyTitle=T("Copy path","Скопировать путь");double finderW=fitWidth("Finder",true),copyW=fitWidth(copyTitle,true);
   for(UInt i=0;i<count(projects);++i){Obj p=at(projects,i);double y=i*rowH;bool present=exists(get(p,"path"));if(i)settingDivider(box,y,w);
    iconTile(box,rect(16,y+14,32,32),present?"folder":"questionmark.folder",present?nullptr:warnColor());
    double bx=w-16-32,textW=bx-8-copyW-8-finderW-16-60;
    labelObj(box,get(p,"name"),rect(60,y+12,textW,18),13,ink(),.3);
    Obj path=labelObj(box,present?shortPath(get(p,"path")):cat(str(T("Folder not found · ","Папка не найдена · ")),shortPath(get(p,"path"))),rect(60,y+31,textW,16),12,present?faint():warnColor(),0);send<void>(path,"setFont:",codeFont(12,0));send<void>(path,"setLineBreakMode:",(Int)5);
    iconButton(box,"trash","projectAction:",rect(bx,y+14,32,32),(Int)i*3+2,T("Remove","Убрать"));
    bx-=8+copyW;button(box,copyTitle,"projectAction:",rect(bx,y+14,copyW,32),(Int)i*3+1,false,"doc.on.doc");
    bx-=8+finderW;Obj finder=button(box,"Finder","projectAction:",rect(bx,y+14,finderW,32),(Int)i*3,false,"folder");if(!present)disable(finder);
   }}
  footerNote(x,footY,w,"arrow.triangle.branch",str(T("To change code from several accounts at once, use separate Git worktrees.","Для одновременного изменения кода из разных аккаунтов используйте отдельные Git worktree.")));
 }else{
  pageHeader(x,w,"APPDECK · MACOS",str(T("Native profile manager","Нативный менеджер профилей")),str(T("No Electron, no WebView, no telemetry, no server of its own.","Без Electron, WebView, телеметрии и собственного сервера.")));
  Obj box=card(rootView,rect(x,138,w,152),16);imageView(box,send(app,"applicationIconImage"),rect(24,24,80,80));
  kern(label(box,"AppDeck",rect(124,24,w-148,26),20,ink(),.4),-.3);
  {Obj vs=cat(str(VERSION),str(" · arm64 + x86_64 · macOS 13+"));Obj font=codeFont(11,.23);double vw=measure(utf8(vs),font)+22;
   Obj version=panel(box,rect(124,56,vw,22),fill(.07),11);Obj vl=send(version,"layer");send<void>(vl,"setBorderWidth:",1.0);send<void>(vl,"setBorderColor:",send(fill(.10),"CGColor"));
   Obj t=labelObj(version,vs,rect(0,3,vw,15),11,muted(),0);send<void>(t,"setFont:",font);send<void>(t,"setAlignment:",alignCenter);}
  lines(box,T("Local ad-hoc build: no Developer ID and no Apple notarization.\nProfile compatibility depends on the version of the launched app.\nAfter a rebuild macOS may ask for Accessibility access again.","Локальная ad-hoc сборка: без Developer ID и нотариального заверения Apple.\nСовместимость профилей зависит от версии запускаемого приложения.\nПосле пересборки macOS может заново запросить Универсальный доступ."),rect(124,88,w-148,48),12,faint(),16);
  caption(rootView,T("KEYBOARD","КЛАВИАТУРА"),rect(x,314,w,14));
  Obj keys=card(rootView,rect(x,336,w,144),16);
  struct Key{const char* cap;const char* text;};
  Key rows[]={{"⌃⌥Space",T("Show or hide the side panel","Показать или скрыть правую панель")},{"⌃⌥1 … 8",T("Profiles in side panel order, from any app","Профили по порядку правой панели, из любого приложения")},{"⌘1 … 9",T("Profiles of the selected app while AppDeck is active","Профили выбранного приложения, когда AppDeck активен")}};
  for(int i=0;i<3;++i){double y=i*48;if(i)panel(keys,rect(16,y,w-32,1),hairline(),0);keycap(keys,rows[i].cap,rect(16,y+13,keycapWidth(rows[i].cap),22));label(keys,rows[i].text,rect(128,y+15,w-144,18),13,ink(),0);}
  lines(rootView,T("Change the order by dragging a row in the side panel or with “•••” → “Move up / down” on a card. The panel never records the screen.","Порядок меняется перетаскиванием строки в правой панели или через «•••» → «Переместить выше / ниже» на карточке. Панель не записывает экран."),rect(x,500,w,36),12,muted(),18);
  imageView(rootView,symbol("lock.shield",12),rect(x,545,16,16),muted());label(rootView,T("This separates data; it is not a security sandbox.","Это разделение данных, а не песочница безопасности."),rect(x+22,544,w-22,18),12,muted(),0);
  double bx=x;struct Act{const char* title;const char* action;const char* icon;};
  Act acts[]={{T("User guide","Инструкция (EN)"),"showHelp:","book"},{T("Diagnostics…","Диагностика…"),"diagnostics:","stethoscope"},{T("Data folder","Папка данных"),"openData:","folder"}};
  for(auto& act:acts){double bw=fitWidth(act.title,true);button(rootView,act.title,act.action,rect(bx,footY,bw,32),-1,false,act.icon);bx+=bw+8;}
  if(a){const char* rt=T("Remove selected app…","Убрать выбранное приложение…");double bw=fitWidth(rt,true);styledButton(deckButtonClass,rootView,rt,"removeApp:",rect(x+w-bw,footY,bw,32),-1,"danger",10,13,"trash");}
 }
 // No control starts focused: a focused borderless button would draw its glyph in the accent colour.
 if(window)send<bool>(window,"makeFirstResponder:",(Obj)nullptr);
 building=false;invalidateEdge();refresh();
}
#include "edge_panel.hpp"
#include "edge_selftest.hpp"
void invalidateEdge(){edgeRebuild=true;}
void refresh(){
 if(building||!profiles||!cardRefs)return;Obj ps=visibleProfiles();Int live=0;bool menuChanged=false,flagged=false;
 for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);Obj r=running(p);bool active=r!=nullptr;
  double due=send<double>(get(p,"launchCheck"),"doubleValue");
  if(due>0){double now=send<double>(send(cls("NSDate"),"date"),"timeIntervalSince1970");if(now>=due){erase(p,"launchCheck");if(!active){put(p,"lastError",str(T("The process exited right after starting. Possible causes: an incompatible adapter, a single-instance lock or an error in the app itself. Check the window and the diagnostics.","Процесс завершился сразу после старта. Возможны несовместимый адаптер, блокировка второго экземпляра или ошибка самого приложения. Проверьте окно и диагностику.")));note("Process exited within launch watchdog interval; isolation not confirmed.");flagged=true;}save();}}
  Obj ref=get(cardRefs,utf8(get(p,"id")));
  if(ref){Obj status=get(ref,"status");bool failed=get(p,"lastError")!=nullptr;
   send<void>(status,"setStringValue:",restarting(p)?str(T("Restarting…","Перезапускается…")):active?str(truth(get(p,"syncPending"))?T("Running · updates pending","Запущен · есть обновления"):T("Running","Запущен")):failed?str(T("Failed to start","Не запустился")):str(T("Not running","Не запущен")));
   send<void>(status,"setTextColor:",restarting(p)||failed?warnColor():muted());
   if(Obj dot=get(ref,"dot"))statusDotColor(dot,restarting(p)||failed?warnColor():active?liveColor():dim(),active,!active&&!restarting(p)&&!failed);send<void>(status,"setToolTip:",truth(get(p,"syncPending"))?str(T("The shared list and automations apply after this instance fully restarts.","Общий список и автоматизации применятся после полного перезапуска этого экземпляра.")):get(p,"lastError"));
   if(truth(get(ref,"live"))!=active)flagged=true; // the button row differs between the two states
   usageApplyCard(ref,p);}
  if(active!=truth(get(p,"uiRunning"))){put(p,"uiRunning",num(active));menuChanged=true;}
 }
 for(UInt i=0;i<count(ps);++i)if(running(at(ps,i)))++live;
 if(footer){char b[220];snprintf(b,sizeof b,T("%ld of %ld running  ·  ⌘1–9 to switch  ·  Check the sign-in inside the app itself","%ld из %ld запущено  ·  ⌘1–9 для переключения  ·  Вход проверяйте в самом приложении"),live,(Int)count(ps));send<void>(footer,"setStringValue:",str(b));}
 if(usageFooter){Obj line=str("");double newest=0;
  for(UInt i=0;i<count(ps);++i){Obj p=at(ps,i);if(usageAsking(p))line=cat(cat(str(T("checking “","проверяю «")),get(p,"name")),str(T("”…","»…")));double c=send<double>(get(usageOf(p),"checked"),"doubleValue");if(c>newest)newest=c;}
  if(!send<UInt>(line,"length")&&newest>0)line=cat(str(T("limits: ","лимиты: ")),usageAgeText(newest));send<void>(usageFooter,"setStringValue:",line);}
 if(menuChanged)buildMenus();edgeRefresh();
 if(flagged)buildUI(); // shows the "Подробнее" button on the affected card
}
void buildMenus(){
 invalidateEdge();Obj bar=make("NSMenu");
 Obj top=menuItem(bar,"AppDeck",nullptr);Obj m=make("NSMenu");send<void>(top,"setSubmenu:",m);
 menuItem(m,T("Show AppDeck","Показать AppDeck"),"showWindow:");menuItem(m,T("Side panel  ⌃⌥Space","Правая панель  ⌃⌥Space"),"toggleEdge:");menuItem(m,T("About","О приложении"),"changePage:",3);menuItem(m,T("User guide","Инструкция (EN)"),"showHelp:");separator(m);
 Obj quit=menuItem(m,T("Quit AppDeck","Завершить AppDeck"),"terminate:",-1,"q");send<void>(quit,"setTarget:",app);drop(m);
 top=menuItem(bar,T("File","Файл"),nullptr);m=make("NSMenu");send<void>(top,"setSubmenu:",m);
 menuItem(m,T("New Profile…","Новый профиль…"),"newProfile:",-1,"n");menuItem(m,T("Add App…","Добавить приложение…"),"chooseApp:");menuItem(m,T("Add Project…","Добавить проект…"),"addProject:");separator(m);menuItem(m,T("Diagnostics…","Диагностика…"),"diagnostics:");
 Obj closeItem=menuItem(m,T("Close Window","Закрыть окно"),"performClose:",-1,"w");send<void>(closeItem,"setTarget:",window);drop(m);
 top=menuItem(bar,T("Edit","Правка"),nullptr);m=make("NSMenu");send<void>(top,"setSubmenu:",m);
 struct EditItem{const char* title;const char* action;const char* key;};
 EditItem edit[]={{T("Undo","Отменить"),"undo:","z"},{T("Cut","Вырезать"),"cut:","x"},{T("Copy","Копировать"),"copy:","c"},{T("Paste","Вставить"),"paste:","v"},{T("Select All","Выделить всё"),"selectAll:","a"}};
 for(auto e:edit){Obj it=menuItem(m,e.title,e.action,-1,e.key);send<void>(it,"setTarget:",(Obj)nullptr);}drop(m);
 top=menuItem(bar,T("Profiles","Профили"),nullptr);m=make("NSMenu");send<void>(top,"setSubmenu:",m);
 Obj ps=visibleProfiles();for(UInt i=0;i<count(ps);++i){Obj p=at(ps,i);char key[8]={0};if(i<9)snprintf(key,sizeof key,"%lu",i+1);menuItem(m,utf8(get(p,"name")),"launchProfile:",profileIndex(p),key);}
 separator(m);menuItem(m,T("Launch all","Запустить все"),"launchAll:");menuItem(m,T("Arrange 2×2","Разложить 2×2"),"tileWindows:");menuItem(m,T("Restore Layout","Вернуть расположение"),"restoreWindows:");menuItem(m,T("Hide All Profiles","Скрыть все профили"),"hideProfiles:");drop(m);
 send<void>(app,"setMainMenu:",bar);drop(bar);
 if(statusItem){Obj status=make("NSMenu");send<void>(status,"setAutoenablesItems:",false);menuItem(status,T("Side panel  ⌃⌥Space","Правая панель  ⌃⌥Space"),"toggleEdge:");menuItem(status,T("Show AppDeck","Показать AppDeck"),"showWindow:");menuItem(status,T("Arrange 2×2","Разложить 2×2"),"tileWindows:");menuItem(status,T("Restore Layout","Вернуть расположение"),"restoreWindows:");menuItem(status,T("Hide Profiles","Скрыть профили"),"hideProfiles:");separator(status);
  for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);Obj title=cat(cat(cat(running(p)?str("● "):str("○ "),get(appFor(p),"name")),str(" · ")),get(p,"name"));menuItem(status,utf8(title),"launchProfile:",profileIndex(p));}
  separator(status);menuItem(status,T("User guide","Инструкция (EN)"),"showHelp:");Obj q=menuItem(status,T("Quit AppDeck","Завершить AppDeck"),"terminate:");send<void>(q,"setTarget:",app);send<void>(statusItem,"setMenu:",status);drop(status);
 }
}
bool flipped(Obj,Sel){return true;}
void tick(Obj,Sel,Obj){Pool pool;if(adoptRunning())save();restartPump();groupSyncPump(false);usagePump();refresh();}
void resizeWindow(Obj,Sel,Obj){buildUI();}
void finishedLaunching(Obj,Sel,Obj){installHotkeys();showWindowAction(nullptr,nullptr,nullptr);
 for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);if(!usageCapable(a)||get(a,"usageLimits")||!usageBinary(a))continue;
  bool any=false;for(UInt j=0;j<count(profiles);++j)if(appFor(at(profiles,j))==a){any=true;break;}
  if(any){if(askUsage(a)){for(UInt j=0;j<count(profiles);++j)if(appFor(at(profiles,j))==a)usageForce(at(profiles,j));}buildUI();}break;}
 if(translocated()){note("Running translocated (quarantine).");showError(str(T("AppDeck is running from a quarantined copy","AppDeck запущен из карантинной копии")),str(T("macOS runs this app from a temporary folder (App Translocation) because it was downloaded and never moved in Finder. In this mode the Accessibility permission is not kept and the path changes on every launch.\n\nQuit AppDeck, drag AppDeck.app to Applications (or another folder) in Finder and open it again.","macOS запускает это приложение из временной папки (App Translocation), потому что оно скачано и ни разу не перемещалось через Finder. В таком режиме разрешение «Универсальный доступ» не сохраняется, а путь меняется при каждом запуске.\n\nЗавершите AppDeck, перетащите AppDeck.app в «Программы» (или другую папку) в Finder и откройте снова.")));}
 if(selfTestWanted())selfTestStart(); // acceptance aid, sandbox data only
}
bool reopenApp(Obj,Sel,Obj,bool){showWindowAction(nullptr,nullptr,nullptr);return true;}
bool keepRunning(Obj,Sel,Obj){return false;}
Int shouldTerminate(Obj,Sel,Obj){Int n=0;for(UInt i=0;i<count(profiles);++i)if(running(at(profiles,i)))++n;
 if(n&&!confirm(T("Quit AppDeck?","Закрыть AppDeck?"),T("Running apps keep running. The manager can reconnect to the same processes later.","Запущенные приложения продолжат работать. Позже менеджер сможет повторно подключиться к тем же процессам."),T("Quit manager","Закрыть менеджер")))return 0;
 quitting=true;disposeEdge();save();return 1;
}
void usageRefreshViews(){refresh();}
void usageForce(Obj p){if(!usageForced)usageForced=keep(send(cls("NSMutableSet"),"set"));if(p&&usageEnabled(appFor(p)))send<void>(usageForced,"addObject:",get(p,"id"));}
// One-time sign-in of a profile's Claude Code meter: the CLI's own interactive login, in Terminal.
void claudeMeterLogin(Obj p){
 Obj binary=claudeBinary(),meter=claudeMeterDir(p);if(!binary||!meter){showError(str(T("Sign-in unavailable","Вход недоступен")),str(T("Claude Code (claude) was not found, or AppDeck's data folder is not accessible.","Не найден Claude Code (claude) или нет доступа к папке данных AppDeck.")));return;}
 if(strchr(utf8(binary),'\'')||strchr(utf8(meter),'\'')){showError(str(T("Unsupported path","Неподдерживаемый путь")),str(T("The path to claude or to the AppDeck folder contains an apostrophe.","В пути к claude или к папке AppDeck есть апостроф.")));return;}
 Obj name=replace(replace(get(p,"name"),"'"," "),"\\"," ");
 Obj script=cat(cat(cat(cat(cat(cat(str(T("#!/bin/bash\n# Created by AppDeck: a Claude Code sign-in used only to show the limits of this profile.\nexport CLAUDE_CONFIG_DIR='","#!/bin/bash\n# Создано AppDeck: вход Claude Code только для показа лимитов этого профиля.\nexport CLAUDE_CONFIG_DIR='")),canonical(meter)),str(T("'\nunset ANTHROPIC_API_KEY ANTHROPIC_AUTH_TOKEN CLAUDE_CODE_OAUTH_TOKEN\nclear\necho 'AppDeck · Claude limits · profile: ","'\nunset ANTHROPIC_API_KEY ANTHROPIC_AUTH_TOKEN CLAUDE_CODE_OAUTH_TOKEN\nclear\necho 'AppDeck · лимиты Claude · профиль: "))),name),str(T("'\necho 'In the browser choose THE SAME account as in the window of this profile.'\necho 'If the browser offers another account, sign out of it on claude.ai or open the link in a private window.'\necho\n'","'\necho 'В браузере выберите ТОТ ЖЕ аккаунт, что и в окне этого профиля.'\necho 'Если браузер подставляет другой аккаунт — выйдите из него на claude.ai или откройте ссылку в приватном окне.'\necho\n'"))),binary),str(T("' auth login --claudeai\necho\necho 'Done. Return to AppDeck and click the limits line of this profile. You can close this window.'\n","' auth login --claudeai\necho\necho 'Готово. Вернитесь в AppDeck и нажмите на строку лимитов этого профиля. Окно можно закрыть.'\n")));
 Obj path=join(meter,"AppDeck-login.command");if(!writeText(path,script)||chmod(utf8(path),0700)!=0){showError(str(T("Could not prepare the sign-in","Не удалось подготовить вход")),str(T("Check the permissions of the AppDeck data folder.","Проверьте права на папку данных AppDeck.")));return;}
 // APPDECK_NO_TERMINAL=1 (acceptance runs): prepare the script but do not start the interactive login.
 const char* dry=getenv("APPDECK_NO_TERMINAL");if(dry&&*dry=='1'){note("Claude meter sign-in script written; Terminal not opened (APPDECK_NO_TERMINAL).");return;}
 if(!send<bool>(workspace,"openURL:",url(path)))showError(str(T("Terminal did not open","Терминал не открылся")),cat(str(T("Run it manually: ","Запустите вручную: ")),path));
 note("Claude meter sign-in opened in Terminal (claude auth login); AppDeck handles no credentials.");
}
void usageRefreshAction(Obj,Sel,Obj sender){
 Obj p=fromSender(sender);if(!p)return;
 if(usageIsClaude(appFor(p))&&usageEnabled(appFor(p))&&same(get(usageOf(p),"error"),str("auth"))&&!previewMode){frontForModal();
  Obj alert=make("NSAlert");send<void>(alert,"setMessageText:",cat(str(T("Claude limits — ","Лимиты Claude — ")),get(p,"name")));
  send<void>(alert,"setInformativeText:",str(T("To show limits, this profile's Claude Code helper must be signed in with the same account as the profile's window. AppDeck opens Terminal with the standard claude auth login command; after signing in, click “Check again”.","Для показа лимитов служебный Claude Code этого профиля должен быть авторизован тем же аккаунтом, что и окно профиля. AppDeck откроет Терминал со штатной командой claude auth login; после входа нажмите «Проверить снова».")));
  send(alert,"addButtonWithTitle:",str(T("Sign in…","Войти…")));send(alert,"addButtonWithTitle:",str(T("Check again","Проверить снова")));send(alert,"addButtonWithTitle:",str(T("Cancel","Отмена")));send<void>(at(send(alert,"buttons"),2),"setKeyEquivalent:",str("\033"));
  Int choice=send<Int>(alert,"runModal");drop(alert);if(choice==1000){claudeMeterLogin(p);return;}if(choice!=1001)return;}
 usageForce(p);usagePump();refresh();
}
void usageRefreshAllAction(Obj,Sel,Obj){Obj ps=dockProfiles();for(UInt i=0;i<count(ps);++i)usageForce(at(ps,i));usagePump();refresh();}
// Explicit, per group. The text states exactly what runs and what is kept.
bool askUsage(Obj a){
 if(!a||previewMode)return false;frontForModal();Obj alert=make("NSAlert");send<void>(alert,"setMessageText:",cat(str(T("Show account limits — ","Показывать лимиты аккаунтов — ")),get(a,"name")));
 if(usageIsClaude(a))send<void>(alert,"setInformativeText:",str(T("From time to time AppDeck will run Claude Code (claude) in safe mode — without your hooks, plugins, MCP servers and CLAUDE.md — and send it the standard get_usage request: how much is left in the 5-hour window, the weekly window and the weekly per-model windows (for example Fable). No message is sent to the model and nothing is consumed.\n\nClaude Desktop offers no way to ask for limits on behalf of a window, so for each profile AppDeck creates a separate Claude Code folder in its own data (not ~/.claude). You sign in to it once with the same account: AppDeck opens Terminal with the standard claude auth login command. Claude Code keeps the tokens (Keychain); AppDeck never sees them and makes no network requests itself.\n\nOnly percentages, reset times, the plan and the account address are stored. Automatically at most once an hour per account and at least 5 minutes between checks of different profiles; immediately with “Limits ↻”. Turn it off in Shared settings.","AppDeck будет время от времени запускать Claude Code (claude) в безопасном режиме — без ваших хуков, плагинов, MCP-серверов и CLAUDE.md — и задавать ему штатный запрос get_usage: сколько осталось в 5-часовом окне, в недельном и в недельных окнах по моделям (например, Fable). Сообщений модели не отправляется, ничего не расходуется.\n\nУ Claude Desktop нет способа спросить лимиты от имени окна, поэтому для каждого профиля AppDeck заводит отдельную служебную папку Claude Code в своих данных (не ~/.claude). В неё нужно один раз войти тем же аккаунтом: AppDeck откроет Терминал со штатной командой claude auth login. Токены хранит Claude Code (Связка ключей); AppDeck их не видит и сам в сеть не ходит.\n\nСохраняются только проценты, время сброса, тариф и адрес аккаунта. Автоматически — не чаще раза в час на аккаунт и с паузой не меньше 5 минут между проверками разных профилей; сразу — кнопкой «Лимиты ↻». Отключается в «Общих настройках».")));
 else send<void>(alert,"setInformativeText:",str(T("From time to time AppDeck will run Codex's own helper process from the chosen app (codex app-server) with the profile's data folder and ask it two standard questions: which account is signed in and how much of the limit is left. Codex makes the request to OpenAI — the same way its window does. AppDeck never reads auth.json and never sees tokens.\n\nOnly percentages, reset times, the plan and the account address for the card label are stored. The helper works with the profile's data like any second Codex client (the CLI or an IDE extension next to the window) and exits after a second or two. Automatically at most once an hour per account and at least 5 minutes between checks of different profiles; immediately with “Limits ↻”.\n\nTurn it off in Shared settings.","AppDeck будет время от времени запускать служебный процесс самого Codex из выбранного приложения (codex app-server) с папкой данных профиля и задавать ему два штатных вопроса: какой аккаунт вошёл и сколько лимита осталось. Запрос к OpenAI выполняет Codex — так же, как его окно. AppDeck не читает auth.json и не видит токены.\n\nСохраняются только проценты, время сброса, тариф и адрес аккаунта для подписи на карточке. Служебный процесс работает с данными профиля так же, как любой второй клиент Codex (CLI или расширение IDE рядом с окном), и через секунду-две завершается. Автоматически — не чаще раза в час на аккаунт и с паузой не меньше 5 минут между проверками разных профилей; сразу — кнопкой «Лимиты ↻».\n\nОтключается в «Общих настройках».")));
 send(alert,"addButtonWithTitle:",str(T("Show limits","Показывать лимиты")));send(alert,"addButtonWithTitle:",str(T("Not now","Не нужно")));consentKeys(alert);bool yes=send<Int>(alert,"runModal")==1000;drop(alert);
 put(a,"usageLimits",boolean(yes));save();note(yes?"Account limits enabled: Codex's own app-server is queried; no token is read by AppDeck.":"Account limits declined.");return yes;
}
void toggleUsageAction(Obj,Sel,Obj){
 Obj a=currentApp();if(!usageCapable(a))return;
 if(usageEnabled(a)){put(a,"usageLimits",boolean(false));
  // Switching off also forgets the cached numbers and account labels.
  for(UInt i=0;i<count(profiles);++i)if(appFor(at(profiles,i))==a)erase(at(profiles,i),"usage");save();note("Account limits disabled; cached numbers removed.");}
 else{if(!usageBinary(a)){showError(str(T("Limits unavailable","Лимиты недоступны")),str(usageIsClaude(a)?T("Claude Code (the claude program) was not found. Install it — for example open the Code tab in Claude Desktop or install it from Anthropic's website — and try again.","Не найден Claude Code (программа claude). Установите его — например, откройте вкладку Code в Claude Desktop или выполните установку с сайта Anthropic — и повторите."):T("The chosen app has no Contents/Resources/codex helper that could be asked for limits.","В выбранном приложении нет служебной программы Contents/Resources/codex, у которой можно спросить лимиты.")));return;}
  if(askUsage(a))for(UInt i=0;i<count(profiles);++i)if(appFor(at(profiles,i))==a)usageForce(at(profiles,i));}
 buildUI();usagePump();refresh();
}
// Developer aid (APPDECK_RENDER_DIR): draws every page and the edge dock into PNG files and exits.
// Nothing is saved, launched, locked or registered in this mode; no Screen Recording permission is used.
bool renderView(Obj view,Obj path){
 Rect b=getRect(view,"bounds");Obj rep=send(view,"bitmapImageRepForCachingDisplayInRect:",b);if(!rep)return false;
 send<void>(view,"cacheDisplayInRect:toBitmapImageRep:",b,rep);
 Obj data=send(rep,"representationUsingType:properties:",(UInt)4,dict());return data&&send<bool>(data,"writeToFile:atomically:",path,true);
}
void renderPreview(Obj dir){
 Obj e=nullptr;send<bool>(fileManager,"createDirectoryAtPath:withIntermediateDirectories:attributes:error:",dir,true,(Obj)nullptr,&e);
 const char* names[]={"page-profiles.png","page-shared.png","page-projects.png","page-about.png"};
 for(Int i=0;i<4;++i){page=i;buildUI();send<void>(rootView,"layoutSubtreeIfNeeded");renderView(rootView,join(dir,names[i]));}
 ensureEdge();drop(edgeScreen);edgeScreen=keep(send(cls("NSScreen"),"mainScreen"));edgeLayout();edgeProgress=1;
 send<void>(send(edgeShade,"layer"),"setBackgroundColor:",send(color(.11,.11,.12),"CGColor"));
 edgeWanted=true;edgeRebuild=true;edgeRefresh();renderEdge();edgeWanted=false;send<void>(edgeEffect,"layoutSubtreeIfNeeded");renderView(edgeEffect,join(dir,"edge-dock.png"));
}
void registerClasses(){
 deckViewClass=objc_allocateClassPair((Class)cls("NSView"),"AppDeckFlippedView",0);method(deckViewClass,"isFlipped",flipped,"B@:");objc_registerClassPair(deckViewClass);
 deckButtonClass=objc_allocateClassPair((Class)cls("NSButton"),"AppDeckButton",0);edgeButtonClass=objc_allocateClassPair((Class)cls("NSButton"),"AppDeckEdgeButton",0);
 for(Class kind:{deckButtonClass,edgeButtonClass}){method(kind,"mouseEntered:",buttonEntered,"v@:@");method(kind,"mouseExited:",buttonExited,"v@:@");}
 // Only the nonactivating dock acts on the first click; the main window keeps the usual macOS behaviour.
 method(edgeButtonClass,"acceptsFirstMouse:",firstMouse,"B@:@");objc_registerClassPair(deckButtonClass);objc_registerClassPair(edgeButtonClass);
 // Profile rows of the dock: a click opens the profile, a drag changes the order.
 edgeRowButtonClass=objc_allocateClassPair(edgeButtonClass,"AppDeckEdgeRowButton",0);method(edgeRowButtonClass,"mouseDown:",edgeRowMouseDown,"v@:@");objc_registerClassPair(edgeRowButtonClass);
 controllerClass=objc_allocateClassPair((Class)cls("NSObject"),"AppDeckController",0);
 struct Action {const char* selector;void(*fn)(Obj,Sel,Obj);};
 Action actions[]={
 {"chooseApp:",chooseApplication},{"newProfile:",newProfileAction},{"selectApp:",selectAppAction},{"launchProfile:",launchAction},{"stopProfile:",stopAction},{"restartProfile:",restartAction},
 {"renameProfile:",renameAction},{"duplicateProfile:",duplicateAction},{"profileMoveUp:",profileMoveUpAction},{"profileMoveDown:",profileMoveDownAction},{"profileFolder:",profileFolderAction},{"removeProfile:",removeProfileAction},{"profileMore:",moreAction},
 {"toggleShare:",shareAction},{"profileError:",profileErrorAction},{"changePage:",pageAction},{"showWindow:",showWindowAction},{"launchAll:",launchAllAction},{"changePath:",changePathAction},{"removeApp:",removeAppAction},
 {"openSharedFolder:",openSharedFolder},{"openSharedFile:",openSharedFile},{"importSettings:",importSettings},{"openData:",openDataAction},{"addProject:",addProjectAction},
 {"projectAction:",projectAction},{"showHelp:",helpAction},{"diagnostics:",diagnosticsAction},{"tileWindows:",tileAction},{"timerTick:",tick},
 {"toggleEdge:",toggleEdgeAction},{"edgeFocus:",edgeFocusAction},
 {"edgeMoveUp:",edgeMoveUpAction},{"edgeMoveDown:",edgeMoveDownAction},{"edgeMoveFirst:",edgeMoveFirstAction},{"edgeDropped:",edgeDroppedAction},{"edgeScrolled:",edgeScrolledAction},{"selfTestTick:",selfTestTick},{"selfTestClick:",selfTestClickAction},{"edgeAnimationTick:",edgeAnimationTick},
 {"hideProfiles:",hideProfilesAction},{"restoreWindows:",restoreWindowsAction},
 {"connectBase:",connectBaseAction},{"syncBaseProjects:",syncBaseProjectsAction},{"automationOwner:",automationOwnerAction},{"toggleHistory:",toggleHistoryAction},{"toggleUsage:",toggleUsageAction},{"usageRefresh:",usageRefreshAction},{"usageRefreshAll:",usageRefreshAllAction},{"usageWorker:",usageWorker},{"usageFinished:",usageFinished},{"noop:",noopAction},
 {"screensChanged:",screensChangedAction},{"windowDidResize:",resizeWindow},{"applicationDidFinishLaunching:",finishedLaunching}};
 for(auto a:actions)method(controllerClass,a.selector,a.fn,"v@:@");
 method(controllerClass,"applicationShouldHandleReopen:hasVisibleWindows:",reopenApp,"B@:@B");
 method(controllerClass,"applicationShouldTerminateAfterLastWindowClosed:",keepRunning,"B@:@");
 method(controllerClass,"applicationShouldTerminate:",shouldTerminate,"q@:@");
 objc_registerClassPair(controllerClass);controller=send(send((Obj)controllerClass,"alloc"),"init");
}
} // namespace
int main(){
 umask(0077);
 appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);if(!appKit)return 10;
 dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore",2|8);
 Pool pool;app=send(cls("NSApplication"),"sharedApplication");send<bool>(app,"setActivationPolicy:",(Int)0);
 {Obj languages=send(send(cls("NSBundle"),"mainBundle"),"preferredLocalizations");ruUI=count(languages)&&send<bool>(at(languages,0),"hasPrefix:",str("ru"));}
 fileManager=send(cls("NSFileManager"),"defaultManager");workspace=send(cls("NSWorkspace"),"sharedWorkspace");
 home=keep(send(send(fileManager,"homeDirectoryForCurrentUser"),"path"));dataRoot=keep(join(home,"Library/Application Support/AppDeck"));
 // APPDECK_DATA_ROOT runs a fully separate manager (own state, profiles and lock) for acceptance tests.
 const char* rootOverride=getenv("APPDECK_DATA_ROOT");if(rootOverride&&deck::absoluteLocalPath(rootOverride)&&strlen(rootOverride)>1){drop(dataRoot);dataRoot=keep(canonical(str(rootOverride)));}
 const char* renderDir=getenv("APPDECK_RENDER_DIR");previewMode=renderDir&&deck::absoluteLocalPath(renderDir);
 if(previewMode){send<bool>(app,"setActivationPolicy:",(Int)1);}
 else if(!mkdirPrivate(dataRoot)||!mkdirPrivate(join(dataRoot,"Profiles"))||!mkdirPrivate(join(dataRoot,"Shared"))){showError(str(T("AppDeck cannot create its data folder","AppDeck не может создать папку данных")),str(T("Check the permissions of ~/Library/Application Support/AppDeck. Profile folders must not be symbolic links.","Проверьте права доступа к ~/Library/Application Support/AppDeck. Папки профилей не должны быть символическими ссылками.")));return 2;}
 Obj lockPath=join(dataRoot,"manager.lock");if(previewMode){}else if(symlink(lockPath)){showError(str(T("Invalid lock file","Некорректный файл блокировки")),str(T("manager.lock must not be a symbolic link.","manager.lock не должен быть символической ссылкой.")));return 3;}
 if(!previewMode)lockFd=open(utf8(lockPath),0x0002|0x0200,0600);if(previewMode){}else if(lockFd<0){showError(str(T("Could not open the lock file","Не удалось открыть файл блокировки")),str(T("Check the permissions of the AppDeck folder.","Проверьте права доступа к папке AppDeck.")));return 3;}else if(flock(lockFd,2|4)!=0){
  Obj runningApps=send(cls("NSRunningApplication"),"runningApplicationsWithBundleIdentifier:",str("local.appdeck.manager"));Obj me=send(cls("NSRunningApplication"),"currentApplication");
  for(UInt i=0;i<count(runningApps);++i){Obj r=at(runningApps,i);if(r!=me)focus(r);}return 0;
 }
 load();registerClasses();send<void>(app,"setDelegate:",controller);
 if(!previewMode&&adoptRunning())save();
 // One bounded maintenance pass, with the same singleton/process checks as the GUI.
 // Used for local update acceptance; it never closes or starts a managed Codex window.
 if(!previewMode&&getenv("APPDECK_SYNC_ONCE")){
  Obj report=dict(),groups=array();Int failures=0;put(report,"version",str(VERSION));put(report,"groups",groups);
  for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);if(!groupSyncEnabled(a))continue;Obj result=dict(),failure=syncGroup(a,true);put(result,"name",get(a,"name"));put(result,"status",failure?failure:get(a,"syncStatus"));put(result,"ok",boolean(!failure));add(groups,result);if(failure)++failures;}
  Obj error=nullptr,data=send(cls("NSJSONSerialization"),"dataWithJSONObject:options:error:",report,(UInt)1,&error);if(data){write(1,send<const void*>(data,"bytes"),send<UInt>(data,"length"));write(1,"\n",1);}if(lockFd>=0)close(lockFd);return failures?4:0;
 }
 Obj darkName=publicConstant(appKit,"NSAppearanceNameDarkAqua");
 Obj appearance=darkName?send(cls("NSAppearance"),"appearanceNamed:",darkName):nullptr;
 if(appearance)send<void>(app,"setAppearance:",appearance);
 window=send(send(cls("NSWindow"),"alloc"),"initWithContentRect:styleMask:backing:defer:",rect(0,0,1180,780),(UInt)(15|(1UL<<15)),(UInt)2,false);
 // Full-size content under a transparent title bar: the sidebar runs to the top edge of the window.
 send<void>(window,"setTitlebarAppearsTransparent:",true);send<void>(window,"setTitleVisibility:",(Int)1);
 // An empty unified toolbar only moves the traffic lights into the sidebar pane (inset 20, centred in a 52-pt bar).
 {Obj toolbar=send(send(cls("NSToolbar"),"alloc"),"initWithIdentifier:",str("AppDeckToolbar"));send<void>(toolbar,"setShowsBaselineSeparator:",false);
  send<void>(window,"setToolbar:",toolbar);drop(toolbar);send<void>(window,"setToolbarStyle:",(Int)3);} // NSWindowToolbarStyleUnified
 send<void>(window,"setTitle:",str("AppDeck"));send<void>(window,"setReleasedWhenClosed:",false);send<void>(window,"setMinSize:",Extent{1040,720});send<void>(window,"setDelegate:",controller);
 send<void>(window,"setOpaque:",false);send<void>(window,"setBackgroundColor:",send(cls("NSColor"),"clearColor"));send<void>(window,"setFrameAutosaveName:",str("AppDeckMainWindow"));
 // The window is glass: a behind-window blur (HUD material, like the side panel) under every page; pages add their own veils.
 Obj glass=send(send(cls("NSVisualEffectView"),"alloc"),"initWithFrame:",rect(0,0,1180,780));send<void>(glass,"setMaterial:",(Int)13);send<void>(glass,"setBlendingMode:",(Int)0);
 send<void>(glass,"setState:",(Int)1);send<void>(glass,"setAutoresizingMask:",(UInt)(2|16));send<void>(window,"setContentView:",glass);drop(glass);
 rootView=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,1180,780));send<void>(rootView,"setAutoresizingMask:",(UInt)(2|16));
 send<void>(rootView,"setWantsLayer:",true);send<void>(glass,"addSubview:",rootView);drop(rootView);
 if(!previewMode){
  Obj statusBar=send(cls("NSStatusBar"),"systemStatusBar");statusItem=keep(send(statusBar,"statusItemWithLength:",-1.0));Obj statusButton=send(statusItem,"button");
  Obj icon=send(cls("NSImage"),"imageWithSystemSymbolName:accessibilityDescription:",str("sidebar.right"),str("AppDeck"));if(icon){send<void>(icon,"setTemplate:",true);send<void>(statusButton,"setImage:",icon);}else send<void>(statusButton,"setTitle:",str("AD"));
  send<void>(statusButton,"setToolTip:",str(T("AppDeck — switch profiles","AppDeck — переключение профилей")));
 }
 Obj notifications=send(cls("NSNotificationCenter"),"defaultCenter");
 send<void>(notifications,"addObserver:selector:name:object:",controller,sel("screensChanged:"),str("NSApplicationDidChangeScreenParametersNotification"),(Obj)nullptr);
 buildMenus();buildUI();
 if(previewMode){renderPreview(str(renderDir));return 0;}
 // Centre only on the very first run: the autosaved frame must survive relaunches.
 if(!send(send(cls("NSUserDefaults"),"standardUserDefaults"),"stringForKey:",str("NSWindow Frame AppDeckMainWindow")))send<void>(window,"center");
 send<void>(window,"makeKeyAndOrderFront:",(Obj)nullptr);
 timer=keep(send(cls("NSTimer"),"scheduledTimerWithTimeInterval:target:selector:userInfo:repeats:",2.0,controller,sel("timerTick:"),(Obj)nullptr,true));
 send<void>(app,"run");if(timer){send<void>(timer,"invalidate");drop(timer);}if(lockFd>=0)close(lockFd);return 0;
}
