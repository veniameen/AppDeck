#include "../src/core.hpp"
#include "../src/edge_policy.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
int main(){
 unsigned checks=0;auto check=[&](bool b){++checks;if(!b){std::cerr<<"FAILED assertion "<<checks<<'\n';std::exit(1);}};
 auto close=[](double a,double b){return std::abs(a-b)<1e-7;};
 check(deck::bloom(-1)==0);check(deck::bloom(2)==1);check(deck::bloom(0)==0);check(deck::bloom(1)==1);
 double last=-1;for(int i=0;i<=1000;++i){double p=deck::bloom(i/1000.0);check(p>=last&&p>=0&&p<=1);last=p;}
 const deck::Box screens[]={{0,24,1512,920},{-2560,40,2560,1400},{0,-1300,1920,1200},{3456,800,1600,900},{0,0,744,460}};
 for(auto s:screens){for(unsigned rows:{1u,4u,6u}){auto e=deck::edgeBounds(s,deck::edgeWidth,deck::edgeHeight(rows));
  check(e.x>=s.x&&e.y>=s.y&&e.x+e.w<=s.x+s.w+1e-7&&e.y+e.h<=s.y+s.h+1e-7);
  // The dock is flush with the right border of the visible frame and vertically centred.
  check(close(e.x+e.w,s.x+s.w));check(close(e.y+e.h/2,s.y+s.h/2));
  double lastWidth=0;
  for(int i=0;i<=100;++i){auto f=deck::edgeFrame(e,i/100.0,false);check(std::isfinite(f.x)&&f.w>0&&f.h>0);check(close(f.x+f.w,e.x+e.w));check(close(f.y+f.h/2,e.y+e.h/2));check(f.w<=e.w&&f.h<=e.h);
   // Slide-out: width only grows, height never changes, nothing crosses the screen border.
   check(f.w>=lastWidth);check(close(f.h,e.h));check(f.x+f.w<=s.x+s.w+1e-7);lastWidth=f.w;}
  auto end=deck::edgeFrame(e,1,false),reduce=deck::edgeFrame(e,.04,true);check(close(end.w,e.w)&&close(end.h,e.h));check(close(reduce.x,e.x)&&close(reduce.h,e.h));
 }}
 // The dock grows with the visible rows, one row step at a time.
 for(unsigned r=1;r<deck::edgeMaxRows;++r)check(deck::edgeHeight(r+1)-deck::edgeHeight(r)==deck::edgeStep);
 check(deck::edgeStep==deck::edgeRowHeight+deck::edgeRowGap);check(deck::edgeHeight(0)==deck::edgeHeight(1));
 // Visible rows follow the number of profiles up to six and never push the dock off the screen.
 check(deck::edgeMaxRows==6);check(deck::edgeHotkeys==8);
 check(deck::edgeRows(0,949)==1);check(deck::edgeRows(1,949)==1);check(deck::edgeRows(3,949)==3);check(deck::edgeRows(5,949)==5);check(deck::edgeRows(6,949)==6);check(deck::edgeRows(7,949)==6);check(deck::edgeRows(9,949)==6);check(deck::edgeRows(30,949)==6);
 check(deck::edgeRows(6,500)<6);check(deck::edgeRows(6,0)==deck::edgeMinRows);check(deck::edgeRows(6,-100)==deck::edgeMinRows);
 for(unsigned n=0;n<=40;++n)for(double h=300;h<=3000;h+=53){unsigned r=deck::edgeRows(n,h);check(r>=deck::edgeMinRows&&r<=deck::edgeMaxRows);check(r==deck::edgeMinRows||deck::edgeHeight(r)<=h-24);
  if(r<deck::edgeMaxRows&&n>r)check(deck::edgeHeight(r+1)>h-24);} // it is also the LARGEST count that fits
 for(unsigned n=0;n<=40;++n){unsigned r=deck::edgeRows(n,5000);check(n>=deck::edgeMaxRows?r==deck::edgeMaxRows:r==(n?n:1));} // one row per profile, no spare rows
 check(deck::edgeBounds({0,0,120,90}).w<=120);check(deck::edgeBounds({0,0,120,90}).h<=90);
 // Everything that is not visible scrolls: the range is exactly the hidden rows, never negative.
 for(unsigned n=0;n<=40;++n)for(unsigned r=1;r<=deck::edgeMaxRows;++r){double range=deck::edgeScrollRange(n,r);check(range>=0);
  check(n<=r?range==0:close(range,(n-r)*deck::edgeStep));check(close(deck::edgeListHeight(r)+range,deck::edgeListHeight(n>r?n:r)));}
 // A dragged row lands on the nearest place inside the list; the others make room without gaps.
 for(unsigned n=0;n<=24;++n){unsigned before=0;
  for(double top=-300;top<=n*deck::edgeStep+300;top+=.25){unsigned k=deck::edgeDropSlot(top,n);check(k<(n?n:1));check(k>=before);before=k;
   if(n>1&&top>=0&&top<=(n-1)*deck::edgeStep)check(std::abs(top-k*deck::edgeStep)<=deck::edgeStep/2+1e-9);}
  check(deck::edgeDropSlot(std::nan(""),n)==0);check(deck::edgeDropSlot(1e300,n)==(n?n-1:0));check(deck::edgeDropSlot(-1e300,n)==0);}
 for(unsigned n=1;n<=12;++n)for(unsigned from=0;from<n;++from)for(unsigned to=0;to<n;++to){
  std::vector<unsigned> list;for(unsigned i=0;i<n;++i)if(i!=from)list.push_back(i);list.insert(list.begin()+to,from); // the order after the move
  std::vector<bool> taken(n,false);
  for(unsigned i=0;i<n;++i){unsigned k=deck::edgeShiftedSlot(i,from,to);check(k<n&&!taken[k]);taken[k]=true;check(list[k]==i);}}
 // Soft edges: clean at rest at the top and at the end of the list, shaded only while a row is cut.
 for(unsigned n=1;n<=40;++n)for(unsigned r=1;r<=deck::edgeMaxRows&&r<=n;++r){double view=deck::edgeListHeight(r),end=deck::edgeScrollRange(n,r);
  check(deck::edgeCut(0)==0&&deck::edgeCut(view)==0);check(deck::edgeCut(end)==0&&deck::edgeCut(end+view)==0);
  for(unsigned k=0;k<=n;++k)check(deck::edgeCut(k*deck::edgeStep)==0&&deck::edgeCut(k*deck::edgeStep+deck::edgeRowHeight)==0);}
 {double last=0;for(double y=-50;y<=40*deck::edgeStep;y+=.125){double c=deck::edgeCut(y);check(c>=0&&c<=1);check(std::abs(c-last)<=.125/10+1e-9);last=c; // continuous: no pops
   double r=std::fmod(y,deck::edgeStep);if(y<=0||r>=deck::edgeRowHeight)check(c==0);if(y>0&&r>=10&&r<=deck::edgeRowHeight-10)check(c==1);}}
 check(deck::edgeCut(std::nan(""))==0);check(deck::edgeCut(1e300)==0);check(deck::edgeCut(29,0)==0);check(deck::edgeCut(29,-1)==0);
 // Auto-scroll: still in the middle, up near the top, down near the bottom, bounded, monotonic.
 for(double h:{58.0,124.0,388.0,1000.0}){double last=-1e9;
  for(double y=-100;y<=h+100;y+=.5){double v=deck::edgeAutoScroll(y,h);check(v>=last-1e-12);last=v;check(v>=-10&&v<=10);
   if(y<=0)check(close(v,-10));if(y>=h)check(close(v,10));}
  check(deck::edgeAutoScroll(h/2,h)==0);check(deck::edgeAutoScroll(1,h)<0);check(deck::edgeAutoScroll(h-1,h)>0);
  check(close(deck::edgeAutoScroll(10,h),-deck::edgeAutoScroll(h-10,h)));}
 check(deck::edgeAutoScroll(5,0)==0);check(deck::edgeAutoScroll(5,-10)==0);check(deck::edgeAutoScroll(std::nan(""),300)==0);check(deck::edgeAutoScroll(5,std::nan(""))==0);
 std::mt19937 rng(42);std::uniform_real_distribution<double> size(500,9000),origin(-10000,10000);
 for(int k=0;k<1000;++k){deck::Box s{origin(rng),origin(rng),size(rng),size(rng)};
  for(unsigned n=1;n<=4;++n){auto first=deck::tileBox(s,n,0);for(unsigned i=0;i<n;++i){auto b=deck::tileBox(s,n,i);check(b.x>=s.x&&b.y>=s.y&&b.x+b.w<=s.x+s.w+.001&&b.y+b.h<=s.y+s.h+.001);check(close(b.w,first.w)&&close(b.h,first.h));
    for(unsigned j=0;j<i;++j){auto a=deck::tileBox(s,n,j);check(b.x>=a.x+a.w||a.x>=b.x+b.w||b.y>=a.y+a.h||a.y>=b.y+b.h);}
  }}
 }
 check(deck::tileBox({0,0,1000,1000},0,0).w==0);check(deck::tileBox({0,0,1000,1000},5,0).w==0);check(deck::tileBox({0,0,1000,1000},4,4).w==0);
 check(deck::absoluteLocalPath("/Users/tester/Проекты"));check(!deck::absoluteLocalPath("../Projects"));check(!deck::absoluteLocalPath("ssh://user@example"));check(!deck::absoluteLocalPath("/tmp/a\nb"));
 for(auto k:{"auth.json","sessions","Cookies","Local Storage","electron-persisted-atom-state","thread-workspace-root-hints","thread-project-assignments","app-server-projects-migration-by-host","auth","password","state_5.sqlite"}){check(!deck::workspaceKey(k));check(!deck::settingsLeaf(k));}
 check(deck::workspaceKey("local-projects"));check(deck::projectId("local-291e64fc0cc39bf506df6d6d0004a476"));check(deck::projectId("749d7688-afe1-47cd-b737-16b46098971f"));
 for(auto bad:{"","cloud:user@example.com:proj","remote/host","a b","id\n","../x"})check(!deck::projectId(bad));
 check(deck::workspaceKey("electron-saved-workspace-roots"));check(deck::workspaceKey("electron-workspace-root-labels"));check(deck::workspaceKey("project-order"));
 check(deck::settingsLeaf("config.toml"));check(deck::settingsLeaf("skills"));check(!deck::settingsLeaf("../config.toml"));
 // Shared history: three rollout/lock folders may be linked; databases and credentials never are.
 for(auto k:{"sessions","archived_sessions","thread-writer-locks"})check(deck::historyLeaf(k));
 for(auto k:{"auth.json","state_5.sqlite","thread_history_1.sqlite","logs_2.sqlite","memories","history.jsonl","user-data","../sessions","sessions/x",".codex-global-state.json","config.toml"})check(!deck::historyLeaf(k));
 check(deck::stateDatabase("state_5.sqlite"));check(deck::stateDatabase("state_12.sqlite"));check(!deck::stateDatabase("state_.sqlite"));check(!deck::stateDatabase("state_5.sqlite-wal"));check(!deck::stateDatabase("logs_2.sqlite"));check(!deck::stateDatabase("xstate_5.sqlite"));
 std::cout<<checks<<" edge/geometry/allowlist assertions passed\n";
}
