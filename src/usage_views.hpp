// Included inside main.cpp's private namespace after the label/panel/button helpers.
// Presentation of the cached limit record. Every view is created once and updated in place, so a
// fresh answer never rebuilds the window or resets a scroll position.
Obj usageColor(int remaining){
 switch(deck::usageLevel(remaining)){case deck::UsageLevel::Ok:return ink();case deck::UsageLevel::Low:return warnColor();default:return dangerColor();}
}
Obj usageTitleForMinutes(long minutes){
 switch(deck::usageWindow(minutes)){
  case deck::UsageWindow::Weekly:return str(T("WEEK","НЕДЕЛЯ"));
  case deck::UsageWindow::Short:return minutes%60==0?formatInt(T("%ld H","%ld Ч"),minutes/60):formatInt(T("%ld MIN","%ld МИН"),minutes);
  default:return minutes>0?formatInt(T("%ld DAYS","%ld ДН."),(minutes+720)/1440):str(T("LIMIT","ЛИМИТ"));
 }
}
// A per-model window carries the server's own label ("Fable"); the others are named by duration.
Obj usageTitle(Obj window){Obj label=get(window,"label");return isClass(label,"NSString")?send(label,"uppercaseString"):usageTitleForMinutes(integer(get(window,"minutes")));}
constexpr UInt usageShown=3; // windows drawn on a card / in a dock row
UInt usageWindowCount(Obj p){UInt n=0;while(n<usageShown&&usageWindowAt(p,n))++n;return n;}
// The window closest to its wall: what a single number has to mean when there are several limits.
Obj usageTightest(Obj p){Obj worst=nullptr;for(UInt i=0;i<usageWindowCount(p);++i){Obj w=usageWindowAt(p,i);if(!worst||usageRemaining(w)<usageRemaining(worst))worst=w;}return worst;}
int usageHeadroom(Obj p){Obj w=usageTightest(p);return w?usageRemaining(w):-1;}
Obj usageClock(double unix,const char* pattern){
 Obj f=autoRelease(make("NSDateFormatter"));Obj locale=autoRelease(send(send(cls("NSLocale"),"alloc"),"initWithLocaleIdentifier:",str(ruUI?"ru_RU":"en_US")));
 send<void>(f,"setLocale:",locale);send<void>(f,"setDateFormat:",str(pattern));return send(f,"stringFromDate:",send(cls("NSDate"),"dateWithTimeIntervalSince1970:",unix));
}
// A reset beyond tomorrow: weekday and time within a week, otherwise the date ("сб 11:10" / "Sat 11:10", "3 окт." / "Oct 3").
Obj usageDay(double unix,double now){Obj s=usageClock(unix,unix-now<7*86400?T("EEE HH:mm","EE HH:mm"):T("MMM d","d MMM"));return ruUI?send(s,"lowercaseString"):s;}
// compact: three windows share a card row, so the text must fit a third of it.
Obj usageResetText(double resets,bool compact=false){
 if(resets<=0)return str("");double now=nowUnix();if(resets<=now)return str(compact?T("reset","обновилось"):T("window reset","окно обновилось"));
 if(compact){Obj day=send(cls("NSDate"),"dateWithTimeIntervalSince1970:",resets),cal=send(cls("NSCalendar"),"currentCalendar");
  if(send<bool>(cal,"isDateInToday:",day))return cat(str(T("until ","до ")),usageClock(resets,"HH:mm"));if(send<bool>(cal,"isDateInTomorrow:",day))return cat(str(T("tomorrow ","завтра ")),usageClock(resets,"HH:mm"));
  return usageDay(resets,now);}
 Obj date=send(cls("NSDate"),"dateWithTimeIntervalSince1970:",resets),calendar=send(cls("NSCalendar"),"currentCalendar");
 if(send<bool>(calendar,"isDateInToday:",date))return cat(str(T("resets today at ","сброс сегодня в ")),usageClock(resets,"HH:mm"));
 if(send<bool>(calendar,"isDateInTomorrow:",date))return cat(str(T("resets tomorrow at ","сброс завтра в ")),usageClock(resets,"HH:mm"));
 return cat(str(T("resets ","сброс ")),usageDay(resets,now));
}
Obj usageAgeText(double at){
 double age=nowUnix()-at;if(at<=0)return str("");if(age<90)return str(T("just now","только что"));
 if(age<3600)return formatInt(T("%ld min ago","%ld мин назад"),(Int)(age/60));if(age<86400)return formatInt(T("%ld h ago","%ld ч назад"),(Int)(age/3600));return usageClock(at,T("MMM d, HH:mm","d MMM HH:mm"));
}
bool usageAsking(Obj p){return usageInFlight&&same(usageInFlight,get(p,"id"));}
Obj usageMessage(Obj p){
 Obj u=usageOf(p),e=get(u,"error");bool claude=usageIsClaude(appFor(p));
 if(claude&&!usageAsking(p)){
  if(same(e,str("auth")))return str(T("Claude Code sign-in needed to show limits — click here","Нужен вход Claude Code для показа лимитов — нажмите здесь"));
  if(same(e,str("nocli")))return str(T("Claude Code (claude) not found: Claude limits need it","Не найден Claude Code (claude): без него лимиты Claude недоступны"));
  if(same(e,str("noplan")))return str(T("This sign-in has no subscription limits (API key or Console)","У этого входа нет лимитов подписки (API-ключ или Console)"));
  if(same(e,str("unsupported")))return str(T("This Claude Code version does not report limits — update claude","Эта версия Claude Code не сообщает лимиты — обновите claude"));
  if(same(e,str("nodata")))return str(T("Claude Code got no limits from the server — click to retry","Claude Code не получил лимиты от сервера — нажмите, чтобы повторить"));
 }
 if(usageAsking(p))return str(T("Checking limit…","Проверяю лимит…"));if(!u)return str(T("Limit not checked yet — click to check","Лимит ещё не запрашивался — нажмите, чтобы проверить"));
 if(same(e,str("auth")))return str(T("Not signed in — the limit appears after sign-in","Нет входа в аккаунт — лимит появится после входа"));
 if(same(e,str("apikey")))return str(T("Signed in with an API key: subscription limits do not apply","Вход по API-ключу: лимиты подписки не применяются"));
 if(same(e,str("unsupported")))return str(T("This app version does not report limits","Эта версия приложения не сообщает лимиты"));
 if(same(e,str("timeout")))return str(T("Codex did not answer in time — will retry later","Codex не ответил вовремя — повторю позже"));
 if(same(e,str("spawn")))return str(T("Could not start the Codex helper process","Не удалось запустить служебный процесс Codex"));
 if(same(e,str("nodata")))return str(T("The server returned no limit windows for this plan","Для этого тарифа сервер не вернул окна лимита"));
 return e?str(T("No limit received — will retry later","Лимит не получен — повторю позже")):str(T("No limit data","Нет данных о лимите"));
}
// "left: week 37 % · 5 h 89 %" and the nearest reset — tooltips and the side panel.
Obj usageSummary(Obj p){
 UInt n=usageWindowCount(p);if(!n)return usageMessage(p);Obj line=str(T("left: ","осталось: "));
 for(UInt i=0;i<n;++i){Obj w=usageWindowAt(p,i);line=cat(cat(cat(line,str(i?" · ":"")),send(usageTitle(w),"lowercaseString")),formatInt(" %ld %%",(Int)usageRemaining(w)));}
 Obj reset=usageResetText(usageResets(usageTightest(p)));if(send<UInt>(reset,"length"))line=cat(cat(line,str("\n")),reset);
 Obj u=usageOf(p);if(get(u,"error"))line=cat(cat(line,str(T(" · data from "," · данные "))),usageAgeText(send<double>(get(u,"windowsAt"),"doubleValue")));return line;
}
// Within one app, the account with the most headroom is the one worth switching to.
bool usageIsBest(Obj p){
 int mine=usageHeadroom(p);if(mine<=0)return false;
 for(UInt i=0;i<count(profiles);++i){Obj other=at(profiles,i);if(other==p||appFor(other)!=appFor(p))continue;int theirs=usageHeadroom(other);if(theirs>mine||(theirs==mine&&i<(UInt)profileIndex(p)))return false;}
 return true;
}
// Another profile of the same group signed in to the same account: its limit is one and the same.
Obj usageTwin(Obj p){
 Obj email=get(usageOf(p),"email");if(!email)return nullptr;Obj mine=send(email,"lowercaseString");
 for(UInt i=0;i<count(profiles);++i){Obj other=at(profiles,i);if(other==p||appFor(other)!=appFor(p))continue;Obj theirs=get(usageOf(other),"email");if(theirs&&same(send(theirs,"lowercaseString"),mine))return other;}
 return nullptr;
}
// Consent must be a deliberate click: Return/Enter may not accept it, Escape declines.
void consentKeys(Obj alert){Obj buttons=send(alert,"buttons");if(count(buttons)<2)return;send<void>(at(buttons,0),"setKeyEquivalent:",str(""));send<void>(at(buttons,1),"setKeyEquivalent:",str("\033"));}
void usageHide(Obj v,bool hidden){send<void>(v,"setHidden:",hidden);}
// ---- profile card strip: up to two windows side by side, or one status line ----
Obj usageStrip(Obj card,double x,double y,double width,Int tag){
 Obj ref=dict();put(ref,"x",real(x));put(ref,"y",real(y));put(ref,"width",real(width));
 for(int i=0;i<(int)usageShown;++i){Obj col=dict();
  put(col,"caption",label(card,"",rect(x,y,10,14),10.5,faint(),.3));Obj value=label(card,"",rect(x,y-1,10,16),12,ink(),.3);send<void>(value,"setFont:",monoFont(12,.3));send<void>(value,"setAlignment:",alignRight);put(col,"value",value);
  Obj track=panel(card,rect(x,y+22,10,4),fill(.10),2);put(col,"track",track);put(col,"fill",panel(track,rect(0,0,0,4),ink(),2));
  Obj reset=label(card,"",rect(x,y+32,10,14),11,faint(),0);send<void>(reset,"setLineBreakMode:",(Int)4);put(col,"reset",reset);put(ref,i==0?"c0":i==1?"c1":"c2",col);}
 Obj message=label(card,"",rect(x,y+11,width,18),12,muted(),.1);send<void>(message,"setLineBreakMode:",(Int)4);put(ref,"message",message);
 Obj hit=styledButton(deckButtonClass,card,"","usageRefresh:",rect(x-8,y-7,width+16,54),tag,"overlay",8);put(ref,"hit",hit);send<void>(hit,"setAccessibilityLabel:",str(T("Refresh account limit","Обновить лимит аккаунта")));
 return ref;
}
void usageApplyStrip(Obj ref,Obj p){
 if(!ref)return;double x=send<double>(get(ref,"x"),"doubleValue"),y=send<double>(get(ref,"y"),"doubleValue"),width=send<double>(get(ref,"width"),"doubleValue");
 UInt n=usageWindowCount(p);Obj u=usageOf(p);bool stale=n&&get(u,"error");
 usageHide(get(ref,"message"),n>0);if(!n)send<void>(get(ref,"message"),"setStringValue:",usageMessage(p));
 double gap=n>2?16:24,colW=n?(width-gap*(n-1))/n:width;
 for(UInt i=0;i<usageShown;++i){Obj col=get(ref,i==0?"c0":i==1?"c1":"c2");bool on=i<n;for(const char* k:{"caption","value","track","reset"})usageHide(get(col,k),!on);if(!on)continue;
  Obj w=usageWindowAt(p,i);int left=usageRemaining(w);double x0=x+i*(colW+gap);bool rolled=usageResets(w)>0&&usageResets(w)<=nowUnix(),grey=stale||rolled;
  // Three windows share a row: the number alone (the bar already reads as what is left).
  Obj valueText=left<=0?str(T("used up","исчерпан")):n>2?formatInt(T("%ld%%","%ld %%"),(Int)left):formatInt(T("%ld%% left","осталось %ld %%"),(Int)left);
  double valueW=measure(utf8(valueText),monoFont(12,.3))+8;if(valueW>colW)valueW=colW;
  send<void>(get(col,"value"),"setFrame:",rect(x0+colW-valueW,y-1,valueW,16));send<void>(get(col,"value"),"setStringValue:",valueText);
  send<void>(get(col,"caption"),"setFrame:",rect(x0,y,colW-valueW-6,14));send<void>(get(col,"caption"),"setStringValue:",usageTitle(w));kern(get(col,"caption"),.63);
  send<void>(get(col,"value"),"setTextColor:",grey?muted():usageColor(left));
  send<void>(get(col,"track"),"setFrame:",rect(x0,y+22,colW,4));send<void>(get(col,"fill"),"setFrame:",rect(0,0,colW*deck::usageFill(left),4));
  send<void>(send(get(col,"fill"),"layer"),"setBackgroundColor:",send(grey?muted():usageColor(left),"CGColor"));
  Obj reset=usageResetText(usageResets(w),n>2);send<void>(get(col,"reset"),"setToolTip:",usageResetText(usageResets(w)));if(stale&&i==0&&n<3)reset=cat(cat(reset,str(send<UInt>(reset,"length")?" · ":"")),cat(str(T("data from ","данные ")),usageAgeText(send<double>(get(u,"windowsAt"),"doubleValue"))));
  send<void>(get(col,"reset"),"setFrame:",rect(x0,y+32,colW,14));send<void>(get(col,"reset"),"setStringValue:",reset);
 }
 Obj tip=usageAsking(p)?str(T("Checking limit…","Проверяю лимит…")):cat(str(T("Click to refresh · checked ","Нажмите, чтобы обновить · проверено ")),usageAgeText(send<double>(get(u,"checked"),"doubleValue")));
 if(get(u,"detail"))tip=cat(cat(tip,str("\n")),get(u,"detail"));send<void>(get(ref,"hit"),"setToolTip:",u?tip:str(T("Click to ask Codex for the limit","Нажмите, чтобы запросить лимит у Codex")));
}
