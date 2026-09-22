// Included inside a namespace that already uses mac::. Only documented Accessibility API.
// Kept free of AppDeck state so tests/macos_api_smoke.cpp can exercise the same calls.
struct WindowAPI {
 void* library=nullptr;
 bool (*trusted)(Obj)=nullptr;
 bool (*trustedPlain)()=nullptr;
 Obj (*create)(int)=nullptr;
 int (*copy)(Obj,Obj,Obj*)=nullptr;
 int (*set)(Obj,Obj,Obj)=nullptr;
 int (*perform)(Obj,Obj)=nullptr;
 Obj (*value)(int,const void*)=nullptr;
 bool (*unbox)(Obj,int,void*)=nullptr;
 int (*timeout)(Obj,float)=nullptr;
 void (*release)(Obj)=nullptr;
 bool load(){
  if(library)return create&&copy&&set&&perform&&value&&unbox&&release&&trusted;
  library=dlopen("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices",2);
  if(!library)return false;
  trusted=(decltype(trusted))dlsym(library,"AXIsProcessTrustedWithOptions");
  trustedPlain=(decltype(trustedPlain))dlsym(library,"AXIsProcessTrusted");
  create=(decltype(create))dlsym(library,"AXUIElementCreateApplication");
  copy=(decltype(copy))dlsym(library,"AXUIElementCopyAttributeValue");
  set=(decltype(set))dlsym(library,"AXUIElementSetAttributeValue");
  perform=(decltype(perform))dlsym(library,"AXUIElementPerformAction");
  value=(decltype(value))dlsym(library,"AXValueCreate");
  unbox=(decltype(unbox))dlsym(library,"AXValueGetValue");
  timeout=(decltype(timeout))dlsym(library,"AXUIElementSetMessagingTimeout");
  release=(decltype(release))dlsym(library,"CFRelease");return load();
 }
 bool permission(bool ask){
  if(!load())return false;
  // Never pass an options dictionary that lacks the prompt key. While the process is
  // not yet trusted, HIServices reads that value unchecked: an empty dictionary ends in
  // CFGetTypeID(NULL) and SIGSEGV at 0x8 (0.2.0-preview crashed here on every focus).
  Obj key=ask?publicConstant(library,"kAXTrustedCheckOptionPrompt"):nullptr;
  if(!key)return trustedPlain?trustedPlain():trusted(nullptr);
  Obj opts=dict();send<void>(opts,"setObject:forKey:",boolean(true),key);
  return trusted(opts);
 }
 Obj attribute(Obj o,const char* key){Obj x=nullptr;if(o)copy(o,str(key),&x);return x;}
 bool flag(Obj o,const char* key){Obj x=attribute(o,key);bool b=truth(x);if(x)release(x);return b;}
 Obj mainWindow(int pid){
  Obj a=create(pid);if(!a)return nullptr;if(timeout)timeout(a,.35f);
  Obj w=attribute(a,"AXMainWindow");
  if(!w){Obj ws=attribute(a,"AXWindows");if(ws){for(UInt i=0;i<count(ws);++i){Obj candidate=at(ws,i),sub=attribute(candidate,"AXSubrole");
    bool standard=same(sub,str("AXStandardWindow"));if(sub)release(sub);
    if(standard){w=candidate;auto retain=(Obj(*)(Obj))dlsym(library,"CFRetain");if(retain)retain(w);else w=nullptr;break;}
   }release(ws);}}
  release(a);if(w&&timeout)timeout(w,.35f);return w;
 }
 bool geometry(Obj w,Point& p,Extent& s){
  Obj pv=attribute(w,"AXPosition"),sv=attribute(w,"AXSize");bool ok=pv&&sv&&unbox(pv,1,&p)&&unbox(sv,2,&s);
  if(pv)release(pv);if(sv)release(sv);return ok;
 }
 bool move(Obj w,Point p,Extent s){
  if(flag(w,"AXFullScreen"))return false;set(w,str("AXMinimized"),boolean(false));
  Obj pv=value(1,&p),sv=value(2,&s);if(!pv||!sv){if(pv)release(pv);if(sv)release(sv);return false;}
  set(w,str("AXPosition"),pv);int e1=set(w,str("AXSize"),sv),e2=set(w,str("AXPosition"),pv);
  release(pv);release(sv);perform(w,str("AXRaise"));
  Point actual{};Extent extent{};
  if(e1||e2||!geometry(w,actual,extent))return false;
  auto close=[](double a,double b){return a-b<3&&b-a<3;};
  return close(actual.x,p.x)&&close(actual.y,p.y)&&close(extent.width,s.width)&&close(extent.height,s.height);
 }
};
