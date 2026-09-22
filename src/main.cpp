#include "mac.hpp"
#include "core.hpp"
#include "edge_policy.hpp"
#include "usage_policy.hpp"
using namespace mac;
namespace {
constexpr const char* VERSION="0.10.0";
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
const double colors[6][3]={{0.42,0.62,1.00},{0.33,0.83,0.64},{0.98,0.72,0.35},{0.67,0.54,1.00},{1.00,0.49,0.62},{0.31,0.78,0.88}};
void buildUI();void buildMenus();void save();void refresh();
void showError(Obj title,Obj message);
Obj currentApp();
Obj edgeTargetScreen();void setEdgeVisible(bool);
void edgeRefresh();void invalidateEdge();void connectBaseAction(Obj,Sel,Obj);
void showWindowAction(Obj,Sel,Obj);void focus(Obj);bool isMaster(Obj);
void syncBaseProjectsAction(Obj,Sel,Obj);void toggleHistoryAction(Obj,Sel,Obj);void toggleUsageAction(Obj,Sel,Obj);void usageSanitize(Obj);void usagePump();Obj cleanEnvironment();bool askUsage(Obj);void usageForce(Obj);
bool groupSyncEnabled(Obj);Obj syncGroup(Obj,bool);void groupSyncPump(bool);void automationOwnerAction(Obj,Sel,Obj);
Obj color(double r,double g,double b,double a=1){return send(cls("NSColor"),"colorWithSRGBRed:green:blue:alpha:",r,g,b,a);}
// Design tokens: one dark, translucent palette for the whole app (text, surfaces, accents, states).
Obj ink(){return color(.95,.96,.98);}Obj muted(){return color(.63,.67,.74);}Obj faint(){return color(.46,.50,.57);}
Obj accent(){return color(.46,.62,1.0);}Obj accentDeep(){return color(.40,.40,.98);}
Obj liveColor(){return color(.35,.85,.58);}Obj warnColor(){return color(.98,.74,.38);}Obj dangerColor(){return color(1.0,.47,.49);}
Obj surface(){return color(1,1,1,.045);}Obj hairline(){return color(1,1,1,.085);}
Obj accentFor(Obj p){Int i=integer(get(p,"color"))%6;if(i<0)i=0;return color(colors[i][0],colors[i][1],colors[i][2]);}
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
void continuous(Obj layer){send<void>(layer,"setCornerCurve:",str("continuous"));}
double textWidth(const char* text,double size,double weight){
 Obj attrs=dict();send<void>(attrs,"setObject:forKey:",send(cls("NSFont"),"systemFontOfSize:weight:",size,weight),str("NSFont"));
 return send<Extent>(str(text),"sizeWithAttributes:",attrs).width;
}
// Width of a button whose title (and optional icon) should fit with comfortable padding.
double fitWidth(const char* title,bool icon,double size=12.5){return (double)(Int)(textWidth(title,size,.3)+(icon?22:0)+30);}
// One button system for the whole app: borderless, layer-backed, with hover. The style name
// lives in the view identifier so the hover handlers need no per-instance storage.
Obj buttonFill(Obj self,bool hover){
 Obj k=send(self,"identifier");
 if(same(k,str("primary")))return hover?color(1,1,1,.12):color(0,0,0,0); // over its gradient backdrop
 if(same(k,str("danger")))return hover?color(1,.40,.42,.26):color(1,.40,.42,.14);
 if(same(k,str("overlay")))return hover?color(1,1,1,.06):color(0,0,0,0);
 if(same(k,str("ghost")))return hover?color(1,1,1,.07):color(0,0,0,0);
 if(same(k,str("edge")))return hover?color(1,1,1,.14):color(1,1,1,.07);
 return hover?color(1,1,1,.12):color(1,1,1,.065);
}
void buttonEntered(Obj self,Sel,Obj){if(send<bool>(self,"isEnabled"))send<void>(send(self,"layer"),"setBackgroundColor:",send(buttonFill(self,true),"CGColor"));}
void buttonExited(Obj self,Sel,Obj){send<void>(send(self,"layer"),"setBackgroundColor:",send(buttonFill(self,false),"CGColor"));}
bool firstMouse(Obj,Sel,Obj){return true;}
Obj symbol(const char* name,double size=14);
// Primary actions sit on a soft accent gradient with a light rim and glow; the button above it stays
// transparent, so hover simply brightens the gradient.
void primaryBackdrop(Obj parent,Rect f,double radius){
 Obj v=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",f);send<void>(v,"setWantsLayer:",true);send<void>(parent,"addSubview:",v);drop(v);
 Obj layer=send(v,"layer"),g=send(cls("CAGradientLayer"),"layer");
 Obj colors=array();add(colors,send(accent(),"CGColor"));add(colors,send(accentDeep(),"CGColor"));
 send<void>(g,"setColors:",colors);send<void>(g,"setStartPoint:",Point{0,0});send<void>(g,"setEndPoint:",Point{1,1});
 send<void>(g,"setFrame:",rect(0,0,f.size.width,f.size.height));send<void>(g,"setCornerRadius:",radius);continuous(g);
 send<void>(g,"setBorderWidth:",1.0);send<void>(g,"setBorderColor:",send(color(1,1,1,.16),"CGColor"));send<void>(layer,"addSublayer:",g);
 send<void>(layer,"setShadowColor:",send(accent(),"CGColor"));send<void>(layer,"setShadowOpacity:",(float).35);send<void>(layer,"setShadowRadius:",9.0);send<void>(layer,"setShadowOffset:",Extent{0,0});
}
Obj styledButton(Class kind,Obj parent,const char* title,const char* action,Rect f,Int tag,const char* style,double radius=9,double size=12.5,const char* icon=nullptr){
 bool primary=strcmp(style,"primary")==0,danger=strcmp(style,"danger")==0,secondary=strcmp(style,"secondary")==0;
 if(primary)primaryBackdrop(parent,f,radius);
 Obj b=send(send((Obj)kind,"alloc"),"initWithFrame:",f);send<void>(b,"setTitle:",str(title));send<void>(b,"setBordered:",false);
 send<void>(b,"setFont:",send(cls("NSFont"),"systemFontOfSize:weight:",size,primary?0.3:0.23));send<void>(b,"setTarget:",controller);send<void>(b,"setAction:",sel(action));send<void>(b,"setTag:",tag);
 send<void>(b,"setIdentifier:",str(style));send<void>(b,"setWantsLayer:",true);Obj layer=send(b,"layer");send<void>(layer,"setCornerRadius:",radius);continuous(layer);
 send<void>(layer,"setBackgroundColor:",send(buttonFill(b,false),"CGColor"));
 if(secondary||danger){send<void>(layer,"setBorderWidth:",1.0);send<void>(layer,"setBorderColor:",send(danger?color(1,.45,.47,.22):hairline(),"CGColor"));}
 if(icon){if(*title)send<void>(b,"setTitle:",cat(str(" "),str(title))); // a word space between icon and title
  send<void>(b,"setImage:",symbol(icon,size));send<void>(b,"setImagePosition:",(UInt)(*title?7:1));send<void>(b,"setImageHugsTitle:",true);} // NSImageLeading / NSImageOnly
 send<void>(b,"setContentTintColor:",primary?color(1,1,1):danger?color(1,.62,.63):ink());
 // NSTrackingMouseEnteredAndExited | ActiveAlways | InVisibleRect: hover also works in the nonactivating dock.
 Obj area=send(send(cls("NSTrackingArea"),"alloc"),"initWithRect:options:owner:userInfo:",rect(0,0,0,0),(UInt)(0x01|0x80|0x200),b,(Obj)nullptr);
 send<void>(b,"addTrackingArea:",area);drop(area);send<void>(parent,"addSubview:",b);drop(b);return b;
}
Obj button(Obj parent,const char* title,const char* action,Rect f,Int tag=-1,bool primary=false,const char* icon=nullptr){return styledButton(deckButtonClass,parent,title,action,f,tag,primary?"primary":"secondary",9,12.5,icon);}
Obj checkbox(Obj parent,const char* title,Rect f,bool on){
 Obj b=send(cls("NSButton"),"checkboxWithTitle:target:action:",str(title),controller,sel("noop:"));send<void>(b,"setFrame:",f);send<void>(b,"setState:",(Int)(on?1:0));send<void>(parent,"addSubview:",b);return b;
}
Obj panel(Obj parent,Rect f,Obj bg,double radius=14){
 Obj p=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",f);send<void>(p,"setWantsLayer:",true);Obj layer=send(p,"layer");send<void>(layer,"setBackgroundColor:",send(bg,"CGColor"));send<void>(layer,"setCornerRadius:",radius);continuous(layer);
 send<void>(parent,"addSubview:",p);drop(p);return p;
}
// A glass card: a faint fill with a hairline rim.
Obj card(Obj parent,Rect f,double radius=16,Obj fill=nullptr){
 Obj c=panel(parent,f,fill?fill:surface(),radius);Obj l=send(c,"layer");send<void>(l,"setBorderWidth:",1.0);send<void>(l,"setBorderColor:",send(hairline(),"CGColor"));return c;
}
// A key cap such as ⌘1 or ⌃⌥Space.
Obj keycap(Obj parent,const char* text,Rect f){
 Obj k=card(parent,f,6,color(1,1,1,.05));Obj t=label(k,text,rect(0,(f.size.height-14)/2,f.size.width,14),10.5,muted(),.23);send<void>(t,"setAlignment:",alignCenter);return k;
}
// A status light; its colour follows the profile state.
Obj statusDot(Obj parent,Rect f){return panel(parent,f,faint(),f.size.width/2);}
void statusDotColor(Obj dot,Obj c,bool glow){
 Obj l=send(dot,"layer");send<void>(l,"setBackgroundColor:",send(c,"CGColor"));
 send<void>(l,"setShadowColor:",send(c,"CGColor"));send<void>(l,"setShadowOpacity:",(float)(glow?.8:0));send<void>(l,"setShadowRadius:",4.0);send<void>(l,"setShadowOffset:",Extent{0,0});
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
Obj scrollDocument(Obj parent,Rect f,double docHeight){
 Obj scroll=send(send(cls("NSScrollView"),"alloc"),"initWithFrame:",f);send<void>(scroll,"setDrawsBackground:",false);send<void>(scroll,"setHasVerticalScroller:",true);send<void>(scroll,"setAutohidesScrollers:",true);
 Obj doc=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,f.size.width-12,docHeight));send<void>(scroll,"setDocumentView:",doc);send<void>(parent,"addSubview:",scroll);drop(scroll);drop(doc);return doc;
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
// An SF Symbol on a small tinted square, as in System Settings.
Obj iconTile(Obj parent,Rect f,const char* name,Obj tint){
 Obj t=panel(parent,f,send(tint,"colorWithAlphaComponent:",.18),f.size.width*.28);imageView(t,symbol(name,f.size.width*.46),rect(0,0,f.size.width,f.size.height),tint);return t;
}
// Sidebar row: background, icon, title and a transparent hover/click layer over the whole row.
Obj navRow(Obj parent,Rect f,Obj image,bool tinted,Obj title,const char* action,Int tag,bool selected){
 Obj row=panel(parent,f,selected?color(1,1,1,.10):color(0,0,0,0),8);double h=f.size.height;
 if(selected){Obj l=send(row,"layer");send<void>(l,"setBorderWidth:",1.0);send<void>(l,"setBorderColor:",send(color(1,1,1,.06),"CGColor"));}
 if(image)imageView(row,image,rect(10,(h-18)/2,18,18),tinted?(selected?accent():muted()):nullptr);
 labelObj(row,title,rect(38,(h-17)/2,f.size.width-46,17),13,selected?ink():color(.80,.83,.88),selected?.3:.2);
 Obj b=styledButton(deckButtonClass,row,"",action,rect(0,0,f.size.width,h),tag,"overlay",8);send<void>(b,"setAccessibilityLabel:",title);return b;
}
Obj cardFact(Obj c,const char* caption,const char* value,double x,double w,Obj tint=nullptr){
 label(c,caption,rect(x,90,w-8,13),10,faint(),.3);return label(c,value,rect(x,106,w-8,19),13,tint?tint:ink(),.23);
}
// Account label, plan pill and limit strip follow the cached record; called on build and on every refresh.
void usageApplyCard(Obj ref,Obj p){
 Obj u=usageOf(p),accountLabel=get(ref,"account");if(!accountLabel)return;Obj email=get(u,"email");
 Obj twin=usageTwin(p);send<void>(accountLabel,"setTextColor:",twin?warnColor():ink());
 bool claudeCard=usageIsClaude(appFor(p));
 send<void>(accountLabel,"setStringValue:",email?(twin?cat(str("⚠ "),email):email):str(isMaster(p)?(claudeCard?T("Current sign-in","Текущий вход"):T("Current Codex","Текущий Codex")):T("Separate sign-in","Отдельный вход")));send<void>(accountLabel,"setFont:",send(cls("NSFont"),"systemFontOfSize:weight:",email?12.5:13.0,.23));send<void>(accountLabel,"setToolTip:",!email?(Obj)nullptr:twin?cat(cat(cat(str(T("Same account as in profile “","Тот же аккаунт, что и в профиле «")),get(twin,"name")),str(T("”: they share one limit. To use another account in this profile, sign out in its window and sign in with the right one.\n","»: лимит у них общий. Чтобы профиль работал под другим аккаунтом, выйдите из аккаунта в его окне и войдите под нужным.\n"))),email):cat(str(claudeCard?T("The Claude Code sign-in AppDeck uses to ask for limits. It must match the account in this profile's window: ","Вход Claude Code, по которому AppDeck спрашивает лимиты. Он должен совпадать с аккаунтом в окне этого профиля: "):T("Account signed in to this profile: ","Аккаунт, вошедший в этот профиль: ")),email));
 Obj plan=get(u,"plan");usageHide(get(ref,"planPill"),!plan);if(plan)send<void>(get(ref,"planLabel"),"setStringValue:",send(plan,"uppercaseString"));
 usageApplyStrip(get(ref,"usage"),p);
}
const char* adapterTitle(Obj a){switch(deck::adapter(utf8(get(a,"adapter")))){case deck::Adapter::Codex:return "Codex";case deck::Adapter::VSCode:return "VS Code";case deck::Adapter::Chromium:return "Chromium";case deck::Adapter::Electron:return "Electron";case deck::Adapter::Claude:return "Claude";default:return "—";}}
double cardHeightFor(Obj a){return usageEnabled(a)?252:190;}
void makeCard(Obj parent,Obj p,Rect frame,Int ordinal){
 Obj c=card(parent,frame,16);Obj a=appFor(p);double w=frame.size.width,cardHeight=frame.size.height;
 bool codex=deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Codex,master=isMaster(p),limits=usageEnabled(a);Obj ref=dict();
 // Identity: the app icon on a tile in the profile's colour, the name and a status light.
 Obj tile=panel(c,rect(18,18,44,44),send(accentFor(p),"colorWithAlphaComponent:",.16),12);Obj tl=send(tile,"layer");
 send<void>(tl,"setBorderWidth:",1.0);send<void>(tl,"setBorderColor:",send(send(accentFor(p),"colorWithAlphaComponent:",.38),"CGColor"));
 appIcon(tile,get(a,"path"),rect(6,6,32,32));
 double rx=w-18;
 if(ordinal<9){char b[16];snprintf(b,sizeof b,"⌘%ld",ordinal+1);rx-=34;keycap(c,b,rect(rx,20,34,22));rx-=8;}
 if(limits){Obj planPill=panel(c,rect(rx-50,20,50,22),send(accent(),"colorWithAlphaComponent:",.17),11);Obj planLabel=label(planPill,"",rect(0,4,50,14),10,color(.74,.81,1.0),.4);send<void>(planLabel,"setAlignment:",alignCenter);put(ref,"planPill",planPill);put(ref,"planLabel",planLabel);rx-=58;}
 labelObj(c,get(p,"name"),rect(76,18,rx-76-6,22),16,ink(),.4);
 Obj dot=statusDot(c,rect(77,48,7,7));Obj st=label(c,T("Not running","Не запущен"),rect(90,43,w-90-18-(get(p,"lastError")?96:0),16),12,muted(),.23);
 if(get(p,"lastError"))styledButton(deckButtonClass,c,T("Details","Подробнее"),"profileError:",rect(w-18-88,41,88,24),profileIndex(p),"secondary",7,11,"exclamationmark.triangle");
 panel(c,rect(18,76,w-36,1),hairline(),0);double col=(w-36)/3;
 Obj accountLabel=cardFact(c,T("ACCOUNT","АККАУНТ"),master?(codex?T("Current Codex","Текущий Codex"):T("Current sign-in","Текущий вход")):T("Separate sign-in","Отдельный вход"),18,col);send<void>(accountLabel,"setLineBreakMode:",(Int)5);if(limits)put(ref,"account",accountLabel);
 if(limits)put(ref,"usage",usageStrip(c,18,140,w-36,profileIndex(p)));
 if(codex){cardFact(c,T("SETTINGS","НАСТРОЙКИ"),master?T("Original","Оригинал"):truth(get(p,"share"))?T("From base","Из базы"):T("Local","Локальные"),18+col,col,!master&&truth(get(p,"share"))?accentFor(p):nullptr);
  cardFact(c,T("HISTORY","ИСТОРИЯ"),master?T("Original","Оригинал"):sharesHistory(p)?T("Shared","Общая"):T("Own","Своя"),18+col*2,col,sharesHistory(p)?accentFor(p):nullptr);}
 else{cardFact(c,T("DATA","ДАННЫЕ"),master?T("Original","Оригинал"):T("Own folder","Своя папка"),18+col,col);cardFact(c,T("ADAPTER","АДАПТЕР"),adapterTitle(a),18+col*2,col);}
 // Actions: the main one on the accent gradient, the others as glass buttons with icons.
 double by=cardHeight-52;Int tag=profileIndex(p);
 Obj more=button(c,"","profileMore:",rect(w-18-40,by,40,34),tag,false,"ellipsis");send<void>(more,"setToolTip:",str(T("More actions","Другие действия")));send<void>(more,"setAccessibilityLabel:",str(T("More actions","Другие действия")));
 if(running(p)){ // running: close / restart / bring the window forward
  double avail=w-36-40-16,closeW=avail*.27,restartW=avail*.38,windowW=avail-closeW-restartW;
  Obj close=styledButton(deckButtonClass,c,T("Close","Закрыть"),"stopProfile:",rect(18,by,closeW,34),tag,"danger",9,12.5,"xmark");send<void>(close,"setToolTip:",str(T("Send the window a normal quit request (the process is never force-killed)","Отправить окну обычный запрос завершения (без принудительного убийства процесса)")));
  Obj again=button(c,T("Restart","Перезапустить"),"restartProfile:",rect(18+closeW+8,by,restartW-8,34),tag,false,"arrow.clockwise");send<void>(again,"setToolTip:",str(T("Close the window and launch the profile again right away — for example to pick up new projects and settings","Закрыть окно и сразу запустить профиль заново — например, чтобы подтянуть новые проекты и настройки")));
  Obj front=button(c,T("Window","Окно"),"launchProfile:",rect(18+closeW+restartW+8,by,windowW,34),tag,true,"macwindow");send<void>(front,"setToolTip:",str(T("Bring this profile's window to the front","Вывести окно этого профиля на передний план")));
 }else{double folderW=fitWidth(T("Folder","Папка"),true),launchW=w-36-40-8-folderW-8;
  button(c,T("Launch","Запустить"),"launchProfile:",rect(18,by,launchW,34),tag,true,"play.fill");
  Obj folder=button(c,T("Folder","Папка"),"profileFolder:",rect(18+launchW+8,by,folderW,34),tag,false,"folder");send<void>(folder,"setToolTip:",shortPath(master?masterFolder(a):profileRoot(p)));}
 put(ref,"status",st);put(ref,"dot",dot);put(ref,"live",boolean(running(p)!=nullptr));put(cardRefs,utf8(get(p,"id")),ref);usageApplyCard(ref,p);
}
void settingRow(Obj box,double y,double w,const char* title,Obj description,const char* buttonTitle,const char* action,Int tag,bool primary=false){
 label(box,title,rect(24,y,w-220,22),15,ink(),.25);labelObj(box,description,rect(24,y+25,w-210,20),12,muted());
 button(box,buttonTitle,action,rect(w-150,y+7,126,32),tag,primary);
}
void buildUI(){
 if(!rootView||building)return;building=true;
 Obj old=send(send(rootView,"subviews"),"copy");for(UInt i=0;i<count(old);++i)send<void>(at(old,i),"removeFromSuperview");drop(old);send<void>(cardRefs,"removeAllObjects");footer=nullptr;usageFooter=nullptr;
 Rect bounds=getRect(rootView,"bounds");double W=bounds.size.width,H=bounds.size.height;const double side=232,x=side+36,w=W-x-36;
 // Glass: the whole window is a behind-window blur. The sidebar keeps most of it; the content area
 // gets a dark veil for legibility; a hairline separates them.
 panel(rootView,rect(0,0,side,H),color(.02,.025,.04,.16),0);panel(rootView,rect(side,0,W-side,H),color(.05,.055,.08,.80),0);panel(rootView,rect(side,0,1,H),hairline(),0);
 imageView(rootView,send(app,"applicationIconImage"),rect(16,44,36,36));
 label(rootView,"AppDeck",rect(58,46,164,20),16,ink(),.4);label(rootView,T("One place. Many accounts.","Одно место. Разные аккаунты."),rect(58,66,168,14),10.5,muted(),.1);
 const char* navTitles[]={T("Profiles","Профили"),T("Shared settings","Общие настройки"),T("Projects","Проекты"),T("About","О приложении")};const char* navIcons[]={"person.2.fill","slider.horizontal.3","folder.fill","info.circle.fill"};
 for(Int i=0;i<4;++i)navRow(rootView,rect(10,102+i*34,side-20,30),symbol(navIcons[i],14),true,str(navTitles[i]),"changePage:",i,page==i);
 label(rootView,T("APPS","ПРИЛОЖЕНИЯ"),rect(20,254,150,13),10,faint(),.3);
 double listTop=272,listH=count(apps)*36.0+2,listMax=H-listTop-136;if(listMax<72)listMax=72;if(listH>listMax)listH=listMax;
 Obj sideList=scrollDocument(rootView,rect(10,listTop,side-10,listH),count(apps)*36.0+2);
 for(UInt i=0;i<count(apps);++i){Obj a=at(apps,i);navRow(sideList,rect(0,i*36.0,side-20,32),send(workspace,"iconForFile:",get(a,"path")),false,get(a,"name"),"selectApp:",(Int)i,same(get(a,"id"),selected));}
 navRow(rootView,rect(10,listTop+listH+4,side-20,30),symbol("plus.circle",14),true,str(T("Add app","Добавить приложение")),"chooseApp:",-1,false);
 {Obj sp=card(rootView,rect(10,H-82,side-20,38),10,color(1,1,1,.05));imageView(sp,symbol("sidebar.right",14),rect(12,10,18,18),muted());
  label(sp,T("Side panel","Правая панель"),rect(38,10,side-20-38-80,18),13,ink(),.23);keycap(sp,"⌃⌥Space",rect(side-20-10-66,9,66,20));
  Obj edgeToggle=styledButton(deckButtonClass,sp,"","toggleEdge:",rect(0,0,side-20,38),-1,"overlay",10);send<void>(edgeToggle,"setAccessibilityLabel:",str(T("Side panel","Правая панель")));
  send<void>(edgeToggle,"setToolTip:",str(T("A slim panel at the right edge of the screen: quick launch and account switching","Узкая панель у правого края экрана: быстрый запуск и переключение аккаунтов")));}
 labelObj(rootView,cat(str("AppDeck "),str(VERSION)),rect(20,H-32,190,14),10,faint(),.1);
 Obj a=currentApp();bool codexGroup=a&&deck::adapter(utf8(get(a,"adapter")))==deck::Adapter::Codex;
 if(page==0){
  label(rootView,T("APP PROFILES","ПРОФИЛИ ПРИЛОЖЕНИЯ"),rect(x,34,w,13),10.5,faint(),.3);
  double titleW=w;
  if(a){const char* t1=T("App location…","Путь приложения…");const char* t2=T("2×2 grid","Сетка 2×2");const char* t3=T("Launch all","Запустить все");const char* t4=T("Profile","Профиль");
   double w1=fitWidth(t1,true),w2=fitWidth(t2,true),w3=fitWidth(t3,true),w4=fitWidth(t4,true),bx=W-36-(w1+w2+w3+w4+24);titleW=bx-x-12;
   button(rootView,t1,"changePath:",rect(bx,54,w1,32),-1,false,"folder");
   Obj tile=button(rootView,t2,"tileWindows:",rect(bx+w1+8,54,w2,32),-1,false,"square.grid.2x2");
   send<void>(tile,"setToolTip:",str(T("Arrange up to four running windows of this group. Requires Accessibility access.","Разложить до четырёх запущенных окон этой группы. Нужен Универсальный доступ.")));
   button(rootView,t3,"launchAll:",rect(bx+w1+w2+16,54,w3,32),-1,false,"play");button(rootView,t4,"newProfile:",rect(bx+w1+w2+w3+24,54,w4,32),-1,true,"plus");
   Obj origin=labelObj(rootView,cat(str(T("Original: ","Оригинал: ")),shortPath(get(a,"path"))),rect(x,90,titleW,17),12,muted(),.1);send<void>(origin,"setLineBreakMode:",(Int)5);
  }
  Obj title=labelObj(rootView,a?get(a,"name"):str(T("Your apps, side by side","Ваши приложения, рядом")),rect(x,50,titleW,34),26,ink(),.4);send<void>(title,"setLineBreakMode:",(Int)4);
  Obj visible=visibleProfiles();
  if(count(visible)){
   double gap=16,cw=(w-gap-14)/2.0,cardHeight=cardHeightFor(a);Int columns=2;if(cw<330){columns=1;cw=w-14;}
   UInt rows=(count(visible)+columns-1)/columns;
   Obj doc=scrollDocument(rootView,rect(x,126,w,H-126-56),rows*(cardHeight+gap)+4);
   for(UInt i=0;i<count(visible);++i)makeCard(doc,at(visible,i),rect((i%columns)*(cw+gap),(i/columns)*(cardHeight+gap),cw,cardHeight),(Int)i);
  }else{
   Obj box=card(rootView,rect(x,130,w,300),20);iconTile(box,rect(34,34,52,52),"rectangle.stack.badge.person.crop",accent());
   label(box,T("Many accounts.\nOne workspace.","Несколько аккаунтов.\nОдна рабочая среда."),rect(34,100,w-68,70),26,ink(),.4);
   label(box,a?T("This group has no profiles yet. Add the first one — you sign in inside the app itself.","В этой группе пока нет профилей. Добавьте первый — вход выполняется уже внутри приложения."):T("Choose Codex (ChatGPT.app), Claude or another supported app and create profiles.","Выберите Codex (ChatGPT.app), Claude или другое поддерживаемое приложение и создайте профили."),rect(34,176,w-68,20),13.5,muted(),.1);
   label(box,T("Every profile is an ordinary macOS window with its own sign-in. The original .app is neither copied nor modified.","Каждый профиль — обычное окно macOS со своим входом. Исходное .app не копируется и не изменяется."),rect(34,200,w-68,20),13,faint(),.1);
   if(a)button(box,T("Create profile","Создать профиль"),"newProfile:",rect(34,238,fitWidth(T("Create profile","Создать профиль"),true,13),36),-1,true,"plus");
   else button(box,T("Choose app…","Выбрать приложение…"),"chooseApp:",rect(34,238,fitWidth(T("Choose app…","Выбрать приложение…"),true,13),36),-1,true,"plus.app");
  }
  bool limitsOn=usageEnabled(a),capable=usageCapable(a);double reserved=!capable?0:limitsOn?380:200;
  footer=label(rootView,"",rect(x,H-37,w-reserved,17),11.5,faint(),.1);send<void>(footer,"setLineBreakMode:",(Int)4);
  if(capable&&limitsOn){Obj again=styledButton(deckButtonClass,rootView,T("Limits","Лимиты"),"usageRefreshAll:",rect(W-36-fitWidth(T("Limits","Лимиты"),true,12),H-44,fitWidth(T("Limits","Лимиты"),true,12),28),-1,"secondary",8,12,"arrow.clockwise");send<void>(again,"setToolTip:",str(T("Check the limits of all profiles now. On its own, AppDeck asks each account at most once an hour, with 5 minutes between profiles.","Проверить лимиты всех профилей сейчас. Сам AppDeck спрашивает каждый аккаунт не чаще раза в час, с паузой 5 минут между профилями.")));
   usageFooter=label(rootView,"",rect(W-36-fitWidth(T("Limits","Лимиты"),true,12)-10-280,H-38,280,16),11.5,faint(),.1);send<void>(usageFooter,"setAlignment:",alignRight);}
  else if(capable){Obj enable=styledButton(deckButtonClass,rootView,T("Account limits…","Лимиты аккаунтов…"),"toggleUsage:",rect(W-36-fitWidth(T("Account limits…","Лимиты аккаунтов…"),true,12),H-44,fitWidth(T("Account limits…","Лимиты аккаунтов…"),true,12),28),-1,"secondary",8,12,"gauge.with.dots.needle.33percent");send<void>(enable,"setToolTip:",str(T("Show on the cards and in the side panel how much of the weekly limit each account has left","Показывать на карточках и в правой панели, сколько недельного лимита осталось у каждого аккаунта")));}
 }else if(page==1){
  label(rootView,T("SHARED WORKSPACE","ОБЩАЯ РАБОЧАЯ СРЕДА"),rect(x,28,w,14),10,muted(),.2);label(rootView,T("One workspace, separate sign-ins","Одна среда, разные входы"),rect(x,48,w,36),27,ink(),.3);
  if(usageIsClaude(a)){bool limitsOn=usageEnabled(a);
   label(rootView,T("Claude Desktop: each profile has its own data folder and sign-in; the settings and history of the Code tab (~/.claude) are shared.","Claude Desktop: у каждого профиля своя папка данных и свой вход; настройки и история вкладки Code (~/.claude) общие."),rect(x,94,w,18),12,muted());
   Obj box=panel(rootView,rect(x,126,w,86),color(.098,.11,.133));Obj bl=send(box,"layer");send<void>(bl,"setBorderWidth:",1.0);send<void>(bl,"setBorderColor:",send(color(.18,.20,.24),"CGColor"));
   settingRow(box,18,w,T("Account limits","Лимиты аккаунтов"),str(limitsOn?T("On · 5 hours, week and weekly per-model limits (for example Fable)","Включено · 5 часов, неделя и недельные лимиты по моделям (например, Fable)"):!usageBinary(a)?T("Claude Code (claude) not found — limits need it","Не найден Claude Code (claude) — без него лимиты недоступны"):T("Off · AppDeck does not run Claude Code","Выключено · AppDeck не запускает Claude Code")),limitsOn?T("Turn off","Отключить"):T("Turn on…","Включить…"),"toggleUsage:",-1,!limitsOn&&usageBinary(a));
   label(rootView,T("Claude Desktop hands its sign-in to the embedded Claude Code in memory, so there is nothing to ask for limits “on behalf of the window”. For each profile AppDeck keeps\na separate Claude Code folder (in AppDeck's data, not ~/.claude); you sign in to it once with the same account using the standard claude auth login.\nAnthropic counts limits per account, so the numbers match the Claude window. Claude Code keeps the tokens; AppDeck never sees them and makes no network requests itself.","Claude Desktop передаёт свой вход встроенному Claude Code в памяти, поэтому спросить лимиты «от имени окна» нечем. Для каждого профиля AppDeck держит\nотдельную служебную папку Claude Code (в данных AppDeck, не ~/.claude); вы один раз входите в неё тем же аккаунтом через штатный claude auth login.\nЛимиты у Anthropic считаются на аккаунт, поэтому цифры совпадают с окном Claude. Токены хранит Claude Code; AppDeck их не видит и в сеть сам не ходит."),rect(x,230,w,62),12,muted());}
  else if(!codexGroup){label(rootView,T("Choose a Codex or Claude group on the left. Shared settings are available for Codex, limits for both.","Выберите группу Codex или Claude слева. Общие настройки доступны для Codex, лимиты — для обеих."),rect(x,96,w,44),14,muted());}
  else{
   bool based=baseSource(a)!=nullptr,history=based&&truth(get(a,"sharedHistory"))&&historyAvailable(a);
   Obj source=labelObj(rootView,based?cat(str(T("Source: ","Источник: ")),shortPath(baseSource(a))):str(T("No source connected — this group uses AppDeck's shared folder","Источник не подключён — используется общая папка AppDeck этой группы")),rect(x,94,w,18),12,muted());send<void>(source,"setLineBreakMode:",(Int)5);
   const double rowH=58;bool limitsOn=usageEnabled(a);Obj box=panel(rootView,rect(x,122,w,rowH*6+20),color(.098,.11,.133));Obj bl=send(box,"layer");send<void>(bl,"setBorderWidth:",1.0);send<void>(bl,"setBorderColor:",send(color(.18,.20,.24),"CGColor"));
   const char* titles[]={"config.toml","AGENTS.md","skills/","rules/"};const char* descriptions[]={T("Copied before launch · model, MCP and agent settings","Копируется перед запуском · модель, MCP и настройки агента"),T("Copied before launch · instructions for all profiles","Копируется перед запуском · инструкции для всех профилей"),T("Shared folder via link · changes are visible to all profiles","Общая папка по ссылке · изменения видны всем профилям"),T("Shared folder via link · command execution rules","Общая папка по ссылке · правила выполнения команд")};
   for(Int i=0;i<4;++i){settingRow(box,14+i*rowH,w,titles[i],str(descriptions[i]),T("Open","Открыть"),"openSharedFile:",i);panel(box,rect(24,14+(i+1)*rowH-9,w-48,1),color(1,1,1,.05),0);}
   settingRow(box,14+4*rowH,w,T("Shared workspace","Общая среда"),str(!based?T("Unavailable until the current Codex is connected","Недоступно, пока не подключён текущий Codex"):history?T("History, projects and automations · changes from every profile","История, проекты и автоматизации · изменения из всех профилей"):historyAvailable(a)?T("Off · each copy keeps its own history","Выключено · у каждой копии своя история"):T("The source has no state_*.sqlite thread database","В источнике нет базы тредов state_*.sqlite")),history?T("Turn off…","Отключить…"):T("Turn on…","Включить…"),"toggleHistory:",-1,!history&&based);
   settingRow(box,14+5*rowH,w,T("Account limits","Лимиты аккаунтов"),str(limitsOn?T("On · weekly limit left on the cards and in the side panel","Включено · остаток недельного лимита на карточках и в правой панели"):!usageBinary(a)?T("This app has no codex helper — limits unavailable","В этом приложении нет служебного codex — лимиты недоступны"):T("Off · AppDeck does not run the Codex helper process","Выключено · AppDeck не запускает служебный процесс Codex")),limitsOn?T("Turn off","Отключить"):T("Turn on…","Включить…"),"toggleUsage:",-1,!limitsOn&&usageBinary(a));
   panel(box,rect(24,14+5*rowH-9,w-48,1),color(1,1,1,.05),0);double by=122+rowH*6+20+14;
   button(rootView,based?T("Base workspace…","Базовая среда…"):T("Connect Codex…","Подключить Codex…"),"connectBase:",rect(x,by,176,34),-1,!based);button(rootView,T("Sync ↻","Синхронизировать ↻"),"syncBaseProjects:",rect(x+184,by,188,34));button(rootView,T("Automations…","Автоматизации…"),"automationOwner:",rect(x+380,by,168,34));button(rootView,T("Folder","Папка"),"openSharedFolder:",rect(x+556,by,86,34));
   labelObj(rootView,history?(get(a,"syncStatus")?get(a,"syncStatus"):str(T("Reconciled before launch; running windows update after a restart","Согласование перед запуском; работающие окна обновятся после перезапуска"))):str(T("Each account's sign-in and data stay separate.","Вход и данные каждого аккаунта остаются раздельными.")),rect(x,by+46,w,20),13,accent());
   label(rootView,T("Projects: add, rename and remove in any profile → one shared list.\nAutomations: one shared catalogue, a single assigned owner; the other copies are paused.\nconfig.toml and AGENTS.md still come from the base workspace. Sign-in and cookies are never transferred.","Проекты: добавление, переименование и удаление из любого профиля → общий список.\nАвтоматизации: общий каталог, один назначенный исполнитель; остальные копии приостановлены.\nconfig.toml и AGENTS.md по-прежнему поступают из базовой среды. Вход и cookies не переносятся."),rect(x,by+72,w,58),12,muted());
  }
 }else if(page==2){
  label(rootView,T("ONE LIST OF FOLDERS","ЕДИНЫЙ СПИСОК ПАПОК"),rect(x,28,w,14),10,muted(),.2);label(rootView,T("Projects","Проекты"),rect(x,48,w-220,36),27,ink(),.3);
  button(rootView,T("＋  Add folder","＋  Добавить папку"),"addProject:",rect(W-32-188,50,188,34),-1,true);
  label(rootView,T("Shared projects are reconciled between profiles before launch. Removing one from the list keeps its folders and tasks.","Общие проекты согласуются между профилями перед запуском. Удаление из списка сохраняет папки и задачи."),rect(x,94,w,18),12,muted());
  if(!count(projects)){Obj c=panel(rootView,rect(x,130,w,150),color(.098,.11,.133));label(c,T("One repository — several profiles","Один репозиторий — несколько профилей"),rect(26,28,w-52,28),20,ink(),.25);label(c,T("Keep your working folders here to open them or copy their path quickly.","Сохраните рабочие папки здесь, чтобы быстро открыть их или скопировать путь."),rect(26,70,w-52,40),13,muted());}
  else{const double rowH=66;Obj doc=scrollDocument(rootView,rect(x,130,w,H-130-58),count(projects)*(rowH+10));
   for(UInt i=0;i<count(projects);++i){Obj p=at(projects,i);double rw=w-14;Obj c=panel(doc,rect(0,i*(rowH+10),rw,rowH),color(.098,.11,.133),12);
    imageView(c,symbol("folder",17),rect(18,21,24,24),exists(get(p,"path"))?accent():muted());
    labelObj(c,get(p,"name"),rect(54,12,rw-54-300,22),15,ink(),.25);Obj path=labelObj(c,exists(get(p,"path"))?shortPath(get(p,"path")):cat(str(T("Folder not found · ","Папка не найдена · ")),shortPath(get(p,"path"))),rect(54,36,rw-54-300,18),11,muted());send<void>(path,"setLineBreakMode:",(Int)5);
    button(c,"Finder","projectAction:",rect(rw-288,17,84,32),(Int)i*3);button(c,T("Path","Путь"),"projectAction:",rect(rw-196,17,84,32),(Int)i*3+1);button(c,T("Remove","Убрать"),"projectAction:",rect(rw-104,17,88,32),(Int)i*3+2);
   }}
  label(rootView,T("To change code from several accounts at once, use separate Git worktrees.","Для одновременного изменения кода из разных аккаунтов используйте отдельные Git worktree."),rect(x,H-38,w,18),12,accent());
 }else{
  label(rootView,"APPDECK / MACOS",rect(x,28,w,14),10,muted(),.2);label(rootView,T("Native profile manager","Нативный менеджер профилей"),rect(x,48,w,36),27,ink(),.3);
  Obj box=panel(rootView,rect(x,104,w,254),color(.098,.11,.133));Obj bl=send(box,"layer");send<void>(bl,"setBorderWidth:",1.0);send<void>(bl,"setBorderColor:",send(color(.18,.20,.24),"CGColor"));
  label(box,"C++17 + AppKit",rect(26,22,w-52,28),21,ink(),.25);label(box,T("No Electron, no WebView, no telemetry, no server of its own.","Без Electron, WebView, телеметрии и собственного сервера."),rect(26,58,w-52,22),13.5,muted());
  labelObj(box,cat(cat(str(T("Version ","Версия ")),str(VERSION)),str(" · arm64 + x86_64 · macOS 13+")),rect(26,92,w-52,22),13.5,accent());
  const char* buildNote=T("Local ad-hoc build: no Developer ID and no Apple notarization.\nProfile compatibility depends on the version of the launched app.\nAfter a rebuild macOS may ask for Accessibility access again.","Локальная ad-hoc сборка: без Developer ID и нотариального заверения Apple.\nСовместимость профилей зависит от версии запускаемого приложения.\nПосле пересборки macOS может заново запросить Универсальный доступ.");
  label(box,buildNote,rect(26,126,w-52,62),12.5,muted());
  label(box,T("This separates data; it is not a security sandbox.","Это разделение данных, а не песочница безопасности."),rect(26,206,w-52,22),13,ink());
  double by=374;button(rootView,T("User guide","Инструкция (EN)"),"showHelp:",rect(x,by,140,34));button(rootView,T("Diagnostics…","Диагностика…"),"diagnostics:",rect(x+148,by,150,34));button(rootView,T("Data folder","Папка данных"),"openData:",rect(x+306,by,150,34));
  if(a)styledButton(deckButtonClass,rootView,T("Remove selected app…","Убрать выбранное приложение…"),"removeApp:",rect(x+464,by,270,34),-1,"danger");
  label(rootView,T("⌃⌥Space — side panel; ⌃⌥1–8 — profiles in its order (global, from any app).\nChange the order by dragging a row in the panel or with “•••” → “Move up / down”.\n⌘1–9 — launch / focus a profile while the AppDeck window is active. The panel never records the screen.","⌃⌥Space — правая панель; ⌃⌥1–8 — профили по её порядку (глобально, из любого приложения).\nПорядок меняется перетаскиванием строки в панели или через «•••» → «Переместить выше / ниже».\n⌘1–9 — запуск / фокус профиля, когда активно окно AppDeck. Панель не записывает экран."),rect(x,by+56,w,62),12.5,muted());
 }
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
   send<void>(status,"setTextColor:",restarting(p)||failed?warnColor():active?liveColor():muted());
   if(Obj dot=get(ref,"dot"))statusDotColor(dot,restarting(p)||failed?warnColor():active?liveColor():faint(),active);send<void>(status,"setToolTip:",truth(get(p,"syncPending"))?str(T("The shared list and automations apply after this instance fully restarts.","Общий список и автоматизации применятся после полного перезапуска этого экземпляра.")):get(p,"lastError"));
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
 send<void>(send(edgeShade,"layer"),"setBackgroundColor:",send(color(.09,.10,.13),"CGColor"));
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
 send<void>(window,"setTitle:",str("AppDeck"));send<void>(window,"setReleasedWhenClosed:",false);send<void>(window,"setMinSize:",Extent{1040,720});send<void>(window,"setDelegate:",controller);
 send<void>(window,"setOpaque:",false);send<void>(window,"setBackgroundColor:",send(cls("NSColor"),"clearColor"));send<void>(window,"setFrameAutosaveName:",str("AppDeckMainWindow"));
 // The window is glass: a behind-window blur (HUD material, like the side panel) under every page; pages add their own veils.
 Obj glass=send(send(cls("NSVisualEffectView"),"alloc"),"initWithFrame:",rect(0,0,1180,780));send<void>(glass,"setMaterial:",(Int)13);send<void>(glass,"setBlendingMode:",(Int)0);
 send<void>(glass,"setState:",(Int)1);send<void>(glass,"setAutoresizingMask:",(UInt)(2|16));send<void>(window,"setContentView:",glass);drop(glass);
 rootView=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,1180,780));send<void>(rootView,"setAutoresizingMask:",(UInt)(2|16));
 send<void>(rootView,"setWantsLayer:",true);send<void>(glass,"addSubview:",rootView);drop(rootView);
 if(!previewMode){
  Obj statusBar=send(cls("NSStatusBar"),"systemStatusBar");statusItem=keep(send(statusBar,"statusItemWithLength:",-1.0));Obj statusButton=send(statusItem,"button");
  Obj icon=send(cls("NSImage"),"imageWithSystemSymbolName:accessibilityDescription:",str("square.grid.2x2"),str("AppDeck"));if(icon){send<void>(icon,"setTemplate:",true);send<void>(statusButton,"setImage:",icon);}else send<void>(statusButton,"setTitle:",str("AD"));
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
