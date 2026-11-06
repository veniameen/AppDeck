// Offline native scanner, reservations and Apple-event construction. No NSApplication, GUI,
// vendor executable, sign-in, event delivery, model turn or real profile access.
#define main appdeckApplicationMain
#include "../src/main.cpp"
#undef main
extern "C" int dprintf(int,const char*,...);
namespace {
unsigned assertions=0;Obj fixture=nullptr;
void check(bool pass,const char* detail){++assertions;if(!pass){dprintf(2,"FAIL: %s\nFixture: %s\n",detail,utf8(fixture));exit(1);}}
const char* account="10000000-0000-4000-8000-000000000001";
const char* org="20000000-0000-4000-8000-000000000001";
const char* first="30000000-0000-4000-8000-000000000001";
const char* second="30000000-0000-4000-8000-000000000002";
Obj nativeDir(Obj p){return join(join(join(claudeUserData(p),"claude-code-sessions"),account),org);}
Obj metadataPath(Obj p,const char* id){return join(nativeDir(p),cat(cat(str("local_"),str(id)),str(".json")));}
Obj claudeLeasePath(Obj a){return join(sharedRoot(a),"claude-code-lease.json");}
Obj rawRecord(const char* id,const char* title="Saved conversation"){
 Obj raw=dict();put(raw,"sessionId",cat(str("local_"),str(id)));put(raw,"cliSessionId",str(id));put(raw,"cwd",join(fixture,"project"));put(raw,"originCwd",join(fixture,"project"));put(raw,"title",str(title));put(raw,"isArchived",boolean(false));put(raw,"lastActivityAt",num(100));
 put(raw,"remoteMcpServersConfig",str("never-export-secret"));put(raw,"sessionSettings",str("never-export-permissions"));return raw;
}
Obj profile(Obj a,const char* id,bool primary){Obj p=dict();put(p,"id",str(id));put(p,"name",str(primary?"Primary":"Copy"));put(p,"appId",get(a,"id"));put(p,"master",boolean(primary));add(profiles,p);return p;}
Obj scan(Obj p,UInt expected){ClaudeScanBudget b;Obj records=claudeScanProfile(p,b);check(b.complete,"bounded scan completes");check(count(records)==expected,"expected native descriptor count");return records;}
bool contains(Obj value,const char* text){Obj e=nullptr,bytes=send(cls("NSJSONSerialization"),"dataWithJSONObject:options:error:",value,(UInt)0,&e);Obj s=autoRelease(send(send(cls("NSString"),"alloc"),"initWithData:encoding:",bytes,(UInt)4));return strstr(utf8(s),text)!=nullptr;}
}
int main(){
 umask(0077);appKit=dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8);check(appKit!=nullptr,"load system framework");Pool pool;
 fileManager=send(cls("NSFileManager"),"defaultManager");char temp[]="/private/tmp/appdeck-claude-test-XXXXXX";check(mkdtemp(temp)!=nullptr,"create isolated temporary root");fixture=keep(str(temp));
 home=keep(join(fixture,"home"));dataRoot=keep(join(fixture,"manager"));check(mkdirPrivate(home)&&mkdirPrivate(dataRoot)&&mkdirPrivate(join(fixture,"project")),"isolate every source and destination");
 state=keep(dict());apps=array();profiles=array();projects=array();events=keep(array());put(state,"apps",apps);put(state,"profiles",profiles);put(state,"projects",projects);
 Obj a=dict();put(a,"id",str("40000000-0000-4000-8000-000000000001"));put(a,"name",str("Fixture Claude"));put(a,"adapter",str("claude"));put(a,"path",join(fixture,"FixtureClaude.app"));add(apps,a);
 Obj primary=profile(a,"50000000-0000-4000-8000-000000000001",true),copy=profile(a,"50000000-0000-4000-8000-000000000002",false);
 check(mkdirPrivate(nativeDir(primary))&&mkdirPrivate(nativeDir(copy)),"create native account namespaces");
 Obj original=rawRecord(first);check(writeJSON(metadataPath(primary,first),original),"create original native session");Obj originalBytes=send(cls("NSData"),"dataWithContentsOfFile:",metadataPath(primary,first));
 check(writeText(join(nativeDir(primary),"auth.json"),str("not JSON and must never be read"))&&writeText(join(nativeDir(primary),"scheduled-tasks.json"),str("not a session")),"create forbidden and unrelated sentinels");
 Obj initial=scan(primary,1);check(!contains(initial,"never-export-secret")&&!contains(initial,"never-export-permissions")&&!contains(initial,"remoteMcpServersConfig"),"catalogue exports only allowlisted fields");check(count(scan(copy,0))==0,"copy begins empty without cloning");
 Obj rows=claudeLoadCatalogue(a);check(count(rows)==1&&claudeCatalogueComplete&&!claudeCatalogueSkipped,"shared catalogue discovers source session");
 Obj lease=claudeMakeLease(copy,at(rows,0),1234,1000,scan(copy,0),2000);check(claudeLeaseValid(lease),"well-formed reservation");check(!claudeAcknowledged(initial,lease),"source profile is never target acknowledgement");
 Obj invalid=autoRelease(send(lease,"mutableCopy"));put(invalid,"pid",real(1234.5));check(!claudeLeaseValid(invalid),"fractional pid cannot identify a reservation owner");put(invalid,"pid",num(1234));put(invalid,"version",real(1.5));check(!claudeLeaseValid(invalid),"fractional schema is not a supported reservation");put(invalid,"version",num(1));put(invalid,"started",num(0));check(!claudeLeaseValid(invalid),"reservation without a process start time is invalid");
 check(claudeWriteLease(a,lease),"durable reservation created under manager root");bool valid;check(same(claudeReadLease(a,valid),lease)&&valid,"reservation survives reload");
 struct stat privateStat{};check(stat(utf8(claudeLeasePath(a)),&privateStat)==0&&(privateStat.st_mode&0777)==0600,"reservation file stays private");check(stat(utf8(sharedRoot(a)),&privateStat)==0&&(privateStat.st_mode&0777)==0700,"reservation directory stays private");
 check(!claudeAcknowledged(scan(copy,0),lease),"event delivery alone cannot confirm import");
 check(writeJSON(metadataPath(copy,first),rawRecord(first)),"simulate Claude-owned target registration");Obj target=scan(copy,1);check(claudeAcknowledged(target,lease),"new target descriptor confirms registration");
 Obj repeat=claudeMakeLease(copy,at(rows,0),1234,1000,target,2001);check(!claudeAcknowledged(target,repeat),"old target descriptor does not acknowledge a new request");
 Obj changed=rawRecord(first,"Updated native title");check(writeJSON(metadataPath(copy,first),changed),"simulate native metadata refresh");check(claudeAcknowledged(scan(copy,1),repeat),"changed target descriptor confirms a repeated request");
 Obj archived=rawRecord(first);put(archived,"isArchived",boolean(true));check(writeJSON(metadataPath(copy,first),archived),"simulate archived target record");check(!claudeAcknowledged(scan(copy,1),repeat),"archived descriptor cannot acknowledge an open request");
 Obj wrong=rawRecord(first);put(wrong,"cwd",join(fixture,"different-project"));check(writeJSON(metadataPath(copy,first),wrong),"simulate conflicting native project");check(!claudeAcknowledged(scan(copy,1),repeat),"wrong project cannot acknowledge selected session");
 rows=claudeLoadCatalogue(a);check(count(rows)==1&&truth(get(at(rows,0),"conflict")),"duplicate session with different cwd blocks opening");
 check(writeJSON(metadataPath(copy,first),rawRecord(first)),"restore compatible fixture");rows=claudeLoadCatalogue(a);check(count(rows)==1&&count(get(at(rows,0),"records"))==2&&!truth(get(at(rows,0),"conflict")),"same transcript is deduplicated across accounts");
 check(same(originalBytes,send(cls("NSData"),"dataWithContentsOfFile:",metadataPath(primary,first))),"source descriptor remains byte-identical");
 Obj bad=rawRecord(second);put(bad,"cliSessionId",str("../unsafe"));check(writeJSON(metadataPath(copy,second),bad),"malformed native fixture");ClaudeScanBudget b;check(count(claudeScanProfile(copy,b))==1&&b.skipped==1,"malformed metadata is reported rather than exported");
 Obj er=nullptr;check(send<bool>(fileManager,"removeItemAtPath:error:",metadataPath(copy,second),&er),"remove only malformed fixture");
 check(send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",metadataPath(copy,second),metadataPath(primary,first),&er),"symlink fixture");b={};check(count(claudeScanProfile(copy,b))==1&&b.skipped==1,"symlink metadata is refused");
 check(send<bool>(fileManager,"removeItemAtPath:error:",metadataPath(copy,second),&er),"remove only symlink fixture");check(link(utf8(metadataPath(primary,first)),utf8(metadataPath(copy,second)))==0,"hard-link fixture");b={};check(count(claudeScanProfile(copy,b))==1&&b.skipped==1,"multiply-linked metadata is refused");
 check(unlink(utf8(metadataPath(copy,second)))==0,"remove only hard-link fixture");
 int large=open(utf8(metadataPath(copy,second)),O_WRONLY|O_CREAT|O_EXCL,0600);check(large>=0&&ftruncate(large,claudeMetadataMax+1)==0,"oversized metadata fixture");close(large);b={};check(count(claudeScanProfile(copy,b))==1&&b.skipped==1,"oversized record is refused before reading");check(unlink(utf8(metadataPath(copy,second)))==0,"remove only oversized fixture");
 check(writeJSON(metadataPath(copy,second),rawRecord(second)),"second local session");b={};b.files=claudeScanMaxFiles;check(count(claudeScanProfile(copy,b))==0&&!b.complete,"scan file budget fails closed");b={};b.bytes=claudeScanMaxBytes;check(count(claudeScanProfile(copy,b))==0&&!b.complete,"scan byte budget fails closed");
 Obj saved=claudeReadLease(a,valid),next=autoRelease(send(saved,"mutableCopy"));put(next,"status",str("confirmed"));check(claudeWriteLease(a,next,saved),"acknowledgement updates reservation atomically");check(!claudeWriteLease(a,saved,saved),"changed journal input cannot be overwritten by stale acknowledgement");
 Obj malformed=dict();put(malformed,"version",array());check(writeJSON(claudeLeasePath(a),malformed),"malformed reservation fixture");check(!claudeReadLease(a,valid)&&!valid,"malformed reservation is not an empty owner registry");check(!claudeWriteLease(a,lease),"malformed reservation is preserved");
 check(unlink(utf8(claudeLeasePath(a)))==0,"remove only malformed fixture reservation");check(send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",claudeLeasePath(a),metadataPath(primary,first),&er),"unsafe reservation fixture");check(!claudeWriteLease(a,lease),"reservation never replaces a symlink");check(unlink(utf8(claudeLeasePath(a)))==0,"remove only reservation symlink");
 put(a,"claudeBridgeLeaseRecorded",boolean(true));check(!claudeReadLease(a,valid)&&!valid,"disappearing recorded journal never releases ownership");check(!claudeWriteLease(a,lease),"missing recorded journal cannot be silently recreated");erase(a,"claudeBridgeLeaseRecorded");check(count(claudeReadLease(a,valid))==0&&valid,"only genuine first use admits an absent journal");
 check(rmdir(utf8(sharedRoot(a)))==0,"remove empty fixture group directory");check(mkdirPrivate(join(fixture,"journal-link-target")),"isolated link target");check(send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",sharedRoot(a),join(fixture,"journal-link-target"),&er),"linked journal parent fixture");check(!claudeReadLease(a,valid)&&!valid&&!claudeWriteLease(a,lease),"journal directory links are refused for reads and writes");check(unlink(utf8(sharedRoot(a)))==0,"remove only linked journal parent");
 check(!claudeTranscriptPresent(str(first))&&!claudeTranscriptPresent(str("../unsafe")),"missing and injected transcript ids are refused");Obj transcriptDir=join(home,".claude/projects/fixture-project"),transcript=join(transcriptDir,cat(str(first),str(".jsonl")));check(mkdirPrivate(transcriptDir)&&writeText(transcript,str("fixture transcript sentinel")),"isolated transcript existence fixture");check(claudeTranscriptPresent(str(first)),"regular local transcript discovered without reading content");check(unlink(utf8(transcript))==0&&send<bool>(fileManager,"createSymbolicLinkAtPath:withDestinationPath:error:",transcript,metadataPath(primary,first),&er),"linked transcript fixture");check(!claudeTranscriptPresent(str(first)),"transcript links are never followed");check(unlink(utf8(transcript))==0&&link(utf8(metadataPath(primary,first)),utf8(transcript))==0,"multiply-linked transcript fixture");check(!claudeTranscriptPresent(str(first)),"multiply-linked transcript is not admitted");check(unlink(utf8(transcript))==0,"remove only transcript link fixture");
 Obj event=claudeResumeEvent(1234,str(first));check(event!=nullptr,"construct pid-addressed native event without sending");check(send<unsigned>(event,"eventClass")==0x4755524c&&send<unsigned>(event,"eventID")==0x4755524c,"public GURL event class and id");
 Obj direct=send(event,"paramDescriptorForKeyword:",(unsigned)0x2d2d2d2d);check(same(send(direct,"stringValue"),cat(str("claude://resume?session="),str(first))),"event contains only a validated session URL");
 check(!claudeResumeEvent(0,str(first))&&!claudeResumeEvent(1234,str("invalid&account=other")),"invalid pid and injected URL cannot construct an event");
 Obj targetAddress=send(event,"attributeDescriptorForKeyword:",(unsigned)0x61646472);check(send<unsigned>(targetAddress,"descriptorType")==0x6b706964,"event targets a kernel pid rather than bundle-wide LaunchServices");
 Obj addressBytes=send(targetAddress,"data");check(send<UInt>(addressBytes,"length")==sizeof(int)&&*(const int*)send<const void*>(addressBytes,"bytes")==1234,"exact selected pid encoded in event");
 Obj info=dict();put(info,"CFBundleIdentifier",str("com.anthropic.claudefordesktop"));put(info,"CFBundleShortVersionString",str("2.31226.1"));put(info,"CFBundleExecutable",str("FixtureClaude"));
 check(mkdirPrivate(join(get(a,"path"),"Contents"))&&send<bool>(info,"writeToFile:atomically:",join(get(a,"path"),"Contents/Info.plist"),true),"create version-only fixture bundle");check(claudeBridgeSupported(a),"inspected native version admitted without launching it");
 Obj supportedPath=get(a,"path"),unknownPath=join(fixture,"UnknownClaude.app");put(info,"CFBundleShortVersionString",str("2.31226.2"));check(mkdirPrivate(join(unknownPath,"Contents"))&&send<bool>(info,"writeToFile:atomically:",join(unknownPath,"Contents/Info.plist"),true),"unknown version fixture bundle");put(a,"path",unknownPath);check(!claudeBridgeSupported(a),"uninspected native version disables import without a launch");put(a,"path",supportedPath);
 check(claudePrivateChild(-1,"../outside")==-1,"private journal child cannot escape root");
 dprintf(1,"%u offline Claude session assertions passed; no application or vendor helper was launched.\n",assertions);
 check(send<bool>(fileManager,"removeItemAtPath:error:",fixture,&er),"remove only isolated test fixture");return 0;
}
