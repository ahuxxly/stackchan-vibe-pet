#include "RuntimeHealth.h"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
int main() {
 runtime_health::Monitor h;
 assert(h.completed()==0 && h.shown()==0 && !h.takeChanged(true));
 const std::string initial=h.text();
 // Drawing or elapsed wall time alone never advances a claimed completed loop.
 for(unsigned wall=0;wall<100000;wall+=1000){assert(h.text()==initial);assert(h.completed()==0);}
 h.complete(1500,7,30,42,1,true);assert(h.completed()==7 && h.shown()==7);
 assert(h.takeChanged(true)&&!h.takeChanged(true));
 assert(strstr(h.text(),"L000007 I000030/42 G1 R"));
 h.complete(2499,100,50,40,1,true);assert(h.completed()==100 && h.shown()==7 && !h.takeChanged(true));
 h.complete(2500,101,51,74,2,false);assert(h.shown()==101 && !h.ready());
 assert(!h.takeChanged(false));assert(h.takeChanged(true)); // Sleep cannot eat wake-up refresh.
 const std::string paused=h.text();
 for(int draws=0;draws<500;++draws)assert(h.text()==paused);
 runtime_health::Monitor wrap;
 wrap.complete(0xffffff00U,999999,999999,0xffffffffU,0xffffffffU,false);wrap.takeChanged(true);
 assert(std::strlen(wrap.text())*6+1<=320);
 wrap.complete(0x2e7U,1000000,1000000,40,0,true);assert(wrap.shown()==999999);
 wrap.complete(0x2e8U,1000001,1000001,40,0,true);assert(wrap.shown()==1000001);
 assert(strstr(wrap.text(),"L000001 I000001/40 G0 R"));
 puts("PASS runtime health: completed-loop-only, 1Hz publication, frozen between completions, screen-off pending, timer/counter wrap and bounded labels");
}
