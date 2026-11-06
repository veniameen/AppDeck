// Native UI for the opt-in, read-only catalogue and Claude-owned registration in one exact pid.
void claudeBridgeToggleAction(Obj,Sel,Obj){
 Obj a=currentApp();if(!claudeGroup(a)||previewMode)return;bool on=truth(get(a,"claudeSessionBridge"));
 if(!on&&!confirm(T("Use local Code sessions across accounts?","Использовать локальные сессии Code в разных аккаунтах?"),T("AppDeck reads titles and project folders from your Claude Desktop session lists. Choosing a profile lets Claude register and continue the same local conversation under that account. Close other Claude Desktop instances before opening a shared session. Cloud chats and account data stay separate.","AppDeck читает названия и папки проектов из списков сессий Claude Desktop. При выборе профиля Claude регистрирует и продолжает ту же локальную переписку под этим аккаунтом. Перед открытием общей сессии закройте остальные экземпляры Claude Desktop. Облачные чаты и данные аккаунтов остаются раздельными."),T("Enable local sessions","Включить локальные сессии")))return;
 put(a,"claudeSessionBridge",boolean(!on));save();drop(claudeCatalogue);claudeCatalogue=nullptr;drop(claudeCatalogueApp);claudeCatalogueApp=nullptr;buildUI();
}
void claudeSessionsRefreshAction(Obj,Sel,Obj){drop(claudeCatalogue);claudeCatalogue=nullptr;buildUI();}
void claudeSessionMenuAction(Obj,Sel,Obj sender){
 Obj a=currentApp();Int index=send<Int>(sender,"tag");if(!claudeGroup(a)||!claudeCatalogue||!same(claudeCatalogueApp,get(a,"id"))||index<0||(UInt)index>=count(claudeCatalogue))return;
 Obj entry=at(claudeCatalogue,(UInt)index),menu=make("NSMenu");send<void>(menu,"setAutoenablesItems:",false);
 for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);if(appFor(p)!=a||claudeCleanProfile(p))continue;Obj item=menuItem(menu,utf8(get(p,"name")),"claudeSessionOpen:");Obj context=dict();
  put(context,"appId",get(a,"id"));put(context,"profileId",get(p,"id"));put(context,"id",get(entry,"id"));send<void>(item,"setRepresentedObject:",context);
  send<void>(item,"setEnabled:",!truth(get(entry,"conflict"))&&claudeCatalogueComplete&&!claudeCatalogueSkipped&&claudeBridgeSupported(a));
 }send<bool>(menu,"popUpMenuPositioningItem:atLocation:inView:",(Obj)nullptr,Point{0,36},sender);drop(menu);
}
bool claudeConfirmSession(Obj p,Obj entry){
 if(previewMode)return false;frontForModal();Obj alert=make("NSAlert");send<void>(alert,"setMessageText:",str(T("Continue this conversation in the chosen profile?","Продолжить переписку в выбранном профиле?")));
 Obj message=cat(cat(cat(get(entry,"title"),str("\n\n")),get(p,"name")),str("\n"));message=cat(cat(message,get(entry,"cwd")),str(T("\n\nAllow Claude in this profile to access this project folder and the saved conversation. Close any terminal session using this conversation first. No prompt is sent to the model by AppDeck.","\n\nРазрешите Claude в этом профиле доступ к папке проекта и сохранённой переписке. Сначала закройте терминальную сессию, если в ней открыта эта переписка. AppDeck не отправляет запрос модели.")));
 send<void>(alert,"setInformativeText:",message);send(alert,"addButtonWithTitle:",str(T("Open in this profile","Открыть в этом профиле")));send(alert,"addButtonWithTitle:",str(T("Cancel","Отмена")));
 bool yes=send<Int>(alert,"runModal")==1000;drop(alert);return yes;
}
void claudeSessionOpenAction(Obj,Sel,Obj sender){
 if(previewMode)return;Obj context=send(sender,"representedObject");if(!isClass(context,"NSDictionary"))return;
 Obj a=nullptr,p=nullptr;for(UInt i=0;i<count(apps);++i)if(same(get(at(apps,i),"id"),get(context,"appId")))a=at(apps,i);
 for(UInt i=0;i<count(profiles);++i)if(same(get(at(profiles,i),"id"),get(context,"profileId")))p=at(profiles,i);
 if(!a||!p||appFor(p)!=a||!claudeGroup(a)||claudeCleanProfile(p)||!truth(get(a,"claudeSessionBridge")))return;
 if(!claudeBridgeSupported(a)){showError(str(T("Claude version needs verification","Версия Claude требует проверки")),str(T("The session bridge supports the inspected Claude Desktop 2.31226.1. Ordinary profile launches still work; no import is attempted on another version.","Мост сессий поддерживает проверенную по коду версию Claude Desktop 2.31226.1. Обычный запуск профилей доступен; в другой версии импорт не выполняется.")));return;}
 Obj rows=claudeLoadCatalogue(a),entry=nullptr;for(UInt i=0;i<count(rows);++i)if(same(get(at(rows,i),"id"),get(context,"id")))entry=at(rows,i);
 if(!entry||!claudeCatalogueComplete||claudeCatalogueSkipped||truth(get(entry,"conflict"))||!claudeTranscriptPresent(get(entry,"id"))){showError(str(T("Session cannot be opened safely","Сессию нельзя безопасно открыть")),str(T("Refresh the list. The session may have changed, its local transcript may be missing, or its project folders disagree. No Claude data was changed.","Обновите список. Сессия могла измениться, локальная переписка может отсутствовать или папки проекта не совпадают. Данные Claude не изменены.")));return;}
 Obj target=running(p);if(!target)target=ownedProcess(p);
 if(claudeCompetingDesktop(target)){showError(str(T("Close the other Claude instances first","Сначала закройте остальные экземпляры Claude")),str(T("AppDeck cannot tell from saved metadata whether another window is editing this conversation. Quit other Claude Desktop instances, then open it in the chosen profile.","По сохранённым данным AppDeck не может определить, редактирует ли другое окно эту переписку. Завершите остальные экземпляры Claude Desktop и откройте сессию в выбранном профиле.")));return;}
 if(Obj failure=claudeLaunchGate(p)){showError(str(T("Claude session is reserved","Сессия Claude зарезервирована")),failure);return;}
 if(!claudeConfirmSession(p,entry))return;
 // Modal confirmation permits timers and external changes. Revalidate the consented folder
 // and process set before launching anything; never apply consent to a newly changed record.
 Obj freshRows=claudeLoadCatalogue(a),fresh=nullptr;for(UInt i=0;i<count(freshRows);++i)if(same(get(at(freshRows,i),"id"),get(entry,"id")))fresh=at(freshRows,i);
 target=running(p);if(!target)target=ownedProcess(p);
 if(!fresh||!claudeCatalogueComplete||claudeCatalogueSkipped||truth(get(fresh,"conflict"))||!same(get(fresh,"cwd"),get(entry,"cwd"))||!same(get(fresh,"projectRoot"),get(entry,"projectRoot"))||!claudeBridgeSupported(a)||!claudeTranscriptPresent(get(entry,"id"))||claudeCompetingDesktop(target)){
  showError(str(T("Session changed during confirmation","Сессия изменилась во время подтверждения")),str(T("No import was sent. Refresh the list and close other Claude instances before retrying.","Запрос импорта не отправлен. Обновите список и закройте остальные экземпляры Claude перед повтором.")));return;}
 if(!launch(p,true))return;target=running(p);if(!target||claudeCompetingDesktop(target)){showError(str(T("Chosen Claude process is unavailable","Выбранный процесс Claude недоступен")),str(T("No import was sent. Check the chosen profile and close other Claude instances.","Запрос импорта не отправлен. Проверьте выбранный профиль и закройте остальные экземпляры Claude.")));return;}
 ClaudeScanBudget budget;Obj before=claudeScanProfile(p,budget);if(!budget.complete||budget.skipped){showError(str(T("Target session list is unavailable","Список сессий выбранного профиля недоступен")),str(T("No import was sent. Check the profile's folder and retry.","Запрос импорта не отправлен. Проверьте папку профиля и повторите.")));return;}
 double now=send<double>(send(cls("NSDate"),"date"),"timeIntervalSince1970");Obj lease=claudeMakeLease(p,entry,send<int>(target,"processIdentifier"),send<double>(get(p,"started"),"doubleValue"),before,now);
 if(!claudeWriteLease(a,lease)){showError(str(T("Session reservation was not saved","Резервирование сессии не сохранено")),str(T("No import was sent. Check AppDeck's Shared folder; an unreadable reservation is never overwritten.","Запрос импорта не отправлен. Проверьте папку Shared AppDeck; нечитаемый файл резервирования не перезаписывается.")));return;}
 put(a,"claudeBridgeLeaseRecorded",boolean(true));if(!save())return; // disappearing journals must not look like a first-use empty reservation
 // Recheck identity and competitors after durable reservation, immediately before the pid event.
 if(!claudeLeaseProcess(a,lease)||claudeCompetingDesktop(target)||!claudeSendResume(target,get(entry,"id"))){Obj failed=autoRelease(send(lease,"mutableCopy"));put(failed,"status",str("transport-error"));claudeWriteLease(a,failed,lease);
  showError(str(T("Claude did not accept the open request","Claude не принял запрос открытия")),str(T("Check macOS Automation permission and the chosen window. No other Claude instance was addressed. The reservation stays until this process exits because delivery may be uncertain.","Проверьте разрешение автоматизации macOS и выбранное окно. Запрос не направлялся другому экземпляру Claude. Резервирование сохраняется до завершения процесса, поскольку доставка могла быть неопределённой.")));}
 else note("Claude resume URL sent to the selected pid; registration is pending, not assumed successful.");
 drop(claudeCatalogue);claudeCatalogue=nullptr;page=2;buildUI();
}
void buildClaudeSessionsPage(double x,double w,double footY){
 Obj a=currentApp();bool enabled=truth(get(a,"claudeSessionBridge"));
 headerButtons(x+w,{{T("Refresh","Обновить"),"claudeSessionsRefresh:","arrow.clockwise",false,nullptr}});
 pageHeader(x,w,T("LOCAL CONVERSATIONS","ЛОКАЛЬНЫЕ ПЕРЕПИСКИ"),str(T("Claude Code sessions","Сессии Claude Code")),str(T("Choose an account to continue a saved local conversation.","Выберите аккаунт, чтобы продолжить сохранённую локальную переписку.")));
 if(!enabled){Obj box=card(rootView,rect(x,138,w,172),16);iconTile(box,rect(20,24,40,40),"bubble.left.and.bubble.right");
  label(box,T("Your local sessions, across accounts","Локальные сессии в разных аккаунтах"),rect(76,26,w-96,20),15,ink(),.3);
  label(box,T("Claude registers each selected conversation in the chosen profile.","Claude регистрирует выбранную переписку в нужном профиле."),rect(76,54,w-96,18),12,muted(),0);
  const char* title=T("Enable local sessions","Включить локальные сессии");button(box,title,"claudeBridgeToggle:",rect(20,112,fitWidth(title,true),32),-1,true,"link");
 }else{
  if(!claudeCatalogue||!same(claudeCatalogueApp,get(a,"id"))){drop(claudeCatalogue);claudeCatalogue=keep(claudeLoadCatalogue(a));drop(claudeCatalogueApp);claudeCatalogueApp=keep(get(a,"id"));}
  if(!count(claudeCatalogue)){Obj box=card(rootView,rect(x,138,w,92),16);label(box,T("No local Code sessions were found","Локальные сессии Code не найдены"),rect(20,20,w-40,20),15,ink(),.3);label(box,T("The bridge reads Desktop session lists. It does not import all CLI history.","Мост читает списки сессий Desktop. Вся история CLI не импортируется."),rect(20,50,w-40,18),12,muted(),0);}
  else{
   UInt groups=0;Obj previous=nullptr;for(UInt i=0;i<count(claudeCatalogue);++i){Obj root=get(at(claudeCatalogue,i),"projectRoot");if(!same(root,previous)){++groups;previous=root;}}
   double rowH=64,listH=count(claudeCatalogue)*rowH+groups*40;Obj doc=scrollDocument(rootView,rect(x,138,w,footY-16-138),listH),box=card(doc,rect(0,0,w,listH),16);double y=0;previous=nullptr;
   const char* openTitle=T("Open in…","Открыть в…");double bw=fitWidth(openTitle,true);bool compatible=claudeBridgeSupported(a);
   for(UInt i=0;i<count(claudeCatalogue);++i){Obj entry=at(claudeCatalogue,i),root=get(entry,"projectRoot");
    if(!same(root,previous)){if(y)settingDivider(box,y,w);Obj heading=labelObj(box,send(root,"lastPathComponent"),rect(16,y+12,w-32,18),13,inkSoft(),.4);send<void>(heading,"setToolTip:",root);y+=40;previous=root;}
    Obj title=labelObj(box,get(entry,"title"),rect(20,y+8,w-bw-60,18),13,ink(),.23);send<void>(title,"setLineBreakMode:",(Int)4);send<void>(title,"setToolTip:",get(entry,"title"));
    Obj detail=truth(get(entry,"conflict"))?str(T("Project folders disagree · opening blocked","Папки проекта не совпадают · открытие заблокировано")):shortPath(get(entry,"cwd"));
    Obj sub=labelObj(box,detail,rect(20,y+29,w-bw-60,16),11,truth(get(entry,"conflict"))?warnColor():faint(),0);send<void>(sub,"setLineBreakMode:",(Int)5);
    Obj b=button(box,openTitle,"claudeSessionMenu:",rect(w-16-bw,y+12,bw,32),(Int)i,false,"person.crop.circle");if(!compatible||!claudeCatalogueComplete||claudeCatalogueSkipped||truth(get(entry,"conflict")))disable(b);y+=rowH;
   }
  }
 }
 Obj status=!enabled?str(T("Sign-ins stay separate. Enable the bridge to choose sessions across profiles.","Входы остаются раздельными. Включите общие сессии для выбора переписок между профилями.")):
  !claudeBridgeSupported(a)?str(T("This Claude version needs bridge verification; session opening is disabled.","Эта версия Claude требует проверки моста; открытие сессий отключено.")):
  !claudeCatalogueComplete||claudeCatalogueSkipped?str(T("Some session records could not be read. Refresh after checking the profile folders.","Часть записей сессий не прочитана. Проверьте папки профилей и обновите список.")):claudeLeaseStatus(a);
 claudeStatusLabel=footerNote(x,footY,w,"lock.shield",status);
}
