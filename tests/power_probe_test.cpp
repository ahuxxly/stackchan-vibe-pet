#include "PowerGateProbe.h"
#include <cassert>
#include <cstdio>
#include <cstddef>
#include <vector>
#include <algorithm>
using power_probe::WriteResult;using power_probe::State;using power_probe::OffSnapshot;
// No bitOn/bitOff or write API: OffSnapshot compiles against a read-only type.
struct Reader {
  uint32_t now=0,cost=1;unsigned reads=0;int fail=-1;
  std::array<uint8_t,256> regs{};std::vector<uint8_t> addresses;
  bool readRegister(uint8_t reg,uint8_t* out,size_t n) {
    assert(n==1);const unsigned i=reads++;addresses.push_back(reg);now+=cost;
    *out=int(i)==fail?0xff:regs[reg];return int(i)!=fail;
  }
};
void tick(OffSnapshot& p,Reader& r,bool off=true){p.tick(r,[&]{return r.now;},off,p.attempt);}
void finish(OffSnapshot& p,Reader& r){for(unsigned n=0;n<10&&p.state==State::Pending;++n){tick(p,r);r.now+=40;}}

int main(){
 power_probe::WriteHistory h;h.record(true,0,3,5,9);h.record(false,1,6,8,9);
 assert(h.enable.sampled&&h.enable.result==WriteResult::Failed&&h.enable.started==3&&h.enable.finished==5);
 assert(h.disable.result==WriteResult::Succeeded&&h.disable.attempt==9);
  // Normal OFF snapshot: exact allowlist, one read/tick, timestamps, no write API.
  Reader r;r.regs[3]=0x21;r.regs[5]=0xa4;r.regs[11]=1;r.regs[2]=6;OffSnapshot p;
  assert(!p.request(0,false,1)&&r.reads==0);assert(p.request(0,true,1));
  assert(!p.request(0,true,2));tick(p,r);const auto n=r.reads;tick(p,r);assert(r.reads==n);
  r.now+=40;finish(p,r);assert(p.state==State::Complete&&r.reads==8);
  assert(std::equal(r.addresses.begin(),r.addresses.end(),p.registers.begin()));
  for(const auto& s:p.samples)assert(s.valid&&s.finished-s.started==1);
  assert(p.samples[1].value==0x21&&p.samples[6].value==6&&!p.request(r.now,true,2));
  // Every NACK terminates, failed bytes never promoted; no retry storm.
  for(int f=0;f<8;++f){Reader x;x.fail=f;OffSnapshot q;assert(q.request(0,true,3));finish(q,x);
    assert(q.state==State::ReadFailed&&x.reads==unsigned(f+1));
    assert(!q.samples[f].valid&&q.samples[f].value==-1);tick(q,x);assert(x.reads==unsigned(f+1));}
  // Cancellation at each boundary: no further I2C call when any OFF precondition disappears.
  for(unsigned f=0;f<8;++f){Reader x;OffSnapshot q;assert(q.request(0,true,4));
    for(unsigned n=0;n<f;++n){tick(q,x);x.now+=40;}const auto n=x.reads;tick(q,x,false);
    assert(q.state==State::Cancelled&&x.reads==n);}
  Reader changed;OffSnapshot pc;assert(pc.request(0,true,4));tick(pc,changed);changed.now+=40;
  pc.tick(changed,[&]{return changed.now;},true,5);
  assert(pc.state==State::Cancelled&&changed.reads==1); // briefly ON then OFF is still a new attempt
  Reader high;high.regs[7]=1;OffSnapshot ph;assert(ph.request(0,true,5));tick(ph,high);
  assert(ph.state==State::InputHigh&&high.reads==1);
  Reader late;OffSnapshot pl;assert(pl.request(0,true,6));
  for(unsigned i=0;i<7;++i){tick(pl,late);late.now+=40;}late.regs[7]=1;tick(pl,late);
  assert(pl.state==State::InputHigh&&late.reads==8);
  Reader deadline;OffSnapshot pd;assert(pd.request(0,true,7));deadline.now=500;tick(pd,deadline);
  assert(pd.state==State::Expired&&deadline.reads==0);
  Reader slow;slow.cost=501;OffSnapshot ps;assert(ps.request(0,true,8));tick(ps,slow);
  assert(ps.state==State::Expired&&slow.reads==1&&ps.samples[0].finished==501);
  // No claim that a C++ deadline preempts the blocking driver. Its overrun is explicit.
  Reader wrap;wrap.now=UINT32_MAX-100;OffSnapshot pw;assert(pw.request(wrap.now,true,9));finish(pw,wrap);
  assert(pw.state==State::Complete&&wrap.reads==8);

 Reader cancelled;OffSnapshot q;assert(q.request(0,true,0));q.cancel();tick(q,cancelled);
 assert(q.state==State::Cancelled&&cancelled.reads==0);
 puts("PASS production OFF snapshot: field times, all NACK/cancel boundaries, HIGH, deadline/overrun, epoch, wrap, rate limit; fake read-only IO");
}
