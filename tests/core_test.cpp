#include "../src/codex_config.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
int main(){
 int n=0;auto check=[&](bool b){if(!b){std::cerr<<"FAIL assertion "<<n+1<<"\n";std::exit(1);}++n;};
 check(deck::uuid("00000000-1234-abcd-EF01-000000000000"));
 check(!deck::uuid("../../other"));check(!deck::uuid("00000000-1234-abcd-EF01-00000000000Z"));
 check(deck::inside("/data/AppDeck","/data/AppDeck/profiles/id"));check(!deck::inside("/data/AppDeck","/data/AppDeck2"));
 check(!deck::safeLeaf("../evil"));check(!deck::safeLeaf(".."));check(deck::safeLeaf("config.toml"));
 check(deck::detect("com.openai.codex","Codex",true)==deck::Adapter::Codex);
 check(deck::detect("com.apple.TextEdit","TextEdit",false)==deck::Adapter::Generic);
 check(deck::detect("com.philandro.anydesk","AnyDesk",false)==deck::Adapter::Generic);
 check(deck::detect(nullptr,"Unknown",false)==deck::Adapter::Generic);
 check(deck::detect("com.microsoft.VSCode","Code",true)==deck::Adapter::VSCode);
 check(deck::detect("com.google.Chrome","Chrome",false)==deck::Adapter::Chromium);
 check(deck::detect("com.other.app","Other",true)==deck::Adapter::Electron);
 check(deck::detect("com.anthropic.claudefordesktop","Claude",true)==deck::Adapter::Claude);check(deck::detect("com.anthropic.claudefordesktop","Claude",false)==deck::Adapter::Claude);
 check(deck::adapter("claude")==deck::Adapter::Claude);check(deck::equal(deck::adapterName(deck::Adapter::Claude),"claude"));check(deck::adapter(deck::adapterName(deck::Adapter::Electron))==deck::Adapter::Electron);check(deck::adapter("Claude")==deck::Adapter::Unsupported);
 check(deck::adapter("generic")==deck::Adapter::Generic);check(deck::equal(deck::adapterName(deck::Adapter::Generic),"generic"));
 check(deck::adapter(deck::adapterName(deck::Adapter::Generic))==deck::Adapter::Generic);
 check(deck::adapter("unsupported")==deck::Adapter::Unsupported);check(deck::adapter("anydesk")==deck::Adapter::Unsupported);check(deck::adapter(nullptr)==deck::Adapter::Unsupported);check(deck::adapter("")==deck::Adapter::Unsupported);
 // Unknown native apps retain their normal data and must never receive Chromium switches.
 check(!deck::supportsProfiles(deck::Adapter::Generic));check(!deck::proxyUsesChromiumFlags(deck::Adapter::Generic));
 check(!deck::supportsProfiles(deck::Adapter::Unsupported));check(!deck::proxyUsesChromiumFlags(deck::Adapter::Unsupported));
 for(auto a:{deck::Adapter::Codex,deck::Adapter::VSCode,deck::Adapter::Chromium,deck::Adapter::Electron,deck::Adapter::Claude}){
  check(deck::supportsProfiles(a));check(deck::proxyUsesChromiumFlags(a));check(deck::adapter(deck::adapterName(a))==a);
 }
 char out[8192];using R=deck::ConfigResult;
 check(deck::codexConfig("model = \"test\"\n",out,sizeof out)==R::Ok);
 check(std::strstr(out,"cli_auth_credentials_store = \"file\""));check(std::strstr(out,"model = \"test\""));
 check(deck::codexConfig(" cli_auth_credentials_store = \"auto\"\nforced_chatgpt_workspace_id = \"keep-me\"\n[mcp_servers.local]\ncommand = \"node\"\n",out,sizeof out)==R::Ok);
 check(!std::strstr(out,"\"auto\""));check(std::strstr(out,"keep-me"));check(std::strstr(out,"command = \"node\""));
 check(deck::codexConfig("\"cli_auth_credentials_store\" = \"auto\"",out,sizeof out)==R::Ambiguous);
 check(deck::codexConfig("x = \"\"\"multiline\"\"\"",out,sizeof out)==R::Ambiguous);
 check(deck::codexConfig("x=1",out,8)==R::TooLarge);
 check(deck::codexConfig("# cli_auth_credentials_store = \"auto\"\n",out,sizeof out)==R::Ok);
 check(deck::codexConfig("[other]\ncli_auth_credentials_store = \"nested\"",out,sizeof out)==R::Ok);check(std::strstr(out,"\"nested\""));
 check(deck::codexConfig(nullptr,out,sizeof out)==R::Ok);
 check(deck::authEnv("OPENAI_API_KEY"));check(!deck::authEnv("PATH"));check(!deck::authEnv("SSL_CERT_FILE"));
 check(deck::neverShare("auth.json"));check(deck::neverShare(".credentials.json"));check(!deck::neverShare("AGENTS.md"));
 check(deck::codexConfig("cli_auth_credentials_store.other=1",out,sizeof out)==R::Ambiguous);
 check(deck::codexConfig("# Инструкции\nmodel = \"my-model\"\n",out,sizeof out)==R::Ok);
 std::string once(out);char twice[8192];check(deck::codexConfig(once.c_str(),twice,sizeof twice)==R::Ok);check(once==twice);

 const char* copyHome="/AppDeck/Profiles/copy/codex";
 const std::string bundledSuffix="/.tmp/bundled-marketplaces/openai-bundled";
 const std::string baseSource="/primary/.codex"+bundledSuffix, copySource=copyHome+bundledSuffix;
 const std::string table="[marketplaces.openai-bundled]\n";
 const std::string local="source_type = \"local\"\n";
 auto sourceLine=[](const std::string& path){return "source = \""+path+"\"\n";};
 const std::string vendorConfig=table+local+sourceLine(baseSource);
 const std::string copyConfig=table+local+sourceLine(copySource);
 check(deck::codexConfig("",out,sizeof out)==R::Ok);
 const std::string managedHeader(out);
 auto checkProjection=[&](const std::string& input,const std::string& expected,const char* home){
  check(deck::codexConfig(input.c_str(),out,sizeof out,home)==R::Ok);
  check(out==managedHeader+expected);
 };
 checkProjection(vendorConfig,copyConfig,copyHome);
 checkProjection(vendorConfig,vendorConfig,nullptr); // Source validation has no destination.
 checkProjection(managedHeader+copyConfig,copyConfig,copyHome);

 // Preserve comments, organization restrictions, plugin choices and nearby marketplace entries byte for byte.
 const std::string preamble="forced_chatgpt_workspace_id = 'keep'\n# [marketplaces.openai-bundled]\n"
  "[marketplaces.openai-bundled-custom]\n"+local+sourceLine(baseSource);
 const std::string trailer="[plugins.\"tool@openai-bundled\"]\nenabled = false\n"
  "[marketplaces.openai-bundled.metadata]\n"+local+sourceLine(baseSource);
 const std::string section="  [ 'marketplaces' . \"openai-bundled\" ] # Desktop\r\n"
  "items = [\n  ['#', ']'], # ]\n  ['source_type', 'git'],\n]\n"
  "\t'source' = ";
 const std::string typeLast=" # keep this comment\r\n\"source_type\" = 'local'\r\n";
 checkProjection(preamble+section+"'"+baseSource+"'"+typeLast+trailer,
                 preamble+section+"\""+copySource+"\""+typeLast+trailer,copyHome);

 // Only the exact generated local registration belongs to this projection.
 for(const auto& other:{
  table+"source_type = \"git\"\n"+sourceLine(baseSource),
  table+"# source_type = 'local'\n"+sourceLine(baseSource),
  table+"source_type_old = 'local'\n"+sourceLine(baseSource),
  table+local+"# "+sourceLine(baseSource),
  table+local+"source_backup = '"+baseSource+"'\n",
  table+local+sourceLine("/custom/plugins/openai-bundled"),
  table+local+sourceLine("relative"+bundledSuffix),
  table+local+sourceLine(baseSource+"-extra"),
  std::string("[other.openai-bundled]\n")+local+sourceLine(baseSource),
  std::string("[marketplaces.openai-bundled-extra]\n")+local+sourceLine(baseSource),
  std::string("[marketplaces.openai-bundled.settings]\n")+local+sourceLine(baseSource)
 })checkProjection(other,other,copyHome);

 const char* escapedHome="/two path/quo\"te/back\\slash/line\nend\t";
 const std::string encodedHome=R"(/two path/quo\"te/back\\slash/line\u000aend\u0009)";
 checkProjection(vendorConfig,table+local+sourceLine(encodedHome+bundledSuffix),escapedHome);
 std::string encodedConfig(out);
 checkProjection(encodedConfig,table+local+sourceLine(encodedHome+bundledSuffix),escapedHome);
 // Crossing the output boundary during expansion cannot touch the byte past the caller's buffer.
 std::string canary(encodedConfig.size()+2,'!');
 check(deck::codexConfig(vendorConfig.c_str(),canary.data(),encodedConfig.size(),escapedHome)==R::TooLarge);
 check(canary[encodedConfig.size()]=='!');
 check(deck::codexConfig(vendorConfig.c_str(),canary.data(),encodedConfig.size()+1,escapedHome)==R::Ok);
 check(encodedConfig==canary.c_str()&&canary.back()=='!');
 // Truncation, newline and arbitrary UTF-8 input never overflow the output.
 for(int size=0;size<200;++size){std::string input(size,'a');char b[256];memset(b,0x55,sizeof b);auto r=deck::codexConfig(input.c_str(),b,128);check((unsigned char)b[128]==0x55);if(r==R::Ok)check(strlen(b)<128);}
 std::cout<<n<<" policy assertions passed\n";
}
