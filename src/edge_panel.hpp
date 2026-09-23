// Native, nonactivating translucent edge controller. No screen capture or injection.
Obj edgePanel=nullptr,edgeEffect=nullptr,edgeContent=nullptr,edgeRefs=nullptr,edgeRipple=nullptr,edgeShade=nullptr;
Obj edgeScreen=nullptr,edgeTimer=nullptr;
Class edgePanelClass=nullptr,edgeRowButtonClass=nullptr;
bool edgeWanted=false,edgeRebuild=true,edgeReducedMotion=false;
double edgeProgress=0,edgeLastTime=0;
Rect edgeFullFrame{};
Obj edgeMuted(){return muted();}
constexpr double edgeCorner=22;
// The dock lists every profile of every app in the user's order (dockProfiles). At most six rows are
// visible, fewer on a short screen (edge_policy.hpp); the rest of the list scrolls.
Int edgeRowsNow=4;
// The visible list, its document view and the mask that fades an edge while rows are hidden behind it.
// Owned by the view hierarchy and rebuilt with the dock; the scroll offset survives rebuilds.
Obj edgeScroll=nullptr,edgeList=nullptr,edgeFade=nullptr;
double edgeScrollY=0;bool edgeScrollHome=false,edgeDragging=false,edgeFadeTopFirst=false;
constexpr double edgeFadeLength=16,edgeFadeFloor=.22;
Obj edgeTargetScreen(){return edgeWanted&&edgeScreen?edgeScreen:pointerScreen();}
bool neverKey(Obj,Sel){return false;}
Obj edgeButton(Obj parent,const char* title,const char* action,Rect frame,Int tag=-1,bool overlay=false,const char* icon=nullptr){
 return styledButton(edgeButtonClass,parent,title,action,frame,tag,overlay?"overlay":"secondary",overlay?14.0:frame.size.height/2,11.5,icon);
}
// One cell of the panel's toolbar: glyph above caption, composed by hand (NSButton's own image/title
// layout lets them collide at this size), with a transparent hit area over the whole cell.
Obj edgeAction(Obj parent,const char* glyph,const char* title,const char* tip,const char* action,Rect f){
 double w=f.size.width;imageView(parent,symbol(glyph,14),rect(f.origin.x+(w-18)/2,f.origin.y+9,18,18),ink());
 Obj caption=label(parent,title,rect(f.origin.x,f.origin.y+30,w,14),10.5,muted(),.23);send<void>(caption,"setAlignment:",alignCenter);
 Obj b=styledButton(edgeButtonClass,parent,"",action,f,-1,"overlay",14);
 send<void>(b,"setToolTip:",str(tip));send<void>(b,"setAccessibilityLabel:",str(tip));return b;
}
// A quiet line with its key names picked out: "⌃⌥Space panel · ⌃⌥1–6 profiles".
Obj edgeHint(Obj parent,Rect f,const char* const* parts,UInt n){
 Obj text=make("NSMutableAttributedString");Obj style=make("NSMutableParagraphStyle");send<void>(style,"setAlignment:",alignCenter);
 for(UInt i=0;i<n;++i){bool key=i%2==0;Obj a=dict();put(a,"NSFont",send(cls("NSFont"),"systemFontOfSize:weight:",10.5,key?.3:0.0));put(a,"NSColor",key?muted():faint());put(a,"NSParagraphStyle",style);
  Obj piece=send(send(cls("NSAttributedString"),"alloc"),"initWithString:attributes:",str(parts[i]),a);send<void>(text,"appendAttributedString:",piece);drop(piece);}
 Obj v=label(parent,"",f,10.5,faint(),0);send<void>(v,"setAttributedStringValue:",text);drop(text);drop(style);return v;
}
Obj edgeClip(){return edgeScroll?send(edgeScroll,"contentView"):nullptr;}
double edgeScrollOffset(){Obj clip=edgeClip();return clip?getRect(clip,"bounds").origin.y:0;}
// Clean at rest: an edge shades only while it cuts through a row (deck::edgeCut), so the list opens
// and ends without any shade and rows slide into a soft edge while it scrolls. Follows the content
// exactly, without implicit animation.
void edgeUpdateFade(){
 Obj clip=edgeClip();if(!clip||!edgeFade)return;Rect view=getRect(clip,"bounds");
 Obj top=send(color(0,0,0,1-deck::edgeCut(view.origin.y)*(1-edgeFadeFloor)),"CGColor"),bottom=send(color(0,0,0,1-deck::edgeCut(view.origin.y+view.size.height)*(1-edgeFadeFloor)),"CGColor");
 Obj colors=array(),solid=send(color(0,0,0,1),"CGColor");add(colors,edgeFadeTopFirst?top:bottom);add(colors,solid);add(colors,solid);add(colors,edgeFadeTopFirst?bottom:top);
 send<void>(cls("CATransaction"),"begin");send<void>(cls("CATransaction"),"setDisableActions:",true);send<void>(edgeFade,"setColors:",colors);send<void>(cls("CATransaction"),"commit");
}
void edgeScrolledAction(Obj,Sel,Obj){edgeUpdateFade();}
void edgeForgetList(){
 if(Obj clip=edgeClip())send<void>(send(cls("NSNotificationCenter"),"defaultCenter"),"removeObserver:name:object:",controller,str("NSViewBoundsDidChangeNotification"),clip);
 edgeScroll=edgeList=edgeFade=nullptr;
}
void edgeLayout(){
 if(!edgeScreen)return;Rect vf=getRect(edgeScreen,"visibleFrame");Int before=edgeRowsNow;
 edgeRowsNow=(Int)deck::edgeRows((unsigned)count(dockProfiles()),vf.size.height);if(before!=edgeRowsNow)edgeRebuild=true;
 auto b=deck::edgeBounds({vf.origin.x,vf.origin.y,vf.size.width,vf.size.height},deck::edgeWidth,deck::edgeHeight((unsigned)edgeRowsNow));
 edgeFullFrame=rect(b.x,b.y,b.w,b.h);
}
void rebuildEdgeContent(){
 if(!edgeContent||edgeDragging)return; // a drag in progress owns the rows; the dock is rebuilt when it ends
 double keepY=edgeScrollHome?0:edgeScroll?edgeScrollOffset():edgeScrollY;edgeScrollHome=false;edgeForgetList();
 Obj old=send(send(edgeContent,"subviews"),"copy");for(UInt i=0;i<count(old);++i)send<void>(at(old,i),"removeFromSuperview");drop(old);
 if(edgeRefs)drop(edgeRefs);edgeRefs=keep(array());
 const double W=deck::edgeWidth,pad=12,rowW=W-pad*2,rowH=deck::edgeRowHeight,step=deck::edgeStep;
 Obj all=dockProfiles();Obj a=currentApp();UInt n=count(all),rows=n?n:1;bool anyLimits=false;
 for(UInt i=0;i<count(apps);++i)if(usageEnabled(at(apps,i)))anyLimits=true;
 // Header: title and counts on the left edge, Limits and Close ending on the right edge.
 kern(label(edgeContent,"AppDeck",rect(pad,14,120,20),15,ink(),.4),-.15);
 Obj closeButton=edgeButton(edgeContent,"","toggleEdge:",rect(W-pad-26,18,26,26),-1,false,"xmark");send<void>(closeButton,"setToolTip:",str(T("Hide panel  ⌃⌥Space","Скрыть панель  ⌃⌥Space")));send<void>(closeButton,"setAccessibilityLabel:",str(T("Hide panel","Скрыть панель")));
 double headerRight=W-pad-26-6;
 if(anyLimits){const char* t=T("Limits","Лимиты");double bw=(double)(Int)(textWidth(t,11.5,.3)+42);Obj again=edgeButton(edgeContent,t,"usageRefreshAll:",rect(headerRight-bw,18,bw,26),-1,false,"arrow.clockwise");
  send<void>(again,"setToolTip:",str(T("Check the limits of all profiles now. On its own, AppDeck asks each account at most once an hour, with 5 minutes between profiles.","Проверить лимиты всех профилей сейчас. Сам AppDeck спрашивает каждый аккаунт не чаще раза в час, с паузой 5 минут между профилями.")));headerRight-=bw+6;}
 Obj sub=cat(count(apps)>1?formatInt(T("%ld apps","%ld прил."),(Int)count(apps)):a?get(a,"name"):str(T("No apps","Нет приложений")),formatInt(n==1?T(" · %ld profile"," · %ld проф."):T(" · %ld profiles"," · %ld проф."),(Int)n));
 labelObj(edgeContent,sub,rect(pad,36,headerRight-pad-4,14),11,faint(),0);
 // One column in the user's order, top to bottom; the rows that do not fit scroll. A row's place is
 // also its shortcut (⌃⌥1…8). Drag a row, or use its menu, to change the order.
 double listH=deck::edgeListHeight((unsigned)edgeRowsNow);
 Obj scroll=send(send(cls("NSScrollView"),"alloc"),"initWithFrame:",rect(pad,62,rowW,listH));
 send<void>(scroll,"setDrawsBackground:",false);send<void>(scroll,"setHasVerticalScroller:",true);send<void>(scroll,"setAutohidesScrollers:",true);
 send<void>(scroll,"setScrollerStyle:",(Int)1);send<void>(scroll,"setScrollerKnobStyle:",(Int)2);send<void>(scroll,"setHorizontalScrollElasticity:",(Int)1); // overlay, light knob
 send<void>(scroll,"setWantsLayer:",true);
 edgeList=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,rowW,deck::edgeListHeight((unsigned)rows)));send<void>(scroll,"setDocumentView:",edgeList);drop(edgeList);
 send<void>(edgeContent,"addSubview:",scroll);drop(scroll);edgeScroll=scroll;
 for(UInt i=0;i<rows;++i){Rect frame=rect(0,i*step,rowW,rowH);
  Obj c=panel(edgeList,frame,surface(),14);Obj layer=send(c,"layer");send<void>(layer,"setBorderWidth:",1.0);send<void>(layer,"setBorderColor:",send(fill(0),"CGColor"));
  if(i<n){Obj p=at(all,i);bool hotkey=i<deck::edgeHotkeys;
   // Identity: the app icon with the profile's colour as a badge; the place number as a key cap.
   appIcon(c,get(appFor(p),"path"),rect(10,13,32,32));identityBadge(c,rect(10,13,32,32),accentFor(p),10,hex(0x2C2C2F));
   bool limits=usageEnabled(appFor(p));double textRight=rowW-10-(hotkey?28:0);
   labelObj(c,get(p,"name"),rect(52,10,textRight-52,18),13,ink(),.3);
   Obj dot=statusDot(c,rect(52,33,6,6));Obj st=label(c,T("Stopped","Остановлен"),rect(63,28,rowW-63-10-(limits?46:0),15),11,muted(),.23);Obj pct=nullptr,track=nullptr,fillBars=nullptr;
   if(limits){pct=label(c,"",rect(rowW-10-44,27,44,17),12,ink(),.3);send<void>(pct,"setFont:",monoFont(12,.3));send<void>(pct,"setAlignment:",alignRight); // on the status line, ending with the meters
    track=array();fillBars=array();for(UInt k=0;k<usageShown;++k){Obj t=panel(c,rect(52,47,10,3),fill(.10),1.5);add(track,t);add(fillBars,panel(t,rect(0,0,0,3),ink(),1.5));}}
   if(hotkey)keycap(c,utf8(formatInt("%ld",(Int)i+1)),rect(rowW-30,9,20,20),10.5);
   Obj b=styledButton(edgeRowButtonClass,c,"","edgeFocus:",rect(0,0,rowW,rowH),profileIndex(p),"overlay",14,10);
   Obj tip=cat(cat(get(p,"name"),hotkey?formatInt(T(" — launch or bring its window forward  ·  ⌃⌥%ld"," — запустить или поднять окно  ·  ⌃⌥%ld"),(Int)i+1):str(T(" — launch or bring its window forward"," — запустить или поднять окно"))),str(T("\nOrder: drag the row up or down","\nПорядок: перетащите строку выше или ниже")));
   send<void>(b,"setToolTip:",tip);send<void>(b,"setAccessibilityLabel:",cat(str(T("Open ","Открыть ")),get(p,"name")));
   // Right click: the management actions of the card and the order, without leaving the dock.
   {Int tag=profileIndex(p);Obj menu=make("NSMenu");send<void>(menu,"setAutoenablesItems:",false);menuItem(menu,T("Open / launch","Открыть / запустить"),"launchProfile:",tag);
    menuItem(menu,T("Restart…","Перезапустить…"),"restartProfile:",tag);menuItem(menu,T("Close instance…","Закрыть экземпляр…"),"stopProfile:",tag);
    if(limits){separator(menu);menuItem(menu,T("Refresh limit","Обновить лимит"),"usageRefresh:",tag);}
    separator(menu);send<void>(menuItem(menu,T("Move up","Переместить выше"),"edgeMoveUp:",tag),"setEnabled:",i>0);send<void>(menuItem(menu,T("Move down","Переместить ниже"),"edgeMoveDown:",tag),"setEnabled:",i+1<n);
    send<void>(menuItem(menu,T("Move to top","Переместить в начало"),"edgeMoveFirst:",tag),"setEnabled:",i>0);send<void>(b,"setMenu:",menu);drop(menu);}
   Obj ref=dict();put(ref,"id",get(p,"id"));put(ref,"row",c);put(ref,"status",st);put(ref,"dot",dot);put(ref,"layer",layer);put(ref,"button",b);put(ref,"pct",pct);put(ref,"track",track);put(ref,"fill",fillBars);put(ref,"width",real(rowW-52-10));
   put(ref,"tip",tip);add(edgeRefs,ref);
  }else{Obj none=label(c,T("No profiles yet — add them in the AppDeck window","Профилей пока нет — добавьте их в окне AppDeck"),rect(14,12,rowW-28,34),11.5,edgeMuted());send<void>(none,"setAlignment:",alignCenter);
   Obj b=edgeButton(c,"","showWindow:",rect(0,0,rowW,rowH),-1,true);send<void>(b,"setAccessibilityLabel:",str(T("Open the AppDeck main window","Открыть главное окно AppDeck")));}
 }
 // The scroll position survives rebuilds while the dock stays open; opening the dock starts at the top.
 double range=deck::edgeScrollRange((unsigned)rows,(unsigned)edgeRowsNow);edgeScrollY=deck::clamp(keepY,0,range);
 Obj clip=send(scroll,"contentView");send<void>(clip,"scrollToPoint:",Point{0,edgeScrollY});send<void>(scroll,"reflectScrolledClipView:",clip);
 if(range>0){send<void>(cls("CATransaction"),"begin");send<void>(cls("CATransaction"),"setDisableActions:",true);
  edgeFade=send(cls("CAGradientLayer"),"layer");send<void>(edgeFade,"setFrame:",rect(0,0,rowW,listH));
  Obj stops=array();add(stops,real(0));add(stops,real(edgeFadeLength/listH));add(stops,real(1-edgeFadeLength/listH));add(stops,real(1));send<void>(edgeFade,"setLocations:",stops);
  Obj opaque=array();for(int k=0;k<4;++k)add(opaque,send(color(0,0,0,1),"CGColor"));send<void>(edgeFade,"setColors:",opaque); // never an empty (invisible) mask
  send<void>(send(scroll,"layer"),"setMask:",edgeFade);
  // Which end of the gradient is the top depends on how AppKit flips the layers of this hierarchy: ask Core Animation.
  edgeFadeTopFirst=send<bool>(edgeFade,"contentsAreFlipped");edgeUpdateFade();send<void>(cls("CATransaction"),"commit");
  send<void>(clip,"setPostsBoundsChangedNotifications:",true);
  send<void>(send(cls("NSNotificationCenter"),"defaultCenter"),"addObserver:selector:name:object:",controller,sel("edgeScrolled:"),str("NSViewBoundsDidChangeNotification"),clip);}
 // Toolbar: one glass bar with four equal cells and hairline separators.
 double y=62+listH+12;
 Obj bar=card(edgeContent,rect(pad,y,rowW,52),14,fill(.05));double cell=(rowW-3)/4;
 const char* glyphs[]={"square.grid.2x2","eye.slash","arrow.uturn.backward","macwindow"};
 const char* titles[]={"2×2",T("Hide","Скрыть"),T("Restore","Вернуть"),"AppDeck"};
 const char* tips[]={T("Arrange the first four running profiles of the list in a 2×2 grid","Разложить первые четыре запущенных профиля списка сеткой 2×2"),T("Hide the windows of all profiles","Скрыть окна всех профилей группы"),T("Restore the window positions from before the layout","Вернуть расположение окон до раскладки"),T("Open the AppDeck main window","Открыть главное окно AppDeck")};
 const char* actions[]={"tileWindows:","hideProfiles:","restoreWindows:","showWindow:"};
 for(int k=0;k<4;++k){double cx=k*(cell+1);if(k)panel(bar,rect(cx-1,12,1,28),hairline(),0);edgeAction(bar,glyphs[k],titles[k],tips[k],actions[k],rect(cx,0,cell,52));}
 Int keys=(Int)(n<deck::edgeHotkeys?n:deck::edgeHotkeys);
 char range1[24];snprintf(range1,sizeof range1,keys>1?"⌃⌥1–%ld":"⌃⌥1",keys);
 const char* hint[]={"⌃⌥Space",T(" panel  ·  "," панель  ·  "),range1,keys>1?T(" profiles"," профили"):T(" profile"," профиль")};
 edgeHint(edgeContent,rect(pad,y+64,rowW,15),hint,4);
 edgeRebuild=false;
}
void renderEdge();
void edgeRefresh(){
 if(!edgePanel||(!edgeWanted&&edgeProgress<=0)||edgeDragging)return;
 if(edgeRebuild){Rect before=edgeFullFrame;edgeLayout();rebuildEdgeContent(); // rows first: they depend on the number of profiles and the screen
  if(before.size.height!=edgeFullFrame.size.height&&!edgeTimer)renderEdge();}
 for(UInt i=0;i<count(edgeRefs);++i){Obj ref=at(edgeRefs,i),p=nullptr;for(UInt j=0;j<count(profiles);++j)if(same(get(at(profiles,j),"id"),get(ref,"id"))){p=at(profiles,j);break;}
  if(!p){edgeRebuild=true;continue;}Obj r=running(p);bool front=r&&send<bool>(r,"isActive");bool hidden=r&&send<bool>(r,"isHidden");
  bool problem=restarting(p)||(!r&&get(p,"lastError"));
  send<void>(get(ref,"status"),"setStringValue:",str(restarting(p)?T("Restarting…","Перезапуск…"):front?T("Active","Активен"):hidden?T("Hidden","Скрыт"):r?T("Running","Запущен"):get(p,"lastError")?T("Failed to start","Не запустился"):T("Stopped","Остановлен")));
  // The words stay quiet; the light carries the state: green running, amber trouble, an empty ring when stopped.
  send<void>(get(ref,"status"),"setTextColor:",problem?warnColor():muted());
  if(Obj dot=get(ref,"dot"))statusDotColor(dot,problem?warnColor():r&&!hidden?liveColor():r?muted():dim(),r&&!hidden,!r||hidden);
  send<void>(get(ref,"layer"),"setBorderColor:",send(front?fill(.12):fill(0),"CGColor"));
  send<void>(get(ref,"layer"),"setBackgroundColor:",send(front?fill(.10):surface(),"CGColor"));
  if(Obj pct=get(ref,"pct")){Obj w=usageTightest(p);bool stale=w&&get(usageOf(p),"error"),best=usageIsBest(p);int left=w?usageRemaining(w):0; // one number = the limit closest to its wall
   send<void>(pct,"setStringValue:",w?formatInt("%ld%%",(Int)left):str(usageAsking(p)?"…":"—"));send<void>(pct,"setTextColor:",!w||stale?faint():usageColor(left));
   // The account with the most weekly headroom is the one to switch to: its number is set a little heavier.
   send<void>(pct,"setFont:",monoFont(12,best?.5:.3));
   // One thin segment per limit window, in the card's order (week · short · per-model).
   UInt n=usageWindowCount(p);double total=send<double>(get(ref,"width"),"doubleValue"),gap=4,segment=n?(total-gap*(n-1))/n:total;
   for(UInt k=0;k<usageShown;++k){Obj t=at(get(ref,"track"),k),f=at(get(ref,"fill"),k);usageHide(t,k>=n);if(k>=n)continue;int part=usageRemaining(usageWindowAt(p,k));
    send<void>(t,"setFrame:",rect(52+k*(segment+gap),47,segment,3));send<void>(f,"setFrame:",rect(0,0,segment*deck::usageFill(part),3));send<void>(send(f,"layer"),"setBackgroundColor:",send(stale?faint():usageColor(part),"CGColor"));}
   send<void>(get(ref,"button"),"setToolTip:",cat(cat(get(ref,"tip"),str("\n")),usageSummary(p)));}
 }
}
// Drag a row to change the order. A press that moves less than 4 pt stays an ordinary click and a
// control-click opens the row's menu. While dragging, the row follows the pointer, the other rows make
// room and the list scrolls by itself near its edges; the order is saved when the button is released.
UInt edgeRowIndex(Obj row){for(UInt i=0;i<count(edgeRefs);++i)if(get(at(edgeRefs,i),"row")==row)return i;return count(edgeRefs);}
void edgeLift(Obj row){
 Obj layer=send(row,"layer");send<void>(layer,"setZPosition:",10.0);
 send<void>(layer,"setBackgroundColor:",send(color(.25,.25,.26,.96),"CGColor"));send<void>(layer,"setBorderColor:",send(fill(.18),"CGColor"));
 send<void>(layer,"setShadowColor:",send(color(0,0,0,1),"CGColor"));send<void>(layer,"setShadowOpacity:",(float).5);send<void>(layer,"setShadowRadius:",8.0);send<void>(layer,"setShadowOffset:",Extent{0,0});
}
// Moves every row a part of the way to its place (the dragged one too once released); true = all arrived.
bool edgeGlide(double* ys,UInt n,UInt from,UInt slot,double rate,bool released){
 bool done=true;
 for(UInt i=0;i<n;++i){
  if(i!=from||released){double target=deck::edgeShiftedSlot((unsigned)i,(unsigned)from,(unsigned)slot)*deck::edgeStep,d=target-ys[i];
   ys[i]=d<.5&&d>-.5?target:ys[i]+d*rate;if(ys[i]!=target)done=false;}
  send<void>(get(at(edgeRefs,i),"row"),"setFrameOrigin:",Point{0,ys[i]});
 }
 return done;
}
void edgeRowMouseDown(Obj self,Sel,Obj event){
 Pool pool;
 if(send<UInt>(event,"modifierFlags")&(1UL<<18)){Obj m=send(self,"menu");if(m)send<void>(cls("NSMenu"),"popUpContextMenu:withEvent:forView:",m,event,self);return;}
 Obj row=send(self,"superview"),list=edgeList;UInt n=count(edgeRefs),from=edgeRowIndex(row),slot=from;
 Obj mode=publicConstant(appKit,"NSEventTrackingRunLoopMode");if(!mode)mode=str("NSEventTrackingRunLoopMode");
 const unsigned long long mask=(1ULL<<2)|(1ULL<<6); // left mouse up | left mouse dragged
 Point start=send<Point>(event,"locationInWindow"),last=start;bool dragging=false,released=false;double grab=0,*ys=nullptr;
 keep(self);
 while(!released){Pool step;
  Obj until=dragging?send(cls("NSDate"),"dateWithTimeIntervalSinceNow:",1.0/60):send(cls("NSDate"),"distantFuture");
  Obj e=send(app,"nextEventMatchingMask:untilDate:inMode:dequeue:",mask,until,mode,true);
  if(e){last=send<Point>(e,"locationInWindow");if(send<UInt>(e,"type")==2)released=true;}
  if(!dragging){double dx=last.x-start.x,dy=last.y-start.y;
   if(released||n<2||from>=n||!list||dx*dx+dy*dy<16)continue;
   ys=(double*)calloc(n,sizeof(double));if(!ys)continue;
   for(UInt i=0;i<n;++i)ys[i]=i*deck::edgeStep;
   grab=send<Point>(list,"convertPoint:fromView:",start,(Obj)nullptr).y-ys[from];dragging=edgeDragging=true;edgeLift(row);
  }
  // Near the top or bottom of the visible part the list scrolls under the pointer.
  Obj clip=edgeClip();Rect view=getRect(clip,"bounds");Point pt=send<Point>(list,"convertPoint:fromView:",last,(Obj)nullptr);
  double speed=deck::edgeAutoScroll(pt.y-view.origin.y,view.size.height),range=getRect(list,"frame").size.height-view.size.height;
  if(speed!=0&&range>0){double to=deck::clamp(view.origin.y+speed,0,range);
   if(to!=view.origin.y){send<void>(clip,"scrollToPoint:",Point{0,to});send<void>(edgeScroll,"reflectScrolledClipView:",clip);pt=send<Point>(list,"convertPoint:fromView:",last,(Obj)nullptr);}}
  ys[from]=deck::clamp(pt.y-grab,0,(n-1)*deck::edgeStep);slot=deck::edgeDropSlot(ys[from],(unsigned)n);
  edgeGlide(ys,n,from,slot,.35,false);
 }
 if(!dragging){ // an ordinary click
  send<bool>(app,"sendAction:to:from:",send<Sel>(self,"action"),send(self,"target"),self);drop(self);return;}
 // Glide into place (about 0.15 s); the new order is applied right after this event, which rebuilds the dock.
 for(int k=0;k<14;++k){Pool step;send(app,"nextEventMatchingMask:untilDate:inMode:dequeue:",mask,send(cls("NSDate"),"dateWithTimeIntervalSinceNow:",1.0/60),mode,true);
  if(edgeGlide(ys,n,from,slot,.45,true))break;}
 free(ys);edgeDragging=false;
 Obj dropped=dict();put(dropped,"id",get(at(edgeRefs,from),"id"));put(dropped,"slot",num((Int)slot));
 send<void>(controller,"performSelector:withObject:afterDelay:",sel("edgeDropped:"),dropped,0.0);drop(self);
}
void edgeDroppedAction(Obj,Sel,Obj dropped){
 Obj p=nullptr;for(UInt i=0;i<count(profiles);++i)if(same(get(at(profiles,i),"id"),get(dropped,"id")))p=at(profiles,i);
 // Dropped back on its own place (or the profile is gone): only the lifted row needs redrawing.
 if(!p||!moveProfileInList(p,dockProfiles(),integer(get(dropped,"slot")))){edgeRebuild=true;edgeRefresh();}
}
// The same moves from a row's menu (and for VoiceOver users, who cannot drag).
Int edgePlace(Obj p){return (Int)send<UInt>(dockProfiles(),"indexOfObjectIdenticalTo:",p);}
void edgeMoveUpAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(p)moveProfileInList(p,dockProfiles(),edgePlace(p)-1);}
void edgeMoveDownAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(p)moveProfileInList(p,dockProfiles(),edgePlace(p)+1);}
void edgeMoveFirstAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(p)moveProfileInList(p,dockProfiles(),0);}
void ensureEdge(){
 if(edgePanel)return;
 edgePanelClass=objc_allocateClassPair((Class)cls("NSPanel"),"AppDeckEdgePanel",0);
 method(edgePanelClass,"canBecomeKeyWindow",neverKey,"B@:");method(edgePanelClass,"canBecomeMainWindow",neverKey,"B@:");objc_registerClassPair(edgePanelClass);
 const double W=deck::edgeWidth,H=deck::edgeHeight(deck::edgeMaxRows);
 edgePanel=send(send((Obj)edgePanelClass,"alloc"),"initWithContentRect:styleMask:backing:defer:",rect(0,0,W,H),(UInt)128,(UInt)2,false);
 send<void>(edgePanel,"setTitle:",str("AppDeck Edge"));send<void>(edgePanel,"setReleasedWhenClosed:",false);send<void>(edgePanel,"setOpaque:",false);
 send<void>(edgePanel,"setBackgroundColor:",send(cls("NSColor"),"clearColor"));send<void>(edgePanel,"setHasShadow:",true);send<void>(edgePanel,"setHidesOnDeactivate:",false);
 send<void>(edgePanel,"setFloatingPanel:",true);send<void>(edgePanel,"setBecomesKeyOnlyIfNeeded:",true);send<void>(edgePanel,"setLevel:",(Int)3);
 send<void>(edgePanel,"setCollectionBehavior:",(UInt)((1UL<<0)|(1UL<<6)|(1UL<<8)));send<void>(edgePanel,"setAnimationBehavior:",(Int)2);
 edgeEffect=send(send(cls("NSVisualEffectView"),"alloc"),"initWithFrame:",rect(0,0,W,H));
 send<void>(edgeEffect,"setMaterial:",(Int)13);send<void>(edgeEffect,"setBlendingMode:",(Int)0);send<void>(edgeEffect,"setState:",(Int)1);
 send<void>(edgeEffect,"setWantsLayer:",true);send<void>(edgeEffect,"setAutoresizingMask:",(UInt)(2|16));
 // The panel floats off the screen border: all four corners are rounded (continuous), with a light rim.
 Obj layer=send(edgeEffect,"layer");send<void>(layer,"setCornerRadius:",edgeCorner);continuous(layer);send<void>(layer,"setMasksToBounds:",true);send<void>(layer,"setBorderWidth:",1.0);send<void>(layer,"setBorderColor:",send(fill(.12),"CGColor"));
 send<void>(edgePanel,"setContentView:",edgeEffect);drop(edgeEffect);
 edgeShade=panel(edgeEffect,rect(0,0,W,H),color(.118,.118,.129,.55),edgeCorner);send<void>(edgeShade,"setAutoresizingMask:",(UInt)(2|16));
 edgeRipple=panel(edgeEffect,rect(0,0,W,H),fill(.02),edgeCorner);
 send<void>(send(edgeRipple,"layer"),"setBorderWidth:",6.0);send<void>(send(edgeRipple,"layer"),"setBorderColor:",send(fill(.10),"CGColor"));
 edgeContent=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,W,H));send<void>(edgeContent,"setWantsLayer:",true);send<void>(edgeEffect,"addSubview:",edgeContent);drop(edgeContent);
 rebuildEdgeContent();
}
void renderEdge(){
 deck::Box full{edgeFullFrame.origin.x,edgeFullFrame.origin.y,edgeFullFrame.size.width,edgeFullFrame.size.height};
 auto b=deck::edgeFrame(full,edgeProgress,edgeReducedMotion);double alpha=deck::smooth(edgeProgress/.3);
 send<void>(cls("CATransaction"),"begin");send<void>(cls("CATransaction"),"setDisableActions:",true);
 send<void>(edgePanel,"setFrame:display:",rect(b.x,b.y,b.w,b.h),true);send<void>(edgePanel,"setAlphaValue:",alpha);
 send<void>(edgeEffect,"setFrame:",rect(0,0,b.w,b.h));
 // Content keeps its full width and is pinned to the moving left side, so it slides out
 // from behind the screen border while the border itself clips the rest.
 send<void>(edgeContent,"setFrame:",rect(0,0,full.w,full.h));
 send<void>(edgeContent,"setAlphaValue:",edgeReducedMotion?alpha:deck::smooth((edgeProgress-.15)/.6));
 send<void>(edgeRipple,"setFrame:",rect(1,1,b.w-2,b.h-2));send<void>(edgeRipple,"setAlphaValue:",edgeReducedMotion?0.0:4*edgeProgress*(1-edgeProgress));
 send<void>(cls("CATransaction"),"commit");
}
void edgeAnimationTick(Obj,Sel,Obj){
 Pool pool;double now=send<double>(send(cls("NSProcessInfo"),"processInfo"),"systemUptime");double dt=deck::clamp(now-edgeLastTime,0,.05);edgeLastTime=now;
 edgeProgress=deck::clamp(edgeProgress+(edgeWanted?dt:-dt)/(edgeReducedMotion?.14:.42),0,1);renderEdge();
 if((edgeWanted&&edgeProgress>=1)||(!edgeWanted&&edgeProgress<=0)){
  if(!edgeWanted)send<void>(edgePanel,"orderOut:",(Obj)nullptr);send<void>(edgeTimer,"invalidate");drop(edgeTimer);edgeTimer=nullptr;
 }
}
void setEdgeVisible(bool show){
 if(!edgePanel&&!show)return;if(show)ensureEdge();bool opening=show&&!edgeWanted;edgeWanted=show;
 if(show){
  // Select the display before hiding AppDeck, based on the pointer position.
  drop(edgeScreen);edgeScreen=keep(pointerScreen());if(!edgeScreen){edgeWanted=false;return;}edgeLayout();
  edgeReducedMotion=send<bool>(workspace,"accessibilityDisplayShouldReduceMotion");
  bool reducedTransparency=send<bool>(workspace,"accessibilityDisplayShouldReduceTransparency");
  send<void>(send(edgeShade,"layer"),"setBackgroundColor:",send(reducedTransparency?color(.11,.11,.12):color(.118,.118,.129,.55),"CGColor"));
  send<void>(window,"orderOut:",(Obj)nullptr);send<bool>(app,"setActivationPolicy:",(Int)1);
  if(opening)edgeScrollHome=true;edgeRebuild=true;edgeRefresh();renderEdge();send<void>(edgePanel,"orderFrontRegardless");
 }
 if(!edgeTimer){edgeLastTime=send<double>(send(cls("NSProcessInfo"),"processInfo"),"systemUptime");
  edgeTimer=keep(send(cls("NSTimer"),"timerWithTimeInterval:target:selector:userInfo:repeats:",1.0/60,controller,sel("edgeAnimationTick:"),(Obj)nullptr,true));
  Obj mode=publicConstant(appKit,"NSRunLoopCommonModes");
  send<void>(send(cls("NSRunLoop"),"mainRunLoop"),"addTimer:forMode:",edgeTimer,mode?mode:str("kCFRunLoopCommonModes"));
 }
}
void screensChangedAction(Obj,Sel,Obj){if(edgeWanted)setEdgeVisible(true);}
void toggleEdgeAction(Obj,Sel,Obj){setEdgeVisible(!edgeWanted);}
void edgeFocusAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);if(p)launch(p);edgeRefresh();}
void hideProfilesAction(Obj,Sel,Obj){
 Obj ps=dockProfiles();unsigned failed=0;for(UInt i=0;i<count(ps);++i){Obj r=running(at(ps,i));if(r&&!send<bool>(r,"hide"))++failed;}
 edgeRefresh();note("Hide requested for group; no process terminated.");
 if(failed)showError(str(T("Not all windows were hidden","Не все окна скрыты")),str(T("The app refused to hide. No process was terminated.","Приложение отклонило скрытие. Завершение процессов не выполнялось.")));
}
// Public Carbon hotkeys: no event tap, key logging or Input Monitoring permission.
struct HotkeyID {unsigned signature,id;};struct HotkeyEvent {unsigned eventClass,eventKind;};
void* carbon=nullptr;void* hotkeyRefs[9]={};void* hotkeyHandler=nullptr;
int (*unregisterKey)(void*)=nullptr;int (*removeKeyHandler)(void*)=nullptr;
int hotkeyCallback(void*,void* event,void*){
 Pool pool;
 using Get=int(*)(void*,unsigned,unsigned,unsigned*,unsigned long,unsigned long*,void*);auto getParam=(Get)dlsym(carbon,"GetEventParameter");if(!getParam)return -9874;
 HotkeyID id{};if(getParam(event,0x2d2d2d2d,0x686b6964,nullptr,sizeof(id),nullptr,&id)!=0||id.signature!=0x41445032)return -9874;
 if(send(app,"modalWindow"))return 0; // Do not launch a profile behind a confirmation dialog.
 if(id.id==1){toggleEdgeAction(nullptr,nullptr,nullptr);return 0;}
 // ⌃⌥N is place N of the user's order, whether or not that row is scrolled into view.
 if(id.id>=2&&id.id<=9){Obj ps=dockProfiles();UInt n=id.id-2;if(n<count(ps))launch(at(ps,n));edgeRefresh();return 0;}return -9874;
}
void installHotkeys(){
 carbon=dlopen("/System/Library/Frameworks/Carbon.framework/Carbon",2);if(!carbon){note("Global hotkeys unavailable; menu bar remains available.");return;}
 using Target=void*(*)();using Install=int(*)(void*,int(*)(void*,void*,void*),unsigned long,const HotkeyEvent*,void*,void**);
 using Register=int(*)(unsigned,unsigned,HotkeyID,void*,unsigned,void**);
 auto target=(Target)dlsym(carbon,"GetApplicationEventTarget");auto install=(Install)dlsym(carbon,"InstallEventHandler");auto reg=(Register)dlsym(carbon,"RegisterEventHotKey");
 unregisterKey=(decltype(unregisterKey))dlsym(carbon,"UnregisterEventHotKey");removeKeyHandler=(decltype(removeKeyHandler))dlsym(carbon,"RemoveEventHandler");
 if(!target||!install||!reg||!unregisterKey||!removeKeyHandler){note("Global hotkey public API missing.");return;}
 HotkeyEvent type{0x6b657962,5}; // kEventClassKeyboard / kEventHotKeyPressed (0.2.0-preview used 6 = released)
 if(install(target(),hotkeyCallback,1,&type,nullptr,&hotkeyHandler)!=0){note("Global hotkey handler registration failed.");return;}
 unsigned keys[]={49,18,19,20,21,23,22,26,28};unsigned failures=0; // Space, 1…8 (ANSI key codes)
 for(unsigned i=0;i<9;++i)if(reg(keys[i],(1U<<12)|(1U<<11),{0x41445032,i+1},target(),0,&hotkeyRefs[i])!=0)++failures;
 note(failures?"Some global hotkeys are unavailable or reserved; use the menu bar.":"Control-Option-Space and Control-Option-1..8 registered.");
}
void disposeEdge(){
 if(edgeTimer){send<void>(edgeTimer,"invalidate");drop(edgeTimer);edgeTimer=nullptr;}
 for(auto& key:hotkeyRefs)if(key&&unregisterKey){unregisterKey(key);key=nullptr;}
 if(hotkeyHandler&&removeKeyHandler)removeKeyHandler(hotkeyHandler);
 clearSavedWindows();if(edgePanel)send<void>(edgePanel,"orderOut:",(Obj)nullptr);
}
