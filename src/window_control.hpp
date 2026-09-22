// Included inside main.cpp's private namespace. Only documented Accessibility API.
#include "ax_api.hpp"
WindowAPI windowAPI;
struct SavedWindow { Obj window=nullptr,profileId=nullptr;double started=0;Point point{};Extent size{}; };
SavedWindow savedWindows[4];unsigned savedWindowCount=0;
void clearSavedWindows(){for(unsigned i=0;i<savedWindowCount;++i){if(savedWindows[i].window)windowAPI.release(savedWindows[i].window);drop(savedWindows[i].profileId);savedWindows[i]={};}savedWindowCount=0;}
// A click on the macOS Dock icon sends the ordinary "reopen" Apple event (aevt/rapp). An app whose last
// window was closed with the red button keeps running without windows; activating such a process
// shows nothing, reopening makes it create its window again. With windows present it is a no-op.
// The event is addressed to this very process, so other instances of the same bundle are untouched.
void reopen(int pid){
 if(pid<=0)return;Obj target=send(cls("NSAppleEventDescriptor"),"descriptorWithProcessIdentifier:",pid);if(!target)return;
 Obj event=send(cls("NSAppleEventDescriptor"),"appleEventWithEventClass:eventID:targetDescriptor:returnID:transactionID:",(unsigned)0x61657674,(unsigned)0x72617070,target,(short)-1,(int)0);
 Obj error=nullptr;if(event)send(event,"sendEventWithOptions:timeout:error:",(UInt)(0x1|0x10),2.0,&error); // kAENoReply | kAENeverInteract
 if(error)note("Reopen event was not delivered (Automation permission may be required).");
}
void focus(Obj r){
 if(!r)return;send<bool>(r,"unhide");reopen(send<int>(r,"processIdentifier"));
 // No focus-fighting loop and no private WindowServer window-level calls.
 if(windowAPI.permission(false)){Obj w=windowAPI.mainWindow(send<int>(r,"processIdentifier"));if(w){windowAPI.set(w,str("AXMinimized"),boolean(false));windowAPI.perform(w,str("AXRaise"));windowAPI.release(w);}}
 send<bool>(r,"activateWithOptions:",(UInt)3);
}
Obj pointerScreen(){
 Point p=send<Point>(cls("NSEvent"),"mouseLocation");Obj screens=send(cls("NSScreen"),"screens");
 for(UInt i=0;i<count(screens);++i){Rect f=getRect(at(screens,i),"frame");if(p.x>=f.origin.x&&p.y>=f.origin.y&&p.x<f.origin.x+f.size.width&&p.y<f.origin.y+f.size.height)return at(screens,i);}
 return send(cls("NSScreen"),"mainScreen");
}
void restoreWindowsAction(Obj,Sel,Obj){
 if(!savedWindowCount){showError(str(T("No saved layout","Нет сохранённой раскладки")),str(T("Use 2×2 first. Restoring is available until AppDeck quits.","Сначала используйте 2×2. Возврат доступен до закрытия AppDeck.")));return;}
 if(!windowAPI.permission(true)){showError(str(T("Accessibility access required","Нужен Универсальный доступ")),str(T("Allow AppDeck to control windows in macOS System Settings.","Разрешите AppDeck управлять окнами в настройках macOS.")));return;}
 unsigned moved=0,eligible=0;
 for(unsigned i=0;i<savedWindowCount;++i){auto& s=savedWindows[i];Obj p=nullptr;
  for(UInt j=0;j<count(profiles);++j)if(same(get(at(profiles,j),"id"),s.profileId)){p=at(profiles,j);break;}
  if(p&&running(p)&&send<double>(get(p,"started"),"doubleValue")==s.started){++eligible;if(windowAPI.move(s.window,s.point,s.size))++moved;}
 }
 if(moved<eligible)showError(str(T("Some windows were not restored","Часть окон не восстановлена")),str(T("A window may have been closed, entered full screen or changed its size limits.","Окно могло быть закрыто, перейти в fullscreen или изменить ограничения размера.")));
 clearSavedWindows();note("Previous window rectangles restored where the same process/window still exists.");
}
void tileAction(Obj,Sel,Obj){
 if(!windowAPI.permission(true)){showError(str(T("Accessibility access required","Нужен Универсальный доступ")),str(T("System Settings → Privacy & Security → Accessibility → AppDeck. Then try 2×2 again. Screen recording is not needed.","Системные настройки → Конфиденциальность и безопасность → Универсальный доступ → AppDeck. Затем повторите 2×2. Запись экрана не нужна.")));return;}
 Obj ps=dockProfiles(),live=array();for(UInt i=0;i<count(ps)&&count(live)<4;++i)if(running(at(ps,i)))add(live,at(ps,i)); // the first four running, in the user's order
 if(!count(live)){showError(str(T("No running windows","Нет запущенных окон")),str(T("Launch the profiles you need. 2×2 arranges the first four running profiles in side panel order.","Запустите нужные профили. 2×2 размещает первые четыре запущенных профиля по порядку правой панели.")));return;}
 Obj screen=edgeTargetScreen();if(!screen)screen=pointerScreen();if(!screen)return;
 Rect vf=getRect(screen,"visibleFrame");Obj screens=send(cls("NSScreen"),"screens");if(!count(screens))return;Rect primary=getRect(at(screens,0),"frame");
 deck::Box screenAX{vf.origin.x,primary.origin.y+primary.size.height-vf.origin.y-vf.size.height,vf.size.width,vf.size.height};
 clearSavedWindows();UInt n=count(live);unsigned moved=0;
 for(UInt i=0;i<n;++i){Obj p=at(live,i),r=running(p);if(!r)continue;send<bool>(r,"unhide");
  Obj w=windowAPI.mainWindow((int)integer(get(p,"pid")));if(!w)continue;
  Point oldP{};Extent oldS{};if(windowAPI.flag(w,"AXFullScreen")||!windowAPI.geometry(w,oldP,oldS)){windowAPI.release(w);continue;}
  savedWindows[savedWindowCount++]={w,keep(get(p,"id")),send<double>(get(p,"started"),"doubleValue"),oldP,oldS};
  deck::Box b=deck::tileBox(screenAX,(unsigned)n,(unsigned)i);
  if(windowAPI.move(w,{b.x,b.y},{b.w,b.h}))++moved;
 }
 setEdgeVisible(false);send<void>(window,"orderOut:",(Obj)nullptr);
 if(moved<n)showError(str(T("The app limited the layout","Раскладка ограничена приложением")),str(T("Some windows did not accept the cell size. A common cause is the minimum width of Codex or full screen. Use a larger screen or fewer windows. The previous layout can be restored from the AppDeck menu.","Некоторые окна не приняли размер ячейки. Частая причина — минимальная ширина Codex или fullscreen. Используйте больший экран или меньше окон. Предыдущую раскладку можно вернуть через меню AppDeck.")));
 note("Tiling requested and actual Accessibility geometry checked.");
}
