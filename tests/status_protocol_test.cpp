#include "StatusProtocol.h"
#include <cassert>
#include <cstdio>

unsigned report(companion::State& state,const char* json,uint32_t now) {
  JsonDocument document;
  assert(!deserializeJson(document,json));
  return companion::applyThreadStatus(state,document.as<JsonVariantConst>(),now);
}
bool poll(companion::State& state,const char* json,uint32_t now) {
  JsonDocument document;assert(!deserializeJson(document,json));
  return companion::applyHostPoll(state,document.as<JsonVariantConst>(),now);
}
int main() {
  companion::State state;
  state.connect(true);
  assert(report(state,R"([{"id":0,"c":16739584,"b":1,"e":4,"s":0.5}])",1)==0);
  assert(state.awaiting(0,2)); // Numeric integer intensity is accepted as float by the actual decoder.
  assert(report(state,R"([{"id":0,"b":0}])",10)==0);
  assert(!state.awaiting(0,11) && state.agents[0].color==0xff6d00);
  assert(report(state,R"([{"id":1,"c":16739584}])",20)==0);
  assert(!state.fresh(1,21));
  assert(report(state,R"([{"id":1,"b":1,"e":1}])",22)==0);
  assert(state.awaiting(1,23));
  const auto revision=state.agents[1].revision;
  assert(report(state,R"([{"id":1,"b":null},{"id":"1","e":1},{"id":1,"b":true},42])",30)==4);
  assert(state.agents[1].revision==revision && state.agents[1].receivedAt==22);
  assert(report(state,R"([{"id":1,"s":2},{"id":1,"c":-1},{"id":8,"b":1}])",40)==3);
  assert(report(state,R"({"id":1})",41)==1);
  assert(!state.fresh(1,22+companion::kLeaseMs));
  state.connect(false);state.connect(true);
  assert(report(state,R"([{"id":1,"b":1,"e":1}])",50000)==0);
  assert(!state.fresh(1,50001));
  companion::State idle;idle.connect(true);
  assert(poll(idle,R"({"method":"device.status","id":1})",0));
  for(int i=0;i<6;++i)assert(idle.receive(i,0xff6d00,1,1,.5,100));
  const auto revision0=idle.agents[0].revision;
  // Real host schedule, changed-only content. The producer may have stalled:
  // keep LAST KNOWN colors, never label these fresh or make approval eligible.
  for(uint32_t now=60000;now<=600000;now+=60000) {
    assert(poll(idle,R"({"m":"device.status","i":2,"p":{}})",now));
    for(int i=0;i<6;++i) {
      assert(idle.cached(i,now+45001) && idle.displayKnown(i,now+45001));
      assert(!idle.fresh(i,now) && !idle.awaiting(i,now) && !idle.approvalEligible(i,now));
      assert(idle.agents[i].receivedAt==100);
    }
  }
  assert(idle.agents[0].revision==revision0); // Heartbeats don't fabricate content.
  const auto count=idle.hostPollCount;
  for(const char* bad:{R"({"m":"device.status"})",R"({"m":"device.status","i":"2"})",
      R"({"m":"device.status","i":true})",R"({"m":"device.status","i":-1})",
      R"({"m":"device.status","i":2,"p":[1]})",R"({"m":"device.status","i":2,"p":{"x":1}})",
      R"({"m":"device.status","i":2,"result":{}})",R"({"m":"device.status","i":2,"error":null})",
      R"({"m":"v.oai.app","i":2})",R"({"m":"sys.version","i":2})",R"([])"})
    assert(!poll(idle,bad,650000));
  assert(idle.hostPollCount==count && idle.hostPollAt==600000);
  assert(idle.cached(0,689999) && !idle.displayKnown(0,690000)); // BLE can stay connected.
  assert(poll(idle,R"({"m":"device.status","i":3})",700000));
  assert(idle.hostLive(700001) && !idle.displayKnown(0,700001)); // No resurrection after gap.
  assert(report(idle,R"([{"id":0,"b":1,"e":1}])",700010)==0);
  assert(!idle.displayKnown(0,700011)); // Incomplete new content cannot borrow old color.
  assert(report(idle,R"([{"id":0,"c":16739584,"b":1,"e":1}])",700020)==0);
  assert(idle.approvalEligible(0,700021) && !idle.displayKnown(1,700021));
  idle.connect(false);idle.connect(true);
  assert(!idle.hostLive(700030) && !idle.displayKnown(0,700030));
  assert(poll(idle,R"({"method":"device.status","id":4})",700040));
  assert(!idle.displayKnown(0,700041));
  // Timer wrap, late / absent initial poll, and heartbeat-only connection.
  companion::State wrap;wrap.connect(true);wrap.receiveHostPoll(0xfffffff0U);
  wrap.receive(0,0xffffff,1,1,0,0xfffffff0U);
  assert(wrap.cached(0,60000));assert(!wrap.displayKnown(0,90000));
  companion::State late;late.connect(true);late.receive(0,0xffffff,1,1,0,0);
  assert(!late.displayKnown(0,45000));late.receiveHostPoll(60000);
  assert(!late.displayKnown(0,60001));
  std::puts("PASS: heartbeat only gives labelled cache; producer failure never fresh/approval; 60s idle polls, 90s stream loss, no stale revival, malformed RPC, six-slot and reconnect isolation");
  std::puts("PASS: real ArduinoJson decoder, integer/float fields, optional-field merge, null/bool/string rejection, malformed entries and reconnect isolation");
}
