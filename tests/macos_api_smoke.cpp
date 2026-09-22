#include "../src/mac.hpp"
using namespace mac;
extern "C" int puts(const char*);
extern "C" int getpid();
extern "C" int dprintf(int,const char*,...);
#include "../src/ax_api.hpp"
#include "../src/process_args.hpp"
#include "../src/usage_probe.hpp"
// Modes:  (none)                     API surface + in-process checks
//         --user-data-dir=<path>     the argument parser must find this marker in our own argv
//         --claude-probe <claude> <configDir> <cwd>   REAL exchange with Claude Code on a signed-out
//                                    scratch folder: spawn, control request, control reply, reap
//         --ax-log <file>            run as a standalone bundle (own, untrusted TCC identity): the
//                                    no-prompt trust query crashed 0.2.0-preview right here
int main(int argc,char** argv){
 void* framework=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);
 if(!framework){puts("FAIL: AppKit did not load");return 1;}
 Pool pool;
 if(argc>2&&!strcmp(argv[1],"--ax-log")){int fd=open(argv[2],0x0001|0x0200|0x0400,0600);WindowAPI api;bool trusted=api.permission(false);
  dprintf(fd,"ax-query-survived trusted=%d\n",(int)trusted);close(fd);return 0;}
 // Diagnostic: what the production probe makes of a given Claude Code config folder (no e-mail printed).
 if(argc>4&&!strcmp(argv[1],"--claude-dump")){
  Obj env=dict();for(const char* k:{"HOME","PATH","USER","TMPDIR","LANG"}){const char* v=getenv(k);if(v)put(env,k,str(v));}put(env,"CLAUDE_CONFIG_DIR",str(argv[3]));for(const char* off:{"DISABLE_AUTOUPDATER","DISABLE_ERROR_REPORTING","DISABLE_TELEMETRY"})put(env,off,str("1"));
  Obj request=dict();put(request,"kind",str("claude"));put(request,"binary",str(argv[2]));put(request,"scratch",str(argv[4]));put(request,"environment",env);Obj result=dict();usageProbeClaude(request,result,true);
  Obj limits=get(result,"limits"),windows=get(limits,"windows");dprintf(1,"signedIn=%d plan=%s error=%s windows=%lu\n",(int)truth(get(get(result,"account"),"signedIn")),utf8(get(limits,"plan")),utf8(get(result,"error")),count(windows));
  for(UInt i=0;i<count(windows);++i){Obj w=at(windows,i);dprintf(1,"  %-8s minutes=%ld remaining=%d%% resets=%.0f\n",get(w,"label")?utf8(get(w,"label")):"-",integer(get(w,"minutes")),deck::remainingPercent(send<double>(get(w,"used"),"doubleValue")),send<double>(get(w,"resets"),"doubleValue"));}
  return count(windows)?0:12;}
 if(argc>4&&!strcmp(argv[1],"--claude-probe")){
  Obj env=dict();for(const char* k:{"HOME","PATH","USER","TMPDIR","LANG"}){const char* v=getenv(k);if(v)put(env,k,str(v));}put(env,"CLAUDE_CONFIG_DIR",str(argv[3]));for(const char* off:{"DISABLE_AUTOUPDATER","DISABLE_ERROR_REPORTING","DISABLE_TELEMETRY"})put(env,off,str("1"));
  Obj request=dict();put(request,"kind",str("claude"));put(request,"binary",str(argv[2]));put(request,"scratch",str(argv[4]));put(request,"environment",env);Obj result=dict();
  double started=usageUptime();usageProbeClaude(request,result,false);double took=usageUptime()-started;
  Obj account=get(result,"account"),limits=get(result,"limits"),error=get(result,"error");
  if(!account||truth(get(account,"signedIn"))){puts("FAIL: scratch Claude Code folder must report signed out");return 11;}
  if(!limits||!usageKind(get(limits,"windows"),"NSArray")||count(get(limits,"windows"))||truth(get(limits,"available"))||!same(error,str("noplan"))){puts(error?utf8(error):"(no error)");puts("FAIL: get_usage exchange with Claude Code did not return the expected signed-out reply");return 11;}
  if(took>deck::usageProbeTimeout*2){puts("FAIL: Claude Code probe exceeded its deadline");return 11;}
  puts("PASS: real get_usage exchange with Claude Code (signed-out scratch folder, --safe-mode, no model turn).");return 0;}
 for(int i=1;i<argc;++i)if(!strncmp(argv[i],"--user-data-dir=",16)){Obj found=processUserDataDir(getpid());
  if(!found||strcmp(utf8(found),argv[i]+16)){puts("FAIL: --user-data-dir marker not recovered from KERN_PROCARGS2");return 9;}puts("PASS: own --user-data-dir recovered through sysctl(KERN_PROCARGS2).");return 0;}
 struct Method{const char* cls;const char* sel;};
 Method required[]={
  {"NSWorkspace","launchApplicationAtURL:options:configuration:error:"},
  {"NSWorkspace","runningApplications"},
  {"NSPanel","setFloatingPanel:"}, {"NSPanel","setBecomesKeyOnlyIfNeeded:"},
  {"NSPanel","setHidesOnDeactivate:"}, {"NSPanel","setCollectionBehavior:"},
  {"NSVisualEffectView","setMaterial:"}, {"NSVisualEffectView","setBlendingMode:"},
  {"NSVisualEffectView","setState:"}, {"NSView","setAlphaValue:"},
  {"NSWorkspace","accessibilityDisplayShouldReduceMotion"},
  {"NSWorkspace","accessibilityDisplayShouldReduceTransparency"},
  {"NSRunLoop","addTimer:forMode:"}, {"NSButton","setAccessibilityLabel:"},
  {"NSRunningApplication","activateWithOptions:"},
  {"NSRunningApplication","launchDate"},
  {"NSWindow","setContentView:"},
  {"NSTextField","setMaximumNumberOfLines:"},
  {"NSButton","setContentTintColor:"},
  {"NSFileManager","homeDirectoryForCurrentUser"},
  {"NSMenu","popUpMenuPositioningItem:atLocation:inView:"},
  {"NSTrackingArea","initWithRect:options:owner:userInfo:"}, {"NSView","addTrackingArea:"},
  {"NSImage","imageWithSymbolConfiguration:"}, {"CALayer","setMaskedCorners:"},
  {"NSView","bitmapImageRepForCachingDisplayInRect:"}, {"NSView","cacheDisplayInRect:toBitmapImageRep:"},
  {"NSRunningApplication","bundleIdentifier"}, {"NSButton","setImagePosition:"},
  {"NSApplication","activateIgnoringOtherApps:"},
  {"NSAppleEventDescriptor","sendEventWithOptions:timeout:error:"},
  {"NSTask","launchAndReturnError:"}, {"NSTask","setExecutableURL:"}, {"NSTask","setCurrentDirectoryURL:"},
  {"NSTask","setEnvironment:"}, {"NSFileHandle","fileDescriptor"}, {"NSCalendar","isDateInTomorrow:"},
  {"NSDateFormatter","setDateFormat:"}, {"NSISO8601DateFormatter","setFormatOptions:"}, {"NSISO8601DateFormatter","dateFromString:"}, {"NSObject","performSelectorOnMainThread:withObject:waitUntilDone:"}
 };
 struct ClassMethod{const char* cls;const char* sel;};
 for(auto m:{ClassMethod{"NSImageSymbolConfiguration","configurationWithPointSize:weight:"},ClassMethod{"NSButton","checkboxWithTitle:target:action:"},ClassMethod{"NSAppleEventDescriptor","descriptorWithProcessIdentifier:"},ClassMethod{"NSAppleEventDescriptor","appleEventWithEventClass:eventID:targetDescriptor:returnID:transactionID:"},ClassMethod{"NSImage","imageWithSystemSymbolName:accessibilityDescription:"}})
  if(!send<bool>(cls(m.cls),"respondsToSelector:",sel(m.sel))){puts(m.cls);puts(m.sel);puts("FAIL: missing public class selector");return 2;}
 for(auto m:required)if(!send<bool>(cls(m.cls),"instancesRespondToSelector:",sel(m.sel))){puts(m.cls);puts(m.sel);puts("FAIL: missing public selector");return 2;}
 if(!publicConstant(framework,"NSWorkspaceLaunchConfigurationEnvironment")||!publicConstant(framework,"NSWorkspaceLaunchConfigurationArguments")||!publicConstant(framework,"NSAppearanceNameDarkAqua")){puts("FAIL: missing public AppKit constant");return 3;}
 void* ax=dlopen("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices",2);
 const char* symbols[]={"AXIsProcessTrustedWithOptions","AXUIElementCreateApplication","AXUIElementCopyAttributeValue","AXUIElementSetAttributeValue","AXUIElementPerformAction","AXValueCreate","AXValueGetValue","AXUIElementSetMessagingTimeout","CFRelease","CFRetain"};
 for(auto symbol:symbols)if(!ax||!dlsym(ax,symbol)){puts(symbol);puts("FAIL: missing Accessibility symbol");return 7;}
 if(!dlsym(ax,"AXIsProcessTrusted")){puts("FAIL: AXIsProcessTrusted missing");return 7;}
 {WindowAPI api;bool trusted=api.permission(false);puts(trusted?"Accessibility (this process tree): granted":"Accessibility (this process tree): not granted");}
 if(processUserDataDir(getpid())){puts("FAIL: phantom --user-data-dir reported for a process without it");return 9;}
 void* carbon=dlopen("/System/Library/Frameworks/Carbon.framework/Carbon",2);
 for(auto symbol:{"GetApplicationEventTarget","InstallEventHandler","RegisterEventHotKey","UnregisterEventHotKey","RemoveEventHandler","GetEventParameter"})if(!carbon||!dlsym(carbon,symbol)){puts(symbol);puts("FAIL: missing hotkey symbol");return 8;}
 Obj view=send(send(cls("NSView"),"alloc"),"initWithFrame:",rect(13,17,101,203));Rect got=getRect(view,"frame");
 if(got.origin.x!=13||got.origin.y!=17||got.size.width!=101||got.size.height!=203){puts("FAIL: NSRect ABI");return 4;}drop(view);
 Obj d=dict();put(d,"test",str("Привет / Hello"));if(!same(get(d,"test"),str("Привет / Hello"))){puts("FAIL: Foundation bridge");return 5;}
 if(!truth(boolean(true))||truth(boolean(false))){puts("FAIL: NSNumber bool ABI");return 6;}
 // Account-limit replies of `codex app-server`, in the shapes this Codex build really sends.
 {auto parse=[](const char* text){return usageJSON(text,strlen(text));};
  // Pro: the weekly window arrives as `primary`, there is no 5-hour window, a credits-only bucket sits beside it.
  Obj pro=usageRecord(get(parse("{\"id\":3,\"result\":{\"rateLimits\":{\"limitId\":\"codex\",\"primary\":{\"usedPercent\":63,\"windowDurationMins\":10080,\"resetsAt\":1789805440},\"secondary\":null,\"planType\":\"pro\"},\"rateLimitsByLimitId\":{\"premium\":{\"primary\":null,\"secondary\":null,\"planType\":\"pro\"},\"codex\":{\"primary\":{\"usedPercent\":63,\"windowDurationMins\":10080,\"resetsAt\":1789805440},\"secondary\":null,\"planType\":\"pro\"}}}}"),"result"));
  Obj pw=get(pro,"windows");if(count(pw)!=1||integer(get(at(pw,0),"minutes"))!=10080||deck::remainingPercent(send<double>(get(at(pw,0),"used"),"doubleValue"))!=37||!same(get(pro,"plan"),str("pro"))){puts("FAIL: Pro-shaped limits reply");return 10;}
  // Plus: short window first, weekly second -> AppDeck must put the weekly one first.
  Obj plus=usageRecord(get(parse("{\"result\":{\"rateLimits\":{\"primary\":{\"usedPercent\":12.5,\"windowDurationMins\":300,\"resetsAt\":1789750000},\"secondary\":{\"usedPercent\":88,\"windowDurationMins\":10080,\"resetsAt\":1789900000},\"planType\":\"plus\",\"rateLimitReachedType\":null}}}"),"result"));
  Obj lw=get(plus,"windows");if(count(lw)!=2||integer(get(at(lw,0),"minutes"))!=10080||integer(get(at(lw,1),"minutes"))!=300||get(plus,"reached")){puts("FAIL: Plus-shaped limits reply");return 10;}
  // Hostile / malformed payloads degrade to "no windows" instead of crashing.
  for(const char* bad:{"{\"result\":{\"rateLimits\":{\"primary\":{\"usedPercent\":\"lots\"},\"secondary\":[1,2]}}}","{\"result\":{\"rateLimits\":42,\"rateLimitsByLimitId\":[]}}","{\"result\":{}}","{\"result\":null}"})
   if(count(get(usageRecord(get(parse(bad),"result")),"windows"))){puts("FAIL: malformed limits reply produced windows");return 10;}
  if(usageJSON("[1,2,3]",7)||usageJSON("not json",8)||usageJSON(nullptr,0)){puts("FAIL: non-object JSON accepted");return 10;}
  Obj in=usageAccount(get(parse("{\"result\":{\"account\":{\"type\":\"chatgpt\",\"email\":\"user@example.com\",\"planType\":\"pro\"},\"requiresOpenaiAuth\":true}}"),"result"));
  Obj out=usageAccount(get(parse("{\"result\":{\"account\":null,\"requiresOpenaiAuth\":true}}"),"result"));
  if(!truth(get(in,"signedIn"))||!same(get(in,"email"),str("user@example.com"))||truth(get(out,"signedIn"))||get(out,"email")){puts("FAIL: account reply");return 10;}
  if(strcmp(usageErrorKind(get(parse("{\"error\":{\"code\":-32600,\"message\":\"codex account authentication required to read rate limits\"}}"),"error")),"auth")||strcmp(usageErrorKind(get(parse("{\"error\":{\"code\":-32601,\"message\":\"method not found\"}}"),"error")),"unsupported")||strcmp(usageErrorKind(get(parse("{\"error\":{\"code\":-32000,\"message\":\"upstream unavailable\"}}"),"error")),"error")){puts("FAIL: error classification");return 10;}
  // Claude Code: get_usage body as documented by the CLI's own schema (five_hour, seven_day, model_scoped[…]).
  Obj claude=usageRecordClaude(get(get(parse("{\"type\":\"control_response\",\"response\":{\"subtype\":\"success\",\"request_id\":\"appdeck-usage\",\"response\":{\"subscription_type\":\"max\",\"rate_limits_available\":true,\"behaviors\":null,\"rate_limits\":{\"five_hour\":{\"utilization\":6,\"resets_at\":\"2026-09-19T17:21:00Z\"},\"seven_day\":{\"utilization\":36.4,\"resets_at\":\"2026-09-25T08:00:00.123456+00:00\"},\"seven_day_opus\":null,\"seven_day_sonnet\":null,\"model_scoped\":[{\"display_name\":\"Fable\",\"utilization\":71,\"resets_at\":\"2026-09-25T08:00:00+00:00\"},{\"display_name\":\"\",\"utilization\":5,\"resets_at\":null},\"junk\"],\"extra_usage\":null}}}}"),"response"),"response"));
  Obj cw=get(claude,"windows");
  if(count(cw)!=3||integer(get(at(cw,0),"minutes"))!=10080||get(at(cw,0),"label")||deck::remainingPercent(send<double>(get(at(cw,0),"used"),"doubleValue"))!=64
   ||integer(get(at(cw,1),"minutes"))!=300||deck::remainingPercent(send<double>(get(at(cw,1),"used"),"doubleValue"))!=94
   ||!same(get(at(cw,2),"label"),str("Fable"))||deck::remainingPercent(send<double>(get(at(cw,2),"used"),"doubleValue"))!=29||!same(get(claude,"plan"),str("max"))||!truth(get(claude,"available"))){puts("FAIL: Claude get_usage reply");return 10;}
  // 2026-09-25T08:00:00Z == 1790323200; fractional seconds and offsets must parse to the same instant.
  double a=send<double>(get(at(cw,0),"resets"),"doubleValue"),b=send<double>(get(at(cw,2),"resets"),"doubleValue");if(b!=1790323200.0||a<b||a-b>1){puts("FAIL: ISO 8601 reset time");return 10;}
  if(usageISO8601(str("yesterday"))!=0||usageISO8601(nullptr)!=0||usageISO8601(str(""))!=0){puts("FAIL: bad ISO 8601 accepted");return 10;}
  Obj legacy=usageRecordClaude(get(parse("{\"r\":{\"subscription_type\":\"pro\",\"rate_limits_available\":true,\"rate_limits\":{\"five_hour\":null,\"seven_day\":{\"utilization\":10,\"resets_at\":null},\"seven_day_opus\":{\"utilization\":50,\"resets_at\":null}}}}"),"r"));
  if(count(get(legacy,"windows"))!=2||!same(get(at(get(legacy,"windows"),1),"label"),str("Opus"))){puts("FAIL: legacy per-model windows");return 10;}
  for(const char* bad:{"{\"r\":{\"rate_limits_available\":false,\"rate_limits\":null}}","{\"r\":{\"rate_limits\":[1,2]}}","{\"r\":{\"rate_limits\":{\"five_hour\":7,\"seven_day\":\"x\",\"model_scoped\":{\"a\":1}}}}","{\"r\":42}"})
   if(count(get(usageRecordClaude(get(parse(bad),"r")),"windows"))){puts("FAIL: malformed Claude reply produced windows");return 10;}
  Obj cin=usageAccountClaude(parse("{\"loggedIn\":true,\"authMethod\":\"claude.ai\",\"email\":\"user@example.com\",\"subscriptionType\":\"max\"}")),cout=usageAccountClaude(parse("{\"loggedIn\":false,\"authMethod\":\"none\"}"));
  if(!truth(get(cin,"signedIn"))||!same(get(cin,"email"),str("user@example.com"))||!same(get(cin,"plan"),str("max"))||truth(get(cout,"signedIn"))||truth(get(usageAccountClaude(parse("{\"loggedIn\":\"yes\"}")),"signedIn"))){puts("FAIL: Claude auth status reply");return 10;}
  // The shape a signed-in Max account REALLY returns (0.7.1): limits[] with kind/percent/scope; model_scoped may be absent.
  Obj real=usageRecordClaude(get(parse("{\"r\":{\"subscription_type\":\"max\",\"rate_limits_available\":true,\"rate_limits\":{\"five_hour\":{\"utilization\":11,\"resets_at\":\"2026-09-19T14:00:00.022299+00:00\",\"limit_dollars\":null},\"seven_day\":{\"utilization\":37,\"resets_at\":\"2026-09-25T08:00:00.022328+00:00\"},\"seven_day_opus\":null,\"model_scoped\":null,\"nimbus_quill\":{\"utilization\":0,\"resets_at\":null},\"limits\":[{\"kind\":\"session\",\"group\":\"session\",\"percent\":11,\"resets_at\":\"2026-09-19T14:00:00.367825+00:00\",\"scope\":null,\"severity\":\"normal\",\"is_active\":false},{\"kind\":\"weekly_all\",\"group\":\"weekly\",\"percent\":37,\"resets_at\":\"2026-09-25T08:00:00.367850+00:00\",\"scope\":null},{\"kind\":\"weekly_scoped\",\"group\":\"weekly\",\"percent\":73,\"resets_at\":\"2026-09-25T08:00:00.368023+00:00\",\"scope\":{\"model\":{\"display_name\":\"Fable\",\"id\":null},\"surface\":null},\"is_active\":true},{\"kind\":\"weekly_scoped\",\"percent\":5,\"scope\":null},{\"kind\":42},\"junk\"]}}}"),"r"));
  Obj rw=get(real,"windows");
  if(count(rw)!=3||integer(get(at(rw,0),"minutes"))!=10080||get(at(rw,0),"label")||deck::remainingPercent(send<double>(get(at(rw,0),"used"),"doubleValue"))!=63
   ||integer(get(at(rw,1),"minutes"))!=300||deck::remainingPercent(send<double>(get(at(rw,1),"used"),"doubleValue"))!=89
   ||!same(get(at(rw,2),"label"),str("Fable"))||deck::remainingPercent(send<double>(get(at(rw,2),"used"),"doubleValue"))!=27||send<double>(get(at(rw,1),"resets"),"doubleValue")<1789826400||send<double>(get(at(rw,1),"resets"),"doubleValue")>1789826401){puts("FAIL: real-shape Claude limits[] reply");return 10;}
  if(count(get(usageRecordClaude(get(parse("{\"r\":{\"rate_limits_available\":true,\"rate_limits\":{\"limits\":\"oops\"}}}"),"r")),"windows"))||count(get(usageRecordClaude(get(parse("{\"r\":{\"rate_limits_available\":true,\"rate_limits\":{\"limits\":[]}}}"),"r")),"windows"))){puts("FAIL: malformed limits[]");return 10;}
  puts("PASS: account-limit replies (Codex Pro/Plus, Claude limits[] real shape + legacy fields, ISO dates, malformed input, accounts, errors).");}
 puts("PASS: public selectors/constants, NSRect return ABI, Foundation/boolean round-trips, no-prompt AX query, sysctl argv.");
 puts("This is not a GUI, LaunchServices or multi-account acceptance test.");return 0;
}
