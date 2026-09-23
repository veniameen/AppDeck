#pragma once
// Pure C++ geometry/state policies; no AppKit or file access.
namespace deck {
struct Box { double x,y,w,h; };
inline double clamp(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
inline double smooth(double t){t=clamp(t,0,1);return t*t*(3-2*t);}
// Quintic smootherstep: no jumps in position, velocity or acceleration at ends.
inline double bloom(double t){t=clamp(t,0,1);return t*t*t*(t*(t*6-15)+10);}
// One-column dock: header, the list of profile rows, action strip. It lists the profiles of every
// registered app in the user's order. At most six rows are visible — fewer when they do not fit
// on the screen — and the rest of the list scrolls. One row per profile and nothing else: profiles
// are created in the main window, not here. The first eight places answer ⌃⌥1…8.
constexpr double edgeWidth=264,edgeRowHeight=58,edgeRowGap=8,edgeStep=edgeRowHeight+edgeRowGap;
constexpr unsigned edgeMinRows=1,edgeMaxRows=6,edgeHotkeys=8;
inline double edgeListHeight(unsigned rows){if(rows<1)rows=1;return rows*edgeRowHeight+(rows-1)*edgeRowGap;}
inline double edgeHeight(unsigned rows){return 66+edgeListHeight(rows)+10+92;}
inline unsigned edgeRows(unsigned profiles,double screenHeight){
 unsigned want=profiles>edgeMaxRows?edgeMaxRows:profiles;if(want<edgeMinRows)want=edgeMinRows;
 // Shrink until the dock leaves 24 pt of screen free; the rows that do not fit stay reachable by scrolling.
 while(want>edgeMinRows&&edgeHeight(want)>screenHeight-24)--want;
 return want;
}
// How far the list scrolls when `rows` of `profiles` rows are visible.
inline double edgeScrollRange(unsigned profiles,unsigned rows){double d=edgeListHeight(profiles)-edgeListHeight(rows);return d>0?d:0;}
// Reordering by drag. Row i sits at i*edgeStep; a dragged row whose top edge is at `top` belongs to
// the nearest place, and the other rows close the gap it left and open one where it will land.
inline unsigned edgeDropSlot(double top,unsigned rows){
 if(rows<2)return 0;double s=top/edgeStep;if(!(s>0))return 0; // also NaN
 if(s>=rows-1)return rows-1;return (unsigned)(s+.5);
}
inline unsigned edgeShiftedSlot(unsigned i,unsigned from,unsigned to){
 if(i==from)return to;if(from<to&&i>from&&i<=to)return i-1;if(to<from&&i>=to&&i<from)return i+1;return i;
}
// Soft edges: at rest the list is clean — no shade over the first or the last visible row.
// An edge of the visible list shades only while it cuts through a row (y = the edge, in list
// coordinates): 0 when it falls on a row boundary or into a gap — as at the top and at the end of the
// list — up to 1 when a row is well under it, ramping over `ramp` points so nothing pops.
inline double edgeCut(double y,double ramp=10){
 if(!(y>0)||y>1e12||!(ramp>0))return 0;double r=y-edgeStep*(double)(unsigned long long)(y/edgeStep);
 if(r>=edgeRowHeight)return 0;double m=r<edgeRowHeight-r?r:edgeRowHeight-r;return clamp(m/ramp,0,1);
}
// While a row is dragged near the top or bottom of the visible list, the list scrolls by itself:
// points per frame, faster the closer the pointer (`y`, from the top of the visible list) is to
// the edge or the farther beyond it, 0 in between. Negative scrolls up.
inline double edgeAutoScroll(double y,double height,double band=30,double fastest=10){
 if(!(height>0))return 0;if(band>height/3)band=height/3;
 if(y<band)return -fastest*clamp((band-y)/band,0,1);
 if(y>height-band)return fastest*clamp((y-(height-band))/band,0,1);
 return 0;
}
// Floating edgeMargin off the right border of the visible frame, vertically centred, never outside it
// (on a screen too narrow for the margin the panel moves back to the border).
constexpr double edgeMargin=12;
inline Box edgeBounds(Box s,double w=edgeWidth,double h=edgeHeight(4)){
 w=clamp(w,1,s.w>1?s.w:1);h=clamp(h,1,s.h>24?s.h-24:(s.h>1?s.h:1));double m=clamp(edgeMargin,0,s.w-w);
 return {s.x+s.w-w-m,s.y+(s.h-h)/2,w,h};
}
inline Box edgeFrame(Box full,double progress,bool reduceMotion){
 if(reduceMotion)return full;
 // The panel unfolds from its right side: only its width grows, the right side stays put,
 // so the window never reaches onto a neighbouring display.
 double w=clamp(full.w*bloom(progress),1,full.w);
 return {full.x+full.w-w,full.y,w,full.h};
}
inline Box tileBox(Box screen,unsigned n,unsigned i,double gap=12){
 if(!n||n>4||i>=n||screen.w<=0||screen.h<=0)return {0,0,0,0};
 unsigned cols=n==1?1:2,rows=n>2?2:1;
 gap=clamp(gap,0,(screen.w<screen.h?screen.w:screen.h)/8);
 double w=(screen.w-gap*(cols+1))/cols,h=(screen.h-gap*(rows+1))/rows;
 // Coordinates use a top-left origin, like Accessibility.
 return {screen.x+gap+(i%cols)*(w+gap),screen.y+gap+(i/cols)*(h+gap),w,h};
}
inline bool absoluteLocalPath(const char* p){
 if(!p||p[0]!='/')return false;
 for(const char* q=p;*q;++q)if((unsigned char)*q<32||*q==127)return false;
 return true;
}
inline bool workspaceKey(const char* k){
 return equal(k,"electron-saved-workspace-roots")||equal(k,"electron-workspace-root-labels")||equal(k,"project-order")||equal(k,"local-projects");
}
// Ids of local Desktop projects: `local-<hash>` or a UUID. Namespaced ids such as `cloud:<account>…`
// (anything with ':' or '/') belong to one signed-in account or host and are never projected.
inline bool projectId(const char* s){
 Size n=length(s);if(!n||n>128)return false;
 for(Size i=0;i<n;++i){char c=s[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'||c=='_'||c=='.'))return false;}
 return true;
}
inline bool settingsLeaf(const char* k){return equal(k,"config.toml")||equal(k,"AGENTS.md")||equal(k,"skills")||equal(k,"rules");}
// Opt-in shared history: the only base folders a copy may link to. Databases are reached
// through Codex's own CODEX_SQLITE_HOME switch and are never linked or copied.
inline bool historyLeaf(const char* k){return equal(k,"sessions")||equal(k,"archived_sessions")||equal(k,"thread-writer-locks");}
inline bool stateDatabase(const char* n){return prefix(n,"state_")&&suffix(n,".sqlite")&&length(n)>13;}
} // namespace deck
