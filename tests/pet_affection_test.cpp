// Real gesture detector and real AffectionController; raw sensor and NVS are fake.
#include "PetGesture.h"
#include "AffectionController.h"
#include <cassert>
#include <cstdio>
using pet_gesture::Detector;
static void ready(Detector& g,uint32_t at){g.update(false,true,at);g.update(false,true,at+1000);assert(g.ready());}
int main(){
 for(unsigned channel=0;channel<3;++channel)for(uint8_t level=1;level<=3;++level){
  uint8_t raw[3]={};raw[channel]=level;assert(pet_gesture::contact(raw[0],raw[1],raw[2]));
 }
 assert(!pet_gesture::contact(0,0,0));
 for(uint32_t start:{0U,0xfffff000U})for(uint32_t duration:{139U,140U,2999U,3000U,3140U,6000U}){
  Detector g;ready(g,start);auto t=start+1100;auto e=g.update(true,true,t);assert(!e.began);
  if(duration>=140)assert(g.update(true,true,t+140).began);
  assert(!g.update(false,true,t+duration).ended);
  e=g.update(false,true,t+duration+180);assert(e.ended==(duration>=140));
  assert(e.durationMs==duration && e.longStroke==(duration>=3000));
  for(unsigned n=0;n<20;++n)assert(!g.update(false,true,t+duration+181+n).ended);
 }
 Detector cancelled;ready(cancelled,0);cancelled.update(true,true,1100);cancelled.update(true,true,1240);
 assert(!cancelled.update(true,false,4100).ended);assert(!cancelled.active());
 assert(!cancelled.update(false,true,4300).ended); // screen off cancels, no replay on wake
 Detector gaps;ready(gaps,0);gaps.update(true,true,1100);gaps.update(true,true,1240);
 gaps.update(false,true,2000);gaps.update(true,true,2100);gaps.update(false,true,4100);
 assert(gaps.update(false,true,4280).longStroke && gaps.session()==1);
 Preferences::numbers={{"aff",459},{"seq",10}};Preferences prefs;
 AffectionController a;tickMs=1000;a.begin(&prefs);assert(a.state().affection==459 && a.loadOk());
 tickMs=5000;auto result=a.applyEvent("petting",1,1,"one",tickMs);
 assert(result.delta==6 && result.state.affection==465 && result.state.mood==20 && result.state.seq==11);
 assert(a.applyEvent("petting",1,1,"one",tickMs).duplicate);
 a.update(tickMs,false);assert(a.saveAttempts()==0 && a.pendingSave()); // motion defers NVS
 a.update(tickMs);assert(a.lastSaveOk() && !a.pendingSave() && a.persistedAffection()==465);
 assert(a.savedAffectionBytes()==2 && a.savedSequenceBytes()==4);
 tickMs=5100;assert(a.petCooldownRemaining(tickMs)==4900);
 auto reduced=a.applyEvent("petting",1,1,"two",tickMs);assert(reduced.delta>0 && reduced.delta<result.delta);
 AffectionController reboot;reboot.begin(&prefs);assert(reboot.state().affection==465 && reboot.state().mood==0);
 tickMs=6000;auto shortStroke=a.debugAdjust(-1);assert(shortStroke.delta==-1);
 // Failed/partial stores remain dirty, are bounded, and recover without inventing success.
 a.debugAdjust(20);Preferences::failSeq=true;a.update(tickMs);
 assert(a.pendingSave() && !a.lastSaveOk() && a.savedSequenceBytes()==0);
 auto attempts=a.saveAttempts();for(unsigned i=1;i<100;++i){tickMs=6000+i;a.update(tickMs);}assert(a.saveAttempts()==attempts);
 Preferences::failSeq=false;tickMs=11000;a.update(tickMs);assert(a.lastSaveOk() && !a.pendingSave());
 AffectionController afterFailure;afterFailure.begin(&prefs);assert(afterFailure.state().affection==a.state().affection);
 a.reset(999);tickMs=20000;result=a.applyEvent("petting",1,1,"cap",tickMs);assert(result.delta==1 && a.state().affection==1000);
 tickMs=26000;result=a.applyEvent("petting",1,1,"cap2",tickMs);assert(result.delta==0);
 a.reset(0);assert(a.debugAdjust(-1).delta==0);
 Preferences::failOpen=true;a.debugAdjust(10);tickMs=40000;a.update(tickMs);assert(a.pendingSave() && !a.lastSaveOk());
 Preferences::failOpen=false;tickMs=45000;a.update(tickMs);assert(a.lastSaveOk());
 puts("PASS real gesture/affection: raw levels1-3,140ms debounce,2999/3000ms boundary,release180ms,wrap,wake,no replay,actual delta,cap,cooldown,dedupe,NVS deferral/failure/retry/reboot (fake sensors/storage)");
}
