#include "MusePolicy.h"
#include <cassert>
#include <cstdio>
using namespace muse;
using namespace companion;
int main() {
  State s;Director d;Local local;
  assert(d.update(s,0,local,0)==Clip::Welcome);
  assert(d.update(s,0,local,1500)==Clip::Disconnected);
  s.connect(true);assert(d.update(s,0,local,1600)==Clip::Disconnected);
  s.receive(0,0x2040ff,1,4,.5,1700);
  assert(d.update(s,0,local,1700)==Clip::Think);
  assert(d.update(s,0,local,3699)==Clip::Think);
  assert(d.update(s,0,local,3700)==Clip::Focus);
  s.receiveHostPoll(40000);
  assert(d.update(s,0,local,46700)==Clip::Cached);
  assert(!s.approvalEligible(0,46700));
  assert(d.update(s,0,local,130000)==Clip::Disconnected);
  // A poll alone cannot resurrect an expired work expression.
  s.receiveHostPoll(130001);assert(d.update(s,0,local,130002)==Clip::Disconnected);
  s.receive(0,0xff6d00,1,4,.5,140000);
  assert(d.update(s,0,local,140000)==Clip::Wait);
  local.pet=true;assert(d.update(s,0,local,140010)==Clip::Pet);
  assert(s.approvalEligible(0,140010)); // Expression has no authority to mutate approval.
  local.dizzy=true;assert(d.update(s,0,local,140020)==Clip::Dizzy);
  local={false,false,true,-1};assert(d.update(s,0,local,140030)==Clip::Dance);
  local={false,false,false,8};assert(d.update(s,0,local,140040)==Clip::Look);
  local={};s.receive(0,0xff2020,1,4,.5,140050);
  assert(d.update(s,0,local,140050)==Clip::Blocked);
  s.receive(0,0x20ff60,1,4,.5,140060);
  assert(d.update(s,0,local,140060)==Clip::Celebrate);
  assert(d.update(s,0,local,142060)==Clip::Idle);
  s.receive(0,0xffffff,1,4,.5,142100);
  assert(host(s,0,142100)==Host::Idle); // Unknown host colour doesn't invent activity.
  assert(d.update(s,1,local,142100)==Clip::Disconnected);
  assert(host(s,-1,0)==Host::Unknown && host(s,6,0)==Host::Unknown);
  // Counter wrap and long animation uptime never exceed the eight-frame library.
  for(unsigned id=0;id<kStates;++id)for(uint32_t age:{0U,124U,125U,875U,1000U,1750U,0xffffffffU})
    for(bool preview:{false,true})assert(frame(static_cast<Clip>(id),age,preview)<8);
  assert(frame(Clip::Idle,875)==7 && frame(Clip::Idle,1000)==6 && frame(Clip::Idle,1750)==0);
  assert(frame(Clip::Blink,2000)==7 && frame(Clip::Blink,2000,true)==0);
  assert(gazeFrame(0)==5 && gazeFrame(8)==2 && gazeFrame(16)==0 && gazeFrame(-1)==0);
  assert((crc32(reinterpret_cast<const uint8_t*>("123456789"),9)^0xffffffffU)==0xcbf43926U);
  State wrap;wrap.connect(true);wrap.receive(0,0x2040ff,1,4,.5,0xfffffff0U);
  Director rollover;assert(rollover.update(wrap,0,local,0xfffffff0U)==Clip::Think);
  assert(rollover.update(wrap,0,local,2010U)==Clip::Focus);
  // Every gallery pixel is harmless even with a fresh approval and armed motion context.
  for(bool connected:{false,true})for(bool awaiting:{false,true})for(bool motion:{false,true}) {
    Context c{Page::Gallery,7,0,connected,awaiting,motion};
    for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
      const auto i=hit(x,y,c);
      assert(i==Intent::None || i==Intent::CloseControl || i==Intent::Stop ||
             i==Intent::GalleryPrevious || i==Intent::GalleryNext || i==Intent::ToggleSkin);
    }
    TouchRouter t;assert(t.screen(true,50,150,0,{Page::Control,7,0,true,true})==Intent::None);
    assert(t.screen(false,50,150,1000,c)==Intent::None);
    assert(t.rear(true,2000)==Intent::None);
  }
  std::puts("PASS Lumi production policy: honest freshness, local precedence, timeline bounds, CRC, 614400 gallery pixels and held-approval isolation");
}
