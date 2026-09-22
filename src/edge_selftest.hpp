// Acceptance aid: APPDECK_DOCK_SELFTEST=1, only together with APPDECK_DATA_ROOT (never on real data).
// Opens the dock and replays presses, clicks and drags through AppKit's own event queue — the same
// dispatch as a real mouse, without moving the user's pointer — then prints what happened and exits.
// Clicks are redirected to a recorder, so no profile is ever launched. Lines starting with CAPTURE
// mark still states (clean top, a cut edge, the end of the list) for a window screenshot.
int selfTestPhase=0,selfTestFrame=0,selfTestFrames=0,selfTestHold=0,selfTestFailures=0;double selfTestWait=0;bool selfTestSayHold=false;
Point selfTestFrom{},selfTestTo{};Obj selfTestNamesThen=nullptr,selfTestClicked=nullptr,selfTestTimer=nullptr;
bool selfTestWanted(){const char* on=getenv("APPDECK_DOCK_SELFTEST");const char* root=getenv("APPDECK_DATA_ROOT");return !previewMode&&on&&*on=='1'&&root&&*root;}
void selfTestSay(Obj line){const char* s=utf8(cat(line,str("\n")));write(1,s,strlen(s));}
Obj selfTestNames(){Obj r=array(),all=dockProfiles();for(UInt i=0;i<count(all);++i)add(r,get(at(all,i),"name"));return r;}
Obj selfTestJoin(Obj names){return send(names,"componentsJoinedByString:",str(", "));}
void selfTestCheck(const char* what,bool ok,Obj detail=nullptr){if(!ok)++selfTestFailures;selfTestSay(cat(cat(str(ok?"PASS ":"FAIL "),str(what)),detail?cat(str(" — "),detail):str("")));}
void selfTestExpectOrder(const char* what,Obj expected){Obj now=selfTestNames();selfTestCheck(what,send<bool>(now,"isEqualToArray:",expected),selfTestJoin(now));}
Obj selfTestMoved(Obj names,UInt from,UInt to){Obj r=autoRelease(send(names,"mutableCopy"));Obj x=keep(at(r,from));send<void>(r,"removeObjectAtIndex:",from);send<void>(r,"insertObject:atIndex:",x,to);drop(x);return r;}
Point selfTestRowCentre(UInt slot){Obj row=get(at(edgeRefs,slot),"row");Rect b=getRect(row,"bounds");return send<Point>(row,"convertPoint:toView:",Point{b.size.width/2,b.size.height/2},(Obj)nullptr);}
void selfTestPost(UInt type,Point p){
 static Int number=0;double now=send<double>(send(cls("NSProcessInfo"),"processInfo"),"systemUptime");
 Obj e=send(cls("NSEvent"),"mouseEventWithType:location:modifierFlags:timestamp:windowNumber:context:eventNumber:clickCount:pressure:",type,p,(UInt)0,now,send<Int>(edgePanel,"windowNumber"),(Obj)nullptr,++number,(Int)1,(float)(type==2?0:1));
 send<void>(app,"postEvent:atStart:",e,false);
}
// Press on row `slot`, move `down` points down the screen over `frames` frames, hold `hold` frames, release.
void selfTestGesture(UInt slot,double down,int frames,int hold){
 selfTestFrom=selfTestRowCentre(slot);selfTestTo=Point{selfTestFrom.x,selfTestFrom.y-down};selfTestFrame=0;selfTestFrames=frames;selfTestHold=hold;selfTestPost(1,selfTestFrom);
}
bool selfTestGestureStep(){
 if(selfTestFrame<selfTestFrames){++selfTestFrame;double t=(double)selfTestFrame/selfTestFrames;selfTestPost(6,Point{selfTestFrom.x,selfTestFrom.y+(selfTestTo.y-selfTestFrom.y)*t});return false;}
 if(selfTestHold>0){if(selfTestSayHold){selfTestSayHold=false;selfTestSay(str("CAPTURE drag"));}--selfTestHold;selfTestPost(6,selfTestTo);return false;}
 selfTestPost(2,selfTestTo);return true;
}
void selfTestClickAction(Obj,Sel,Obj sender){Obj p=fromSender(sender);drop(selfTestClicked);selfTestClicked=p?keep(get(p,"name")):nullptr;}
void selfTestRecordClicks(UInt slot){send<void>(get(at(edgeRefs,slot),"button"),"setAction:",sel("selfTestClick:"));drop(selfTestClicked);selfTestClicked=nullptr;}
double selfTestShade(bool top){Rect v=getRect(edgeClip(),"bounds");return deck::edgeCut(top?v.origin.y:v.origin.y+v.size.height);}
void selfTestScrollTo(double y){Obj clip=edgeClip();send<void>(clip,"scrollToPoint:",Point{0,y});send<void>(edgeScroll,"reflectScrolledClipView:",clip);}
Obj selfTestNumber(double x){return formatInt("%ld",(Int)(x+(x<0?-.5:.5)));}
void selfTestTick(Obj,Sel,Obj){
 Pool pool;double now=send<double>(send(cls("NSProcessInfo"),"processInfo"),"systemUptime");if(now<selfTestWait)return;
 auto after=[&](double seconds,int next){selfTestWait=now+seconds;selfTestPhase=next;};
 UInt n=count(edgeRefs);double step=deck::edgeStep;
 switch(selfTestPhase){
 case 0: if(!edgePanel||edgeProgress<1||edgeTimer||edgeDragging)return;
  selfTestNamesThen=keep(selfTestNames());selfTestSay(cat(str("START order: "),selfTestJoin(selfTestNamesThen)));
  if(n<7){selfTestSay(str("SKIP: the dock self-test needs at least 7 profiles"));exit(0);}
  selfTestCheck("at most six rows visible, the rest scrolls",edgeRowsNow==(Int)deck::edgeRows((unsigned)n,getRect(edgeScreen,"visibleFrame").size.height)&&edgeRowsNow<=6&&n==count(selfTestNamesThen),formatInt("visible %ld",edgeRowsNow));
  selfTestCheck("opens at the top without shade",edgeScrollOffset()==0&&selfTestShade(true)==0&&selfTestShade(false)==0);
  selfTestSay(str("CAPTURE top"));after(2.5,1);break;
 case 1: selfTestGesture(0,2*step+6,20,0);selfTestPhase=2;break;
 case 2: if(selfTestGestureStep())after(.6,3);break;
 case 3: selfTestExpectOrder("drag row 1 to place 3",selfTestMoved(selfTestNamesThen,0,2));drop(selfTestNamesThen);selfTestNamesThen=keep(selfTestNames());
  selfTestGesture(3,12,8,0);selfTestPhase=4;break; // beyond the 4 pt threshold, but still its own place
 case 4: if(selfTestGestureStep())after(.6,5);break;
 case 5: selfTestExpectOrder("short drag drops back in place",selfTestNamesThen);selfTestRecordClicks(4);selfTestGesture(4,0,0,0);selfTestPhase=6;break;
 case 6: if(selfTestGestureStep())after(.4,7);break;
 case 7: selfTestCheck("plain click opens the row's profile",selfTestClicked&&same(selfTestClicked,at(selfTestNamesThen,4)),selfTestClicked);
  selfTestExpectOrder("a click does not reorder",selfTestNamesThen);selfTestRecordClicks(5);selfTestGesture(5,2.5,2,0);selfTestPhase=8;break; // hand jitter under 4 pt
 case 8: if(selfTestGestureStep())after(.4,9);break;
 case 9: {selfTestCheck("a 2.5 pt wobble is still a click",selfTestClicked&&same(selfTestClicked,at(selfTestNamesThen,5)),selfTestClicked);
  // Drag row 2 below the bottom edge of the list and hold: the list scrolls to its end by itself.
  Rect f=getRect(edgeScroll,"frame");Point bottom=send<Point>(edgeContent,"convertPoint:toView:",Point{0,f.origin.y+f.size.height},(Obj)nullptr);
  Point from=selfTestRowCentre(1);selfTestSayHold=true;selfTestGesture(1,from.y-(bottom.y-24),16,150);selfTestPhase=10;break;}
 case 10: if(selfTestGestureStep())after(.7,11);break;
 case 11: {double range=deck::edgeScrollRange((unsigned)n,(unsigned)edgeRowsNow);
  selfTestExpectOrder("drag past the bottom edge auto-scrolls and drops last",selfTestMoved(selfTestNamesThen,1,n-1));
  selfTestCheck("the list stays scrolled to its end after the drop",edgeScrollOffset()>range-.5,cat(selfTestNumber(edgeScrollOffset()),cat(str(" / "),selfTestNumber(range))));
  selfTestCheck("no shade at the end of the list",selfTestShade(true)==0&&selfTestShade(false)==0);selfTestSay(str("CAPTURE end"));after(2.5,12);break;}
 case 12: selfTestScrollTo(step+29);selfTestCheck("rows cut by both edges are shaded",selfTestShade(true)==1&&selfTestShade(false)==1);selfTestSay(str("CAPTURE cut"));after(2.5,13);break;
 case 13: selfTestScrollTo(0);selfTestCheck("back at the top: clean again",selfTestShade(true)==0&&selfTestShade(false)==0);selfTestSay(str("CAPTURE top-again"));after(2.5,14);break;
 case 14: {drop(selfTestNamesThen);selfTestNamesThen=keep(selfTestNames());Obj first=nullptr;
  // The dock's menu actions, through the same selectors as the row menu.
  Obj item=make("NSMenuItem");send<void>(item,"setTag:",profileIndex(at(dockProfiles(),7)));send<void>(controller,"performSelector:withObject:",sel("edgeMoveFirst:"),item);drop(item);
  selfTestExpectOrder("menu: move to the top",selfTestMoved(selfTestNamesThen,7,0));first=selfTestNames();
  item=make("NSMenuItem");send<void>(item,"setTag:",profileIndex(at(dockProfiles(),0)));send<void>(controller,"performSelector:withObject:",sel("edgeMoveDown:"),item);drop(item);
  selfTestExpectOrder("menu: move down",selfTestMoved(first,0,1));
  item=make("NSMenuItem");send<void>(item,"setTag:",profileIndex(at(dockProfiles(),1)));send<void>(controller,"performSelector:withObject:",sel("edgeMoveUp:"),item);drop(item);
  selfTestExpectOrder("menu: move up",first);
  // The main window's card menu moves a profile among the cards of its own app, in the same order.
  Obj cards=visibleProfiles(),q=at(cards,1),p=at(cards,2),expected=autoRelease(send(selfTestNames(),"mutableCopy"));
  send<void>(expected,"removeObject:",get(p,"name"));send<void>(expected,"insertObject:atIndex:",get(p,"name"),send<UInt>(expected,"indexOfObject:",get(q,"name")));
  item=make("NSMenuItem");send<void>(item,"setTag:",profileIndex(p));send<void>(controller,"performSelector:withObject:",sel("profileMoveUp:"),item);drop(item);
  selfTestExpectOrder("card menu: move up among the app's cards",expected);
  item=make("NSMenuItem");send<void>(item,"setTag:",profileIndex(p));send<void>(controller,"performSelector:withObject:",sel("profileMoveDown:"),item);drop(item);
  selfTestExpectOrder("card menu: move down again",first);
  selfTestSay(cat(formatInt("DONE failures=%ld  order: ",(Int)selfTestFailures),selfTestJoin(selfTestNames())));selfTestPhase=15;after(.3,15);break;}
 default: quitting=true;exit(selfTestFailures?1:0);
 }
}
void selfTestStart(){
 selfTestSay(str("SELFTEST dock order/scroll/drag"));setEdgeVisible(true);
 selfTestTimer=keep(send(cls("NSTimer"),"timerWithTimeInterval:target:selector:userInfo:repeats:",1.0/60,controller,sel("selfTestTick:"),(Obj)nullptr,true));
 Obj mode=publicConstant(appKit,"NSRunLoopCommonModes"); // keeps firing inside the drag's own event loop
 send<void>(send(cls("NSRunLoop"),"mainRunLoop"),"addTimer:forMode:",selfTestTimer,mode?mode:str("kCFRunLoopCommonModes"));
}
