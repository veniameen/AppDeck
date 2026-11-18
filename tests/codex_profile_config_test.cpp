// Exercise the production pre-launch preparation without launching a vendor application.
#define main appdeckApplicationMain
#include "../src/main.cpp"
#undef main

namespace {
Obj scratch;
unsigned assertions;

void checkConfig(bool ok,const char* detail){
 ++assertions;
 if(!ok){fprintf(stderr,"FAIL: %s\nScratch root: %s\n",detail,utf8(scratch));exit(1);}
}

Obj newCopy(Obj group,bool shared){
 Obj profile=dict();
 put(profile,"id",uuid());put(profile,"appId",get(group,"id"));put(profile,"share",boolean(shared));
 add(profiles,profile);
 return profile;
}

Obj configPath(Obj profile){return join(profileRoot(profile),"codex/config.toml");}

bool privateConfig(Obj path){
 struct stat mode{};
 return lstat(utf8(path),&mode)==0&&S_ISREG(mode.st_mode)&&(mode.st_mode&0777)==0600;
}

void prepareCopy(Obj profile){
 Obj failure=prepare(profile);
 if(failure)fprintf(stderr,"Preparation failed: %s\n",utf8(failure));
 checkConfig(!failure,"production profile preparation succeeds");
}

const char* const retainedSettings=
 "forced_chatgpt_workspace_id = \"fixture-organization\"\n"
 "[plugins.\"example@openai-bundled\"]\nenabled = false\n"
 "[mcp_servers.example]\ncommand = \"example-mcp\"\n"
 "[marketplaces.team]\nsource_type = \"local\"\nsource = \"/team/curated\"\n";

Obj bundledSource(Obj codexHome){return join(codexHome,".tmp/bundled-marketplaces/openai-bundled");}

Obj fixtureConfig(Obj codexHome){
 return cat(cat(str(retainedSettings),str("[marketplaces.openai-bundled]\nsource_type = \"local\"\nsource = \"")),
            cat(bundledSource(codexHome),str("\"\n")));
}

void checkPrepared(Obj profile,Obj staleHome){
 Obj value=readText(configPath(profile));
 checkConfig(value&&strstr(utf8(value),retainedSettings),"preserve organization, plugin, MCP and third-party marketplace settings");
 checkConfig(strstr(utf8(value),"cli_auth_credentials_store = \"file\""),"prepare the profile's own file-based sign-in store");
 checkConfig(!strstr(utf8(value),utf8(bundledSource(staleHome))),"omit the inherited bundled marketplace source");
 Obj expected=cat(str("source = \""),cat(bundledSource(join(profileRoot(profile),"codex")),str("\"")));
 checkConfig(strstr(utf8(value),utf8(expected)),"use this copy's own bundled marketplace source");
}
}

int main(){
 umask(0077);
 appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);if(!appKit)return 10;
 Pool pool;
 fileManager=send(cls("NSFileManager"),"defaultManager");
 scratch=join(str("/private/tmp"),cat(str("appdeck-codex-config-"),uuid()));
 checkConfig(!exists(scratch)&&mkdirPrivate(scratch),"create fresh scratch root");
 home=join(scratch,"home");dataRoot=join(scratch,"Application Support/AppDeck");
 checkConfig(mkdirPrivate(home)&&mkdirPrivate(dataRoot),"create isolated home and manager directories");
 apps=array();profiles=array();events=array();
 Obj group=dict(),sourceHome=join(home,".codex");
 put(group,"id",uuid());put(group,"adapter",str("codex"));put(group,"baseSource",sourceHome);add(apps,group);
 checkConfig(mkdirPrivate(sourceHome),"create isolated source workspace");
 Obj sourceConfig=fixtureConfig(sourceHome),sourcePath=join(sourceHome,"config.toml");
 checkConfig(writeText(sourcePath,sourceConfig),"write source configuration");

 Obj first=newCopy(group,true),second=newCopy(group,true);
 prepareCopy(first);checkPrepared(first,sourceHome);
 Obj secondPath=configPath(second);
 checkConfig(mkdirPrivate(join(profileRoot(second),"codex"))&&writeText(secondPath,sourceConfig),"seed stale registration in an existing linked copy");
 prepareCopy(second);checkPrepared(second,sourceHome);
 Obj firstConfig=readText(configPath(first));
 Obj secondConfig=readText(secondPath),secondPrevious=cat(secondPath,str(".previous"));
 checkConfig(!strstr(utf8(secondConfig),utf8(join(profileRoot(first),"codex"))),"second linked copy has no path into the first");
 checkConfig(same(readText(secondPrevious),sourceConfig),"save the linked copy's pre-repair configuration");
 prepareCopy(second);
 checkConfig(same(readText(secondPath),secondConfig)&&same(readText(secondPrevious),sourceConfig),"linked relaunch preserves its registration while the source still points at the base");

 Obj detached=newCopy(group,false),detachedPath=configPath(detached);
 checkConfig(mkdirPrivate(join(profileRoot(detached),"codex")),"create an existing detached profile");
 Obj stale=cat(str("model = \"detached-local\"\n"),sourceConfig);
 checkConfig(writeText(detachedPath,stale)&&chmod(utf8(detachedPath),0644)==0,"seed previously inherited local configuration with broad permissions");
 prepareCopy(detached);checkPrepared(detached,sourceHome);
 Obj repaired=readText(detachedPath),previous=cat(detachedPath,str(".previous"));
 checkConfig(strstr(utf8(repaired),"model = \"detached-local\""),"preserve detached settings");
 checkConfig(same(readText(previous),stale),"retain original detached configuration for recovery");
 checkConfig(privateConfig(detachedPath)&&privateConfig(previous),"repaired config and its backup use mode 0600");
 checkConfig(chmod(utf8(detachedPath),0644)==0,"broaden permissions on the repaired config");
 prepareCopy(detached);
 checkConfig(same(readText(detachedPath),repaired)&&same(readText(previous),stale),"repeat preparation preserves repaired configuration and original backup");
 checkConfig(privateConfig(detachedPath)&&privateConfig(previous),"restore mode 0600 on unchanged config and retain private backup");

 Obj correct=newCopy(group,false),correctPath=configPath(correct);
 Obj correctConfig=replace(firstConfig,utf8(profileRoot(first)),utf8(profileRoot(correct)));
 checkConfig(mkdirPrivate(join(profileRoot(correct),"codex"))&&writeText(correctPath,correctConfig),"seed a correctly registered detached copy");
 prepareCopy(correct);
 checkConfig(same(readText(correctPath),correctConfig)&&!exists(cat(correctPath,str(".previous"))),"retain an already correct local registration without a new backup");

 checkConfig(same(readText(configPath(first)),firstConfig),"preparing peers leaves the first copy intact");
 checkConfig(same(readText(sourcePath),sourceConfig)&&!exists(cat(sourcePath,str(".previous"))),"source configuration remains byte-identical and has no backup writes");
 // Settings imported into the AppDeck group still name the original vendor cache.
 erase(group,"baseSource");
 Obj importedPath=join(sharedRoot(group),"config.toml");
 checkConfig(writeText(importedPath,sourceConfig),"seed settings previously imported from the base");
 Obj imported=newCopy(group,true);
 prepareCopy(imported);checkPrepared(imported,sourceHome);
 checkConfig(same(readText(importedPath),sourceConfig),"retain the imported source for all group members");
 Obj error=nullptr;
 checkConfig(send<bool>(fileManager,"removeItemAtPath:error:",scratch,&error),"remove synthetic scratch root");
 printf("%u native Codex profile configuration assertions passed\n",assertions);
 return 0;
}
