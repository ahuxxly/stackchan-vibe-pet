#include "CompanionCore.h"
#include "HidReassembly.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
using namespace companion;

int main() {
  State s;
  assert(!s.fresh(0,0));
  s.connect(true);
  assert(!s.fresh(0,0)); // Bluetooth alone is not a running agent.
  assert(s.receive(0,0xff6d00,1,4,.4f,100));
  assert(s.awaiting(0,101));
  const auto revision=s.agents[0].revision;
  s.receive(0,0xff6d00,1,4,.4f,102);
  assert(s.agents[0].revision==revision); // Unchanged heartbeat does not cancel a deliberate hold.
  s.receive(0,0xff6d00,1,5,.4f,103);
  assert(s.agents[0].revision==revision+1); // Changed status cancels the pending press.
  s.receive(0,0xff6d00,1,4,.4f,100);
  assert(!s.fresh(1,101)); // One agent does not refresh the others.
  assert(s.receive(1,0x2040ff,.5f,4,.4f,200));
  assert(!s.awaiting(1,201));
  assert(!s.receive(6,0,0,0,0,200));
  assert(!s.receive(2,0,std::numeric_limits<float>::quiet_NaN(),1,0,200));
  assert(!s.fresh(0,100+kLeaseMs));
  assert(s.fresh(1,100+kLeaseMs));
  s.connect(false);
  assert(!s.fresh(1,202));
  s.connect(true);
  assert(!s.fresh(0,203)); // Reconnect requires a NEW host report.
  s.receive(0,0xff6d00,1,4,1,0xfffffff0U);
  assert(s.fresh(0,32)); // millis wraparound.
  assert(!s.fresh(0,0xfffffff0U+kLeaseMs));
  State partial;
  partial.connect(true);
  assert(partial.receivePatch(0,1,0xff6d00,0,0,0,10));
  assert(!partial.fresh(0,11)); // Color alone cannot imply a live status.
  assert(partial.receivePatch(0,6,0,1,4,0,20));
  assert(partial.awaiting(0,21)); // Valid partial reports combine within a session.
  assert(partial.receivePatch(0,2,0,.5f,0,0,30));
  assert(partial.agents[0].color==0xff6d00 && partial.agents[0].effect==4);
  partial.connect(false);partial.connect(true);
  partial.receivePatch(0,6,0,1,4,0,40);
  assert(!partial.fresh(0,41)); // Old connection's color must never leak into a new one.
  assert(!partial.receivePatch(0,2,0,INFINITY,0,0,50));

  TouchRouter t;
  Context c{Page::Pet,1,0,true,true};
  // All rear contact is owned by the pet handler and has no command surface.
  for (unsigned i=0;i<10000;++i) assert(t.rear(i%2==0,i)==Intent::None);
  assert(t.screen(true,10,140,100,c)==Intent::None);
  assert(t.screen(false,10,140,900,c)==Intent::None);
  t.screen(true,280,25,1000,c);
  assert(t.screen(false,280,25,1100,c)==Intent::OpenControl);
  c.page=Page::Control; ++c.generation;
  t.screen(true,20,145,2000,c);
  assert(t.screen(false,20,145,2050,c)==Intent::None); // ALLOW needs a deliberate hold.
  t.screen(true,20,145,3000,c);
  assert(t.screen(false,20,145,3700,c)==Intent::Approve);
  t.screen(true,20,145,4000,c);
  c.selected=1;
  assert(t.screen(false,20,145,4700,c)==Intent::None); // Agent changed mid-touch.
  t.screen(true,20,145,5000,c);
  c.awaiting=false;
  assert(t.screen(false,20,145,5700,c)==Intent::None); // Pending state disappeared.
  c.awaiting=true;
  t.screen(true,20,145,6000,c);
  c.connected=false;
  assert(t.screen(false,20,145,6700,c)==Intent::None);
  c.connected=true;
  t.screen(true,20,145,7000,c);
  assert(t.screen(true,300,220,7100,c)==Intent::None);
  assert(t.screen(false,20,145,7800,c)==Intent::None); // Drag out and back cancels.
  t.screen(true,20,145,8000,c);
  ++c.generation;
  assert(t.screen(false,20,145,8700,c)==Intent::None); // Page/connection epoch changed.
  t.screen(true,30,70,9000,c);
  assert(t.screen(false,30,70,9100,c)==Intent::Agent0);
  c.page=Page::Pet; ++c.generation;
  t.screen(true,30,70,9200,c);
  c.page=Page::Control; ++c.generation;
  assert(t.screen(false,30,70,9300,c)==Intent::None);

  uint8_t frame[63];
  assert(keyFrame("ACT07",true,frame));
  const char* golden="{\"m\":\"v.oai.hid\",\"p\":{\"k\":\"ACT07\",\"act\":1}}\r\n";
  assert(frame[0]==2 && frame[1]==std::strlen(golden));
  assert(std::memcmp(frame+2,golden,std::strlen(golden))==0);
  assert(keyFrame("AG05",false,frame));
  assert(!keyFrame("BAD\"INJECTION",true,frame));
  assert(!keyFrame("ACT99",true,frame));
  assert(!keyFrame("AG06",true,frame));
  HidReassembly rx;
  const uint8_t one[]={2,5,'{','"','m','"',':'};
  const uint8_t two[]={2,3,'"','x','"'};
  assert(rx.append(one,sizeof(one),1,10));
  assert(rx.append(two,sizeof(two),2,10));
  assert(std::strcmp(rx.data(),"{\"m\":\"x\"")==0);
  assert(rx.append(two,sizeof(two),3000,10)); // Partial frames expire.
  assert(std::strcmp(rx.data(),"\"x\"")==0);
  assert(rx.append(one,sizeof(one),3001,11)); // New connection clears partial JSON.
  assert(std::strcmp(rx.data(),"{\"m\":")==0);
  const uint8_t bad[]={2,61,'x'};
  assert(!rx.append(bad,sizeof(bad),3002,11) && rx.size()==0);
  const uint8_t nul[]={2,2,'{',0};
  assert(!rx.append(nul,sizeof(nul),3002,11));
  uint8_t full[63];std::memset(full,'x',sizeof(full));full[0]=2;full[1]=61;
  for(int i=0;i<33;++i)assert(rx.append(full,sizeof(full),4000,11));
  assert(!rx.append(full,sizeof(full),4000,11)); // Bounded before copy.
  assert(rx.size()==0);
  for(unsigned n=0;n<10000;++n) {
    uint8_t bytes[63];for(unsigned i=0;i<63;++i)bytes[i]=(n*17+i*29)&255;
    rx.append(bytes,n%64,n,12);assert(rx.size()<=2048);
  }
  c={Page::Control,88,0,false,false};
  t.screen(true,260,20,10000,c);
  assert(t.screen(false,260,20,10100,c)==Intent::Pair);
  t.screen(true,20,145,11000,c);
  assert(t.screen(false,20,145,11800,c)==Intent::None);
  std::puts("PASS: fragmented HID, partial-message timeout, connection epochs, oversized/NUL payload rejection, 10000 malformed frame inputs, explicit pairing");
  std::puts("PASS: freshness, parallel agents, reconnect, millis wrap, malformed state, rear-touch isolation, explicit approval, drag/epoch cancellation, HID golden frames");
}
