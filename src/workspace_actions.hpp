void connectBaseAction(Obj,Sel,Obj){
 Obj a=currentApp();if(!a||deck::adapter(utf8(get(a,"adapter")))!=deck::Adapter::Codex){showError(str(T("Select Codex","Выберите Codex")),str(T("A base workspace is available for a group with the Codex adapter.","Базовый профиль доступен для группы с адаптером Codex.")));return;}
 Obj picker=send(cls("NSOpenPanel"),"openPanel");send<void>(picker,"setTitle:",str(T("Choose the current Codex folder — usually ~/.codex","Выберите текущую папку Codex — обычно ~/.codex")));
 send<void>(picker,"setCanChooseDirectories:",true);send<void>(picker,"setCanChooseFiles:",false);send<void>(picker,"setAllowsMultipleSelection:",false);send<void>(picker,"setShowsHiddenFiles:",true);
 send<void>(picker,"setDirectoryURL:",url(baseSource(a)?baseSource(a):join(home,".codex")));
 if(send<Int>(picker,"runModal")!=1)return;Obj source=canonical(send(send(picker,"URL"),"path"));
 if(!directory(source)||deck::inside(utf8(canonical(dataRoot)),utf8(source))){showError(str(T("Invalid source","Некорректный источник")),str(T("Choose the original Codex folder outside AppDeck's data, not a copy's folder.","Выберите исходную папку Codex вне данных AppDeck, не папку отдельной копии.")));return;}
 if(!exists(join(source,"config.toml"))&&!exists(join(source,".codex-global-state.json"))&&!exists(join(source,"AGENTS.md"))&&!exists(join(source,"skills"))&&!exists(join(source,"rules"))){showError(str(T("No Codex workspace found","Не найдена рабочая среда Codex")),str(T("The chosen folder has no config.toml, AGENTS.md, project list, skills or rules. Choose your current ~/.codex folder.","В выбранной папке нет config.toml, AGENTS.md, списка проектов, skills или rules. Выберите текущую папку ~/.codex.")));return;}
 if(baseSource(a)&&!same(source,baseSource(a))){showError(str(T("Source already connected","Источник уже подключён")),str(T("This preview does not change the base folder, so existing skills/rules links are never swapped. Create a separate group after saving your data.","В этой preview-версии смена базового каталога не выполняется, чтобы не подменить существующие связи skills/rules. Создайте отдельную группу после сохранения данных.")));return;}
 Obj snapshot=workspaceSnapshot(source);Obj config=readText(canonical(join(source,"config.toml")));
 if(config){Size cap=strlen(utf8(config))+512;char* out=(char*)calloc(cap,1);bool ok=out&&deck::codexConfig(utf8(config),out,cap)==deck::ConfigResult::Ok;free(out);if(!ok){showError(str(T("config.toml needs a review","Нужна проверка config.toml")),str(T("The configuration contains a construct that AppDeck's editor cannot change safely (for example multi-line TOML). The source is unchanged. Keep a simplified configuration in AppDeck's shared folder or extend the TOML editor in the sources.","Конфигурация содержит конструкцию, которую редактор AppDeck не умеет безопасно изменять (например, многострочный TOML). Источник не изменён. Сохраните упрощённую конфигурацию в общей папке AppDeck либо расширьте TOML-редактор в исходниках.")));return;}}
 Obj alert=make("NSAlert");send<void>(alert,"setMessageText:",str(T("Use the current workspace","Использовать текущую рабочую среду")));
 send<void>(alert,"setInformativeText:",str(T("config.toml and AGENTS.md are read before linked copies launch; skills/rules are used through links. The config may contain MCP keys and workspace restrictions — they are inherited too. Sign-in and cookies are never transferred. With shared history, copies open the same thread database as the primary Codex (CODEX_SQLITE_HOME): chats and projects become shared, no database is copied.","config.toml и AGENTS.md читаются перед запуском связанных копий; skills/rules используются по ссылкам. Конфиг может содержать MCP-ключи и ограничения workspace — они тоже наследуются. Авторизация и cookies не переносятся никогда. С общей историей копии открывают ту же базу тредов, что и основной Codex (CODEX_SQLITE_HOME): чаты и проекты становятся общими, базы не копируются.")));
 Obj accessory=send(send((Obj)deckViewClass,"alloc"),"initWithFrame:",rect(0,0,485,150));
 labelObj(accessory,shortPath(source),rect(0,0,480,23),12,muted());
 Obj master=checkbox(accessory,T("Add the primary Codex to the panel","Добавить основной Codex в панель"),rect(0,31,480,22),true);
 Obj seed=checkbox(accessory,T("Initial project list for empty copies (experimental)","Начальный список проектов для пустых копий (эксп.)"),rect(0,58,480,22),snapshot!=nullptr);send<void>(seed,"setEnabled:",snapshot!=nullptr);
 bool canShare=false;{Obj e2=nullptr;Obj items=send(fileManager,"contentsOfDirectoryAtPath:error:",source,&e2);for(UInt i=0;i<count(items);++i)if(deck::stateDatabase(utf8(at(items,i)))){canShare=true;break;}}
 Obj history=checkbox(accessory,T("Shared history and projects: copies see the primary Codex chats","Общая история и проекты: копии видят чаты основного Codex"),rect(0,85,480,22),canShare&&(!get(a,"sharedHistory")||truth(get(a,"sharedHistory"))));send<void>(history,"setEnabled:",canShare);
 labelObj(accessory,snapshot?formatInt(T("Folders recognized: %ld. The copies' current lists are not overwritten.","Распознано папок: %ld. Действующий список копий не перезаписывается."),(Int)count(get(snapshot,"electron-saved-workspace-roots"))):str(T("The project list format was not recognized. No files will be changed.","Формат списка проектов не распознан. Файлы не будут изменены.")),rect(0,116,483,30),11,muted());
 send<void>(alert,"setAccessoryView:",accessory);send(alert,"addButtonWithTitle:",str(T("Connect","Подключить")));send(alert,"addButtonWithTitle:",str(T("Cancel","Отмена")));
 if(send<Int>(alert,"runModal")==1000){
  put(a,"baseSource",source);put(a,"seedWorkspace",boolean(send<Int>(seed,"state")==1));if(canShare)put(a,"sharedHistory",boolean(send<Int>(history,"state")==1));Int added=importProjectList(snapshot);
  if(send<Int>(master,"state")==1){Obj original=nullptr,unused=nullptr;
   for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);if(appFor(p)!=a)continue;if(isMaster(p))original=p;
    if(!unused&&!exists(profileRoot(p))&&!get(p,"pid"))unused=p;
   }
   if(!original){original=unused?unused:newProfile(a,str(T("Primary · current","Основной · текущий")),0,false);put(original,"master",boolean(true));put(original,"share",boolean(false));put(original,"name",str(T("Primary · current","Основной · текущий")));}
   // Put the primary first among this app's profiles, keeping all pre-existing initialized profiles intact.
   keep(original);send<void>(profiles,"removeObjectIdenticalTo:",original);UInt first=count(profiles);for(UInt i=0;i<count(profiles);++i)if(appFor(at(profiles,i))==a){first=i;break;}
   send<void>(profiles,"insertObject:atIndex:",original,first);drop(original);
  }
  save();buildUI();buildMenus();note("Read-only source settings connected; recognized project paths imported, credentials not copied.");
  showError(str(T("Base workspace connected","Базовая среда подключена")),cat(formatInt(T("Folders added to AppDeck: %ld. ","В AppDeck добавлено папок: %ld. "),added),str(T("Launch the accounts one at a time. The primary profile uses the existing app unchanged; copies receive the settings before they launch. Whether transferred projects show in the Codex sidebar depends on the client version.","Запускайте аккаунты по очереди. Основной использует существующее приложение без изменений; копии получают настройки перед запуском. Видимость перенесённых проектов в боковой панели Codex зависит от версии клиента."))));
 }
 drop(accessory);drop(alert);
}
void syncBaseProjectsAction(Obj,Sel,Obj){
 Obj a=currentApp();if(!a||!baseSource(a)){connectBaseAction(nullptr,nullptr,nullptr);return;}
 if(groupSyncEnabled(a)){Obj failure=syncGroup(a,true);buildUI();showError(str(failure?T("Sync not finished","Синхронизация не завершена"):T("Shared workspace updated","Общая среда обновлена")),failure?failure:get(a,"syncStatus"));return;}
 Obj snapshot=workspaceSnapshot(baseSource(a));if(!snapshot){showError(str(T("List not recognized","Список не распознан")),str(T("This Codex version has no supported list of local paths. Add the folder under Projects. No Codex state was overwritten.","В этой версии Codex нет поддерживаемого списка локальных путей. Добавьте папку через «Проекты». Никакие состояния Codex не перезаписаны.")));return;}
 Int added=importProjectList(snapshot);save();buildUI();showError(str(T("AppDeck list updated","Список AppDeck обновлён")),formatInt(T("Folders added: %ld. Existing Codex sidebars were not overwritten.","Добавлено папок: %ld. Действующие боковые панели Codex не перезаписывались."),added));
}
void toggleHistoryAction(Obj,Sel,Obj){
 Obj a=currentApp();if(!a||deck::adapter(utf8(get(a,"adapter")))!=deck::Adapter::Codex)return;
 if(!baseSource(a)){showError(str(T("Connect the current Codex first","Сначала подключите текущий Codex")),str(T("Shared history comes from the base workspace. Click “Connect Codex…” and choose ~/.codex.","Общая история берётся из базовой среды. Нажмите «Подключить текущий Codex…» и выберите ~/.codex.")));return;}
 if(!historyAvailable(a)){showError(str(T("The base workspace has no thread database","В базовой среде нет базы тредов")),str(T("No state_*.sqlite file in the chosen folder. Open the primary Codex at least once and try again.","В выбранной папке не найден файл state_*.sqlite. Откройте основной Codex хотя бы один раз и повторите.")));return;}
 if(truth(get(a,"sharedHistory"))){
  Obj journal=groupSyncLoad(a),records=get(get(journal,"automations"),"records");
  if(count(records)){Obj members=groupSyncParticipants(a);for(UInt i=0;i<count(members);++i)if(groupParticipantRunning(a,at(members,i))){showError(str(T("Close the Codex instances first","Сначала закройте экземпляры Codex")),str(T("Before the shared workspace is turned off, automations and their owners must be reconciled. Close this group's instances and leave AppDeck open.","Перед отключением общей среды нужно согласовать автоматизации и их исполнителей. Закройте экземпляры этой группы, оставив AppDeck открытым.")));return;}
   if(Obj failure=syncGroup(a,true)){showError(str(T("Finish the sync first","Сначала завершите синхронизацию")),failure);return;}}
  if(!confirm(T("Turn off shared history?","Отключить общую историю?"),T("On their next launch the copies open their own thread databases again. Shared chats stay in the primary Codex and nothing is deleted; AppDeck removes only its own sessions / archived_sessions links from the copies.","При следующем запуске копии снова откроют собственные базы тредов. Общие чаты остаются в основном Codex, ничего не удаляется; AppDeck уберёт из копий только свои ссылки на sessions / archived_sessions."),T("Turn off","Отключить")))return;
  put(a,"sharedHistory",boolean(false));note("Shared history disabled; copies return to their own thread databases.");
 }else{erase(a,"sharedHistory");ensureHistoryChoice(a);}
 save();buildUI();
 for(UInt i=0;i<count(profiles);++i){Obj p=at(profiles,i);if(appFor(p)==a&&!isMaster(p)&&running(p)){showError(str(T("Takes effect after the copies restart","Применится после перезапуска копий")),str(T("Instances that are already running keep using the previous database. Close them and launch them again from AppDeck.","Уже запущенные экземпляры продолжают работать с прежней базой. Закройте и снова запустите их из AppDeck.")));break;}}
}
void noopAction(Obj,Sel,Obj){}
