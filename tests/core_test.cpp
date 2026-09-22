#include "../src/core.hpp"
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
 check(deck::detect("com.apple.TextEdit","TextEdit",false)==deck::Adapter::Unsupported);
 check(deck::detect("com.microsoft.VSCode","Code",true)==deck::Adapter::VSCode);
 check(deck::detect("com.google.Chrome","Chrome",false)==deck::Adapter::Chromium);
 check(deck::detect("com.other.app","Other",true)==deck::Adapter::Electron);
 check(deck::detect("com.anthropic.claudefordesktop","Claude",true)==deck::Adapter::Claude);check(deck::detect("com.anthropic.claudefordesktop","Claude",false)==deck::Adapter::Claude);
 check(deck::adapter("claude")==deck::Adapter::Claude);check(deck::equal(deck::adapterName(deck::Adapter::Claude),"claude"));check(deck::adapter(deck::adapterName(deck::Adapter::Electron))==deck::Adapter::Electron);check(deck::adapter("Claude")==deck::Adapter::Unsupported);
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
 // Truncation, newline and arbitrary UTF-8 input never overflow the output.
 for(int size=0;size<200;++size){std::string input(size,'a');char b[256];memset(b,0x55,sizeof b);auto r=deck::codexConfig(input.c_str(),b,128);check((unsigned char)b[128]==0x55);if(r==R::Ok)check(strlen(b)<128);}
 std::cout<<n<<" policy assertions passed\n";
}
