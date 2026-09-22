#pragma once
// Pure C++ rules for the account-limit indicator; no AppKit, process or file access.
namespace deck {
// Codex reports up to two windows per metered limit. Their NAMES are not stable: on a Pro plan
// the weekly window arrives as `primary` and there is no 5-hour one. Classify by duration only.
enum class UsageWindow{Short,Weekly,Other};
inline UsageWindow usageWindow(long minutes){
 if(minutes>=6L*24*60&&minutes<=8L*24*60)return UsageWindow::Weekly;
 if(minutes>0&&minutes<=24L*60)return UsageWindow::Short;
 return UsageWindow::Other;
}
inline int remainingPercent(double used){
 if(!(used==used))return 0; // NaN
 double r=100-used;if(r<0)r=0;if(r>100)r=100;return (int)(r+.5);
}
enum class UsageLevel{Ok,Low,Critical,Exhausted};
inline UsageLevel usageLevel(int remaining){
 if(remaining<=0)return UsageLevel::Exhausted;if(remaining<10)return UsageLevel::Critical;if(remaining<25)return UsageLevel::Low;return UsageLevel::Ok;
}
// Automatic polling, one rule for every profile whatever its state (running, stopped, failed, signed
// out): an account is asked at most once an hour, and two automatic probes are never closer than five
// minutes, so the profiles are checked one after another, spread out — never in a burst. Nothing else
// triggers an automatic probe (no extra look after a launch, a stop or a window reset).
// A manual refresh is the user's explicit request: immediate, but at most once per 20 s per account.
constexpr double usageInterval=3600,usageSpacing=300,usageManualFloor=20,usageProbeTimeout=20;
inline bool usageDue(double now,double checked){return checked<=0||now<checked||now-checked>=usageInterval;}       // never asked / clock went back / an hour passed
inline bool usageSpaced(double now,double lastProbe){return lastProbe<=0||now<lastProbe||now-lastProbe>=usageSpacing;} // distance to the previous probe of ANY profile
inline bool usageManualAllowed(double now,double checked){return checked<=0||now<checked||now-checked>=usageManualFloor;}
// Fraction of a bar to fill for "remaining"; a non-zero remainder never renders as an empty bar.
inline double usageFill(int remaining){if(remaining<=0)return 0;double f=remaining/100.0;return f<.03?.03:(f>1?1:f);}
} // namespace deck
