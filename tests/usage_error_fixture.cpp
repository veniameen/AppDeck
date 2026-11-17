#include "../src/mac.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace mac;
#include "../src/usage_probe.hpp"
// This executable doubles as a fake vendor CLI. It reads no profile or credential files,
// makes no network requests, and returns only synthetic auth/control fixtures.
int main(int argc,char** argv){
 const char* scenario=getenv("APPDECK_USAGE_ERROR_CASE");
 if(scenario&&argc>2&&!strcmp(argv[1],"auth")&&!strcmp(argv[2],"status")){
  if(!strcmp(scenario,"bad-auth"))puts("not JSON");
  else if(!strcmp(scenario,"malformed-auth"))puts(R"({"loggedIn":"yes"})");
  else if(!strcmp(scenario,"auth-network"))puts(R"({"error":{"status":403,"message":"Request blocked before it reached Anthropic's API"}})");
  else if(!strcmp(scenario,"signed-out"))puts(R"({"loggedIn":false,"authMethod":"none"})");
  else puts(R"({"loggedIn":true,"authMethod":"claude.ai","email":"fixture@example.invalid","orgId":"fixture-org","subscriptionType":"max"})");
  return 0;
 }
 if(scenario&&argc>1&&!strcmp(argv[1],"-p")){
  char request[2048];if(!fgets(request,sizeof(request),stdin)||!strstr(request,"get_usage")||strstr(request,"\"user\""))return 41;
  const char* problem=nullptr;
  if(!strcmp(scenario,"network"))problem=R"("Failed to authenticate. HTTP 403 Attention Required! Cloudflare")";
  else if(!strcmp(scenario,"denied"))problem=R"({"status":403,"type":"permission_error","message":"Request refused"})";
  else if(!strcmp(scenario,"suspended"))problem=R"({"status":403,"message":"Your account has been suspended"})";
  else if(!strcmp(scenario,"expired"))problem=R"("OAuth token has expired")";
  else if(!strcmp(scenario,"ratelimit"))problem=R"({"status":429,"message":"Too many requests"})";
  else if(!strcmp(scenario,"quota"))problem=R"({"status":429,"message":"Weekly limit reached"})";
  else if(!strcmp(scenario,"proxy"))problem=R"("407 Proxy Authentication Required")";
  else if(!strcmp(scenario,"region"))problem=R"({"message":"Unsupported region"})";
  else if(!strcmp(scenario,"service"))problem=R"({"status":503,"message":"Try later"})";
  else if(!strcmp(scenario,"unsupported"))problem=R"("Unknown control request: get_usage")";
  else if(!strcmp(scenario,"redacted"))problem=R"("HTTP 403 Forbidden for fixture@example.invalid https://username:SECRET@proxy.invalid token=SECRET")";
  else if(!strcmp(scenario,"wrong-id")){puts(R"({"type":"control_response","response":{"subtype":"success","request_id":"not-appdeck","response":{"rate_limits_available":true,"rate_limits":{"five_hour":{"utilization":1}}}}})");return 0;}
  else if(!strcmp(scenario,"bad-control")){puts(R"({"type":"control_response","response":{"subtype":"success","request_id":"appdeck-usage","response":"wrong schema"}})");return 0;}
  else if(!strcmp(scenario,"fatal")){puts(R"({"type":"result","is_error":true,"subtype":"error_during_execution","errors":["ECONNRESET"]})");return 0;}
  else if(!strcmp(scenario,"wrapped-network")){puts(R"({"type":"result","is_error":true,"error":"authentication_failed","api_error_status":403,"errors":["HTTP 403 Attention Required! Cloudflare"]})");return 0;}
  else if(!strcmp(scenario,"wrapped-expired")){puts(R"({"type":"result","is_error":true,"error":"authentication_failed","api_error_status":401,"errors":["OAuth token has expired"]})");return 0;}
  else if(!strcmp(scenario,"wrapped-suspended")){puts(R"({"type":"result","is_error":true,"error":"authentication_failed","api_error_status":403,"errors":["Your account has been suspended"]})");return 0;}
  else if(!strcmp(scenario,"wrapped-denied")){puts(R"({"type":"result","is_error":true,"error":"authentication_failed","api_error_status":403,"errors":["Request refused"]})");return 0;}
  else if(!strcmp(scenario,"wrapped-result-network")){puts(R"({"type":"result","is_error":true,"error":"authentication_failed","api_error_status":403,"result":"Failed to authenticate. API Error:403 Attention Required|Cloudflare"})");return 0;}
  else if(!strcmp(scenario,"wrapped-result-expired")){puts(R"({"type":"result","is_error":true,"error":"authentication_failed","api_error_status":401,"result":"OAuth token has expired"})");return 0;}
  if(problem)printf("{\"type\":\"control_response\",\"response\":{\"subtype\":\"error\",\"request_id\":\"appdeck-usage\",\"error\":%s}}\n",problem);
  else puts(R"({"type":"control_response","response":{"subtype":"success","request_id":"appdeck-usage","response":{"rate_limits_available":true,"rate_limits":{"five_hour":{"utilization":25}}}}})");
  return 0;
 }
 if(argc!=2){std::fprintf(stderr,"Usage: usage_error_fixture /absolute/path/to/self\n");return 2;}
 if(!dlopen("/System/Library/Frameworks/AppKit.framework/AppKit",2|8))return 3;Pool pool;
 unsigned checks=0;auto check=[&](bool ok){++checks;if(!ok){std::fprintf(stderr,"FAIL usage-error fixture assertion %u\n",checks);std::exit(1);}};
 auto parse=[](const char* text){return usageJSON(text,strlen(text));};
 check(!strcmp(usageErrorKind(parse(R"({"status":403,"type":"authentication_error"})")),"denied"));
 check(!strcmp(usageErrorKind(parse(R"({"code":-32601,"message":"Unknown method"})")),"unsupported"));
 check(!strcmp(usageErrorKind(parse(R"({"code":null,"message":42,"status":"403","error":["bad"]})")),"error"));
 check(!strcmp(usageErrorKind(parse(R"({"error":{"error":{"error":{"error":{"message":"Your account has been suspended"}}}}})")),"error"));
 check(!strcmp(usageErrorKind(parse(R"({"error":{"status":429,"message":"Too many requests"}})")),"ratelimit"));
 check(!strcmp(usageErrorKind(parse(R"({"error":"authentication_failed","errors":["HTTP 403 Attention Required! Cloudflare"]})")),"network"));
 check(!strcmp(usageErrorKind(parse(R"({"message":"authentication required","errors":["OAuth token has expired"]})")),"expired"));
 check(!strcmp(usageErrorKind(parse(R"({"type":"permission_error","errors":[{"message":"Your account has been suspended"}]})")),"suspended"));
 check(!strcmp(usageErrorKind(parse(R"({"error":"authentication_failed","errors":[{"status_code":403}]})")),"denied"));
 check(!strcmp(usageErrorKind(parse(R"({"error":"authentication_failed","errors":[{"http_status":429}]})")),"ratelimit"));
 check(!strcmp(usageErrorKind(parse(R"({"error":"authentication_failed","errors":[null,42,{},"unknown"]})")),"auth"));
 Obj tooMany=array();for(int i=0;i<16;++i)add(tooMany,str("unknown"));add(tooMany,str("Your account has been suspended"));check(!strcmp(usageErrorKind(tooMany),"error"));
 struct Case{const char* name;const char* error;};
 for(auto test:{Case{"success",nullptr},Case{"signed-out",nullptr},Case{"bad-auth","protocol"},Case{"malformed-auth","protocol"},Case{"auth-network","network"},Case{"network","network"},Case{"denied","denied"},Case{"suspended","suspended"},Case{"expired","expired"},Case{"ratelimit","ratelimit"},Case{"quota","quota"},Case{"proxy","proxy"},Case{"region","region"},Case{"service","service"},Case{"unsupported","unsupported"},Case{"redacted","denied"},Case{"wrong-id","protocol"},Case{"bad-control","protocol"},Case{"fatal","network"},Case{"wrapped-network","network"},Case{"wrapped-expired","expired"},Case{"wrapped-suspended","suspended"},Case{"wrapped-denied","denied"},Case{"wrapped-result-network","network"},Case{"wrapped-result-expired","expired"}}){
  Obj request=dict(),env=dict(),result=dict();put(env,"APPDECK_USAGE_ERROR_CASE",str(test.name));put(request,"binary",str(argv[1]));put(request,"scratch",str("/private/tmp"));put(request,"environment",env);
  usageProbeClaude(request,result,true);Obj error=get(result,"error");check(test.error?same(error,str(test.error)):!error);check(!get(result,"detail"));
  if(!strcmp(test.name,"success")){check(count(get(get(result,"limits"),"windows"))==1);check(same(get(get(result,"account"),"orgId"),str("fixture-org")));}
  if(!strcmp(test.name,"signed-out"))check(!truth(get(get(result,"account"),"signedIn")));
  if(!strcmp(test.name,"auth-network")||!strcmp(test.name,"bad-auth")||!strcmp(test.name,"malformed-auth"))check(!get(result,"account"));
 }
 std::printf("%u native usage-error fixture assertions passed\n",checks);
}
