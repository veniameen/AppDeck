#include "../src/usage_error_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(){
 unsigned checks=0;auto check=[&](const char* text,int status,const char* expected){++checks;const char* actual=usage_error::classify(text,status);if(std::strcmp(actual,expected)){std::fprintf(stderr,"FAIL error classification %u: expected %s, got %s\n",checks,expected,actual);std::exit(1);}};
 check(nullptr,0,"error");check("",0,"error");check("Unexpected response",0,"error");check("login documentation",0,"error");
 check("Failed to authenticate. HTTP 403 Attention Required! Cloudflare",0,"network");
 check("Request blocked before it reached Anthropic's API (HTTP 403): usually an unsupported region, VPN or proxy",0,"network");
 check("The network this request came from isn't allowed",403,"network");
 check("HTTP 403 Forbidden",0,"denied");check("Permission denied",403,"denied");check("request not allowed",0,"denied");check("Permission_error",0,"denied");
 check("API Error: 403 request refused",0,"denied");check("API Error: 401 login required",0,"auth");check("API Error: 429",0,"ratelimit");
 check("Your account has been suspended",403,"suspended");check("Your account with Claude has been suspended",0,"suspended");
 check("This organization has been disabled",403,"suspended");check("ACCOUNT_DISABLED",0,"suspended");
 check("No evidence of an account ban",403,"denied");check("account may be suspended",0,"error");check("account is not suspended",0,"error");
 check("Unsupported region",403,"region");check("not available in your country",0,"region");check("Supported Countries Policy",0,"region");
 check("407 Proxy Authentication Required",0,"proxy");check("Proxy authentication failed",0,"proxy");check("ERR_TUNNEL_CONNECTION_FAILED",0,"proxy");check("",407,"proxy");
 check("OAuth token has expired",401,"expired");check("Session expired",0,"expired");check("invalid_grant",0,"expired");
 check("Authentication required",0,"auth");check("NOT LOGGED IN",0,"auth");check("Please login",0,"auth");check("",401,"auth");
 check("HTTP 401 Unauthorized",0,"auth");check("AUTHENTICATION_FAILED",0,"auth");
 check("HTTP 429 Too Many Requests",0,"ratelimit");check("RATE_LIMIT_ERROR",0,"ratelimit");check("",429,"ratelimit");
 check("Weekly limit reached",429,"quota");check("You've reached your weekly limit",0,"quota");check("5-hour limit reached",0,"quota");check("insufficient_quota",429,"quota");
 check("ECONNRESET",0,"network");check("getaddrinfo ENOTFOUND api.example.com",0,"network");check("Failed to fetch",0,"network");check("certificate verification failed",0,"network");
 check("ETIMEDOUT",0,"timeout");check("Connection timed out",0,"timeout");
 check("",502,"service");check("overloaded_error",0,"service");check("Service unavailable",0,"service");
 check("method not found",0,"unsupported");check("Unknown control request: get_usage",0,"unsupported");
 check("limit response contains a login note and a 403 in unrelated data",0,"error");
 // Only the bounded prefix is inspected; no long untrusted message can scan indefinitely.
 char longText[5000];std::memset(longText,'x',sizeof(longText));std::strcpy(longText+4500,"Your account has been suspended");longText[4999]=0;check(longText,0,"error");
 std::memcpy(longText,"HTTP 403 Forbidden",18);check(longText,0,"denied");
 std::printf("%u usage-error policy assertions passed\n",checks);
}
