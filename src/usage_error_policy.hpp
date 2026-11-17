#pragma once
// Error text is inspected only in memory. Only a fixed category leaves this parser.
// A 403 may be a network or regional refusal; it is never sufficient evidence of an account ban.
namespace usage_error {
inline char lower(char c){return c>='A'&&c<='Z'?(char)(c-'A'+'a'):c;}
inline bool contains(const char* text,const char* needle){
 if(!text||!needle||!*needle)return false;
 for(unsigned i=0;i<4096&&text[i];++i){unsigned j=0;while(i+j<4096&&needle[j]&&text[i+j]&&lower(text[i+j])==lower(needle[j]))++j;if(!needle[j])return true;}
 return false;
}
inline bool http(const char* text,int code){
 switch(code){
  case 401:return contains(text,"http 401")||contains(text,"http error 401")||contains(text,"status 401")||contains(text,"status code 401")||contains(text,"401 unauthorized")||contains(text,"api error: 401");
  case 403:return contains(text,"http 403")||contains(text,"http error 403")||contains(text,"status 403")||contains(text,"status code 403")||contains(text,"403 forbidden")||contains(text,"403 attention required")||contains(text,"api error: 403");
  case 407:return contains(text,"http 407")||contains(text,"status 407")||contains(text,"407 proxy");
  case 429:return contains(text,"http 429")||contains(text,"status 429")||contains(text,"status code 429")||contains(text,"429 too many")||contains(text,"api error: 429");
  default:return false;
 }
}
inline const char* classify(const char* text,int status=0){
 // Network-edge messages can mention authentication, regions and accounts as possible causes.
 if(contains(text,"blocked before it reached")||contains(text,"network edge refused")||contains(text,"network this request came from")||contains(text,"cloudflare"))return "network";
 if(contains(text,"your account has been suspended")||contains(text,"your account has been disabled")||contains(text,"your account has been banned")||contains(text,"your account with claude has been suspended")||contains(text,"your account with claude has been disabled")||contains(text,"organization has been disabled")||contains(text,"organization is disabled")||contains(text,"account_disabled")||contains(text,"account_suspended")||contains(text,"organization_disabled"))return "suspended";
 if(status==407||http(text,407)||contains(text,"proxy authentication")||contains(text,"err_tunnel_connection_failed")||contains(text,"err_proxy_connection_failed")||contains(text,"proxy connection failed"))return "proxy";
 if(contains(text,"unsupported region")||contains(text,"unsupported country")||contains(text,"not available in your country")||contains(text,"not supported in your region")||contains(text,"supported countries policy")||contains(text,"supported regions policy"))return "region";
 if(contains(text,"weekly limit reached")||contains(text,"session limit reached")||contains(text,"usage limit reached")||contains(text,"usage_limit_reached")||contains(text,"quota exceeded")||contains(text,"insufficient_quota")||contains(text,"you've reached your weekly limit")||contains(text,"5-hour limit reached"))return "quota";
 if(status==429||http(text,429)||contains(text,"rate_limit_error")||contains(text,"rate limit exceeded")||contains(text,"too many requests"))return "ratelimit";
 if(contains(text,"token expired")||contains(text,"token has expired")||contains(text,"session expired")||contains(text,"session has expired")||contains(text,"invalid_grant")||contains(text,"refresh token is invalid")||contains(text,"refresh token has expired"))return "expired";
 if(contains(text,"econnreset")||contains(text,"econnrefused")||contains(text,"enotfound")||contains(text,"enetunreach")||contains(text,"ehostunreach")||contains(text,"eai_again")||contains(text,"network error")||contains(text,"network is unreachable")||contains(text,"connection error")||contains(text,"failed to fetch")||contains(text,"certificate verify failed")||contains(text,"certificate verification failed"))return "network";
 if(contains(text,"etimedout")||contains(text,"timed out")||contains(text,"timeout"))return "timeout";
 if(status==403||http(text,403)||contains(text,"permission_error")||contains(text,"access denied")||contains(text,"request not allowed"))return "denied";
 if(status==401||http(text,401)||contains(text,"authentication required")||contains(text,"authentication_error")||contains(text,"authentication_failed")||contains(text,"not logged in")||contains(text,"not signed in")||contains(text,"please log in")||contains(text,"please login")||contains(text,"invalid api key"))return "auth";
 if(contains(text,"method not found")||contains(text,"unknown control request")||contains(text,"unknown request subtype")||contains(text,"unsupported control request"))return "unsupported";
 if(status>=500&&status<=599)return "service";
 if(contains(text,"overloaded_error")||contains(text,"service unavailable")||contains(text,"internal_server_error")||contains(text,"unexpected capacity constraints"))return "service";
 return "error";
}
} // namespace usage_error
