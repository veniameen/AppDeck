#include "../src/usage_policy.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
int main(){
 unsigned checks=0;auto check=[&](bool b){++checks;if(!b){std::cerr<<"FAILED assertion "<<checks<<'\n';std::exit(1);}};
 using deck::UsageWindow;using deck::UsageLevel;
 // Windows are classified by duration, never by the primary/secondary name.
 check(deck::usageWindow(10080)==UsageWindow::Weekly);check(deck::usageWindow(7*24*60+30)==UsageWindow::Weekly);check(deck::usageWindow(6*24*60)==UsageWindow::Weekly);
 check(deck::usageWindow(300)==UsageWindow::Short);check(deck::usageWindow(60)==UsageWindow::Short);check(deck::usageWindow(1440)==UsageWindow::Short);
 check(deck::usageWindow(0)==UsageWindow::Other);check(deck::usageWindow(-5)==UsageWindow::Other);check(deck::usageWindow(43200)==UsageWindow::Other);check(deck::usageWindow(2*24*60)==UsageWindow::Other);
 // Remaining percentage is clamped and rounded; garbage never escapes the 0..100 range.
 check(deck::remainingPercent(0)==100);check(deck::remainingPercent(100)==0);check(deck::remainingPercent(63)==37);check(deck::remainingPercent(62.6)==37);
 check(deck::remainingPercent(150)==0);check(deck::remainingPercent(-20)==100);check(deck::remainingPercent(std::numeric_limits<double>::quiet_NaN())==0);
 check(deck::remainingPercent(std::numeric_limits<double>::infinity())==0);check(deck::remainingPercent(-std::numeric_limits<double>::infinity())==100);
 for(int used=-50;used<=200;++used){int r=deck::remainingPercent(used);check(r>=0&&r<=100);double f=deck::usageFill(r);check(f>=0&&f<=1);check((r==0)==(f==0));}
 check(deck::usageLevel(100)==UsageLevel::Ok);check(deck::usageLevel(25)==UsageLevel::Ok);check(deck::usageLevel(24)==UsageLevel::Low);check(deck::usageLevel(10)==UsageLevel::Low);
 check(deck::usageLevel(9)==UsageLevel::Critical);check(deck::usageLevel(1)==UsageLevel::Critical);check(deck::usageLevel(0)==UsageLevel::Exhausted);check(deck::usageLevel(-3)==UsageLevel::Exhausted);
 check(deck::usageFill(1)>=.03);check(deck::usageFill(100)==1);
 // Cadence: ONE rule — an account at most once an hour, whatever the state of its profile ...
 const double t=1'800'000'000;
 check(deck::usageDue(t,0));check(deck::usageDue(t,-5));check(deck::usageDue(t,t+50)); // never asked / garbage / clock went back
 check(!deck::usageDue(t,t));check(!deck::usageDue(t+3599,t));check(deck::usageDue(t+3600,t));check(deck::usageDue(t+86400,t));
 for(double age=0;age<deck::usageInterval;age+=11)check(!deck::usageDue(t+age,t));
 // ... and automatic probes of different profiles at least five minutes apart.
 check(deck::usageSpaced(t,0));check(deck::usageSpaced(t,t+10));check(!deck::usageSpaced(t,t));check(!deck::usageSpaced(t+299,t));check(deck::usageSpaced(t+300,t));
 for(double gap=0;gap<deck::usageSpacing;gap+=3)check(!deck::usageSpaced(t+gap,t));
 // Simulated day with five overdue profiles: every automatic probe obeys both rules.
 {double checked[5]={0,0,0,0,0},last=0;unsigned probes=0;
  for(double now=t;now<t+86400;now+=2){int pick=-1;double oldest=1e18; // the 2 s status tick
   for(int i=0;i<5;++i)if(deck::usageDue(now,checked[i])&&deck::usageSpaced(now,last)&&checked[i]<oldest){oldest=checked[i];pick=i;}
   if(pick<0)continue;check(last==0||now-last>=deck::usageSpacing);check(checked[pick]==0||now-checked[pick]>=deck::usageInterval);checked[pick]=now;last=now;++probes;}
  check(probes>=5*23&&probes<=5*24+5);} // about one probe per account per hour, never more
 check(deck::usageManualAllowed(t,0));check(!deck::usageManualAllowed(t+5,t));check(!deck::usageManualAllowed(t+19.9,t));check(deck::usageManualAllowed(t+20,t));check(deck::usageManualAllowed(t,t+10));
 check(deck::usageManualFloor<deck::usageSpacing&&deck::usageSpacing<deck::usageInterval&&deck::usageInterval>=3600&&deck::usageSpacing>=300&&deck::usageProbeTimeout>=5&&deck::usageProbeTimeout<=60);
 std::cout<<checks<<" account-limit policy assertions passed\n";
}
