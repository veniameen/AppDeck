#pragma once
// Small, typed C++ bridge to the PUBLIC Objective-C runtime and AppKit APIs.
// No Apple SDK files, private APIs, webview, or vendored runtime are included.
// On macOS the program dynamically uses the frameworks supplied by the OS.
// Keeping the public ABI declarations here also permits cross-compilation.
namespace std {
template<class E> class initializer_list {
    const E* p_; unsigned long n_;
public:
    constexpr initializer_list() noexcept : p_(nullptr), n_(0) {}
    constexpr unsigned long size() const noexcept { return n_; }
    constexpr const E* begin() const noexcept { return p_; }
    constexpr const E* end() const noexcept { return p_+n_; }
};
}
using Obj = void*;
using Sel = void*;
using Class = void*;
using UInt = unsigned long;
using Int = long;
using Size = unsigned long;
struct Point { double x, y; };
struct Extent { double width, height; };
struct Rect { Point origin; Extent size; };
inline Rect rect(double x, double y, double w, double h) { return {{x,y},{w,h}}; }
extern "C" {
void* dlopen(const char*, int);
void* dlsym(void*, const char*);
char* getenv(const char*);
Size strlen(const char*);
int strcmp(const char*,const char*);
int strncmp(const char*,const char*,Size);
char* strchr(const char*,int);
char* strstr(const char*,const char*);
void* memcpy(void*,const void*,Size);
void* memset(void*,int,Size);
int snprintf(char*,Size,const char*,...);
void* calloc(Size,Size);
void free(void*);
int chmod(const char*,unsigned short);
unsigned short umask(unsigned short);
int open(const char*,int,...);
int close(int);
int flock(int,int);
int fcntl(int,int,...);
long read(int,void*,Size);
long write(int,const void*,Size);
int poll(void*,unsigned,int);
int sysctl(int*,unsigned,void*,Size*,void*,Size);
void exit(int);
Class objc_getClass(const char*);
Sel sel_registerName(const char*);
void objc_msgSend();
#if defined(__x86_64__)
void objc_msgSend_stret();
#endif
Class objc_allocateClassPair(Class,const char*,Size);
void objc_registerClassPair(Class);
bool class_addMethod(Class,Sel,void(*)(),const char*);
}
namespace mac {
inline Obj cls(const char* n) { return objc_getClass(n); }
inline Sel sel(const char* n) { return sel_registerName(n); }
template<class R=Obj,class... A> inline R send(Obj o,const char* s,A... a) {
    return reinterpret_cast<R(*)(Obj,Sel,A...)>(objc_msgSend)(o,sel(s),a...);
}
inline Rect getRect(Obj o,const char* s) {
#if defined(__x86_64__)
    Rect r{};
    reinterpret_cast<void(*)(Rect*,Obj,Sel)>(objc_msgSend_stret)(&r,o,sel(s));
    return r;
#else
    return send<Rect>(o,s);
#endif
}
inline Obj str(const char* s) { return send(cls("NSString"),"stringWithUTF8String:",s?s:""); }
inline const char* utf8(Obj s) { return s?send<const char*>(s,"UTF8String"):""; }
inline Obj make(const char* n) { return send(send(cls(n),"alloc"),"init"); }
inline Obj keep(Obj o) { return send(o,"retain"); }
inline void drop(Obj o) { if(o) send<void>(o,"release"); }
inline Obj autoRelease(Obj o) { return send(o,"autorelease"); }
inline Obj array() { return send(cls("NSMutableArray"),"array"); }
inline UInt count(Obj o) { return send<UInt>(o,"count"); }
inline Obj at(Obj a,UInt i) { return send(a,"objectAtIndex:",i); }
inline void add(Obj a,Obj o) { if(o) send<void>(a,"addObject:",o); }
inline Obj dict() { return send(cls("NSMutableDictionary"),"dictionary"); }
inline Obj get(Obj d,const char* k) { return send(d,"objectForKey:",str(k)); }
inline void put(Obj d,const char* k,Obj v) { if(v) send<void>(d,"setObject:forKey:",v,str(k)); }
inline void erase(Obj d,const char* k) { send<void>(d,"removeObjectForKey:",str(k)); }
inline Obj num(Int i) { return send(cls("NSNumber"),"numberWithLong:",i); }
inline Obj boolean(bool b) { return send(cls("NSNumber"),"numberWithBool:",b); }
inline Obj real(double d) { return send(cls("NSNumber"),"numberWithDouble:",d); }
inline Int integer(Obj o) { return send<Int>(o,"longValue"); }
inline bool truth(Obj o) { return send<bool>(o,"boolValue"); }
inline bool same(Obj a,Obj b) { return a==b||(a&&b&&send<bool>(a,"isEqual:",b)); }
inline Obj join(Obj a,Obj b) { return send(a,"stringByAppendingPathComponent:",b); }
inline Obj join(Obj a,const char* b) { return join(a,str(b)); }
inline Obj cat(Obj a,Obj b) { return send(a,"stringByAppendingString:",b); }
inline Obj replace(Obj s,const char* a,const char* b) { return send(s,"stringByReplacingOccurrencesOfString:withString:",str(a),str(b)); }
inline Obj formatInt(const char* f,Int i) { char b[128]; snprintf(b,sizeof b,f,i); return str(b); }
inline void method(Class c,const char* name,void(*fn)(),const char* types) { class_addMethod(c,sel(name),fn,types); }
template<class F> inline void method(Class c,const char* name,F f,const char* types) { method(c,name,reinterpret_cast<void(*)()>(f),types); }
inline Obj publicConstant(void* framework,const char* n) { auto p=reinterpret_cast<Obj*>(dlsym(framework,n)); return p?*p:nullptr; }
// NSTextAlignment differs by ABI: AppKit uses the iOS values everywhere except x86_64 macOS.
#if defined(__x86_64__)
constexpr Int alignRight=1,alignCenter=2;
#else
constexpr Int alignRight=2,alignCenter=1;
#endif
struct Pool { Obj p; Pool():p(make("NSAutoreleasePool")){} ~Pool(){send<void>(p,"drain");} };
}
