// Executes the production input policy in host memory. No USB/BLE is opened.
// These checks prove policy invariants, not physical touch or host-side actions.
#include "CompanionCore.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace companion;
static unsigned checks = 0, gridPoints = 0, gestures = 0, frames = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
  std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
  std::exit(1); } } while (false)

static Intent gesture(int x, int y, uint32_t duration, const Context& context,
                      uint32_t start = 1000) {
  TouchRouter router;
  ++gestures;
  CHECK(router.screen(true, x, y, start, context) == Intent::None);
  // Holding a button never emits an action, even after the threshold.
  CHECK(router.screen(true, x, y, start + duration, context) == Intent::None);
  const Intent result = router.screen(false, x, y, start + duration, context);
  // A release is consumed once; polling the released finger cannot repeat it.
  CHECK(router.screen(false, x, y, start + duration + 1, context) == Intent::None);
  return result;
}

static Context waitingContext(const State& state, uint32_t now) {
  return {Page::Control, 17 + state.generation + state.agents[0].revision,
          0, state.connected, state.awaiting(0, now)};
}

int main() {
  // Safety boundaries over every physical screen pixel, including button gaps.
  for (bool connected : {false, true}) for (bool waiting : {false, true}) {
    const Context pet{Page::Pet, 1, 0, connected, waiting};
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) {
      const auto result = hit(x, y, pet);
      CHECK(result == Intent::None || result == Intent::OpenControl || result == Intent::Motion || result == Intent::Stop);
      ++gridPoints;
    }
  }
  for (bool waiting : {false, true}) {
    const Context disconnected{Page::Control, 2, 0, false, waiting};
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) {
      const auto result = hit(x, y, disconnected);
      CHECK(result == Intent::None || result == Intent::CloseControl || result == Intent::Pair || result == Intent::OpenGallery || result == Intent::Stop || result == Intent::OpenAudio);
      ++gridPoints;
    }
  }
  const Context ready{Page::Control, 3, 0, true, false};
  for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) {
    const auto result = hit(x, y, ready);
    CHECK(result != Intent::Approve && result != Intent::Reject);
    ++gridPoints;
  }
  const Context waiting{Page::Control, 3, 0, true, true};
  for (auto page : {Page::Pet, Page::Control}) {
    Context context = waiting;
    context.page = page;
    for (int x : {-1, 320}) for (int y = -1; y <= 240; ++y)
      CHECK(hit(x, y, context) == Intent::None);
    for (int y : {-1, 240}) for (int x = -1; x <= 320; ++x)
      CHECK(hit(x, y, context) == Intent::None);
  }

  struct Button { int x, y; Intent intent; uint32_t threshold; };
  const std::array<Button, 14> buttons{{
    {50, 65, Intent::Agent0, 40}, {154, 65, Intent::Agent1, 40},
    {258, 65, Intent::Agent2, 40}, {50, 105, Intent::Agent3, 40},
    {154, 105, Intent::Agent4, 40}, {258, 105, Intent::Agent5, 40},
    {50, 150, Intent::Approve, 600}, {154, 150, Intent::Reject, 600},
    {258, 150, Intent::Send, 40}, {40, 210, Intent::Fast, 40},
    {120, 210, Intent::Fork, 40}, {200, 210, Intent::Mic, 40},
    {280, 210, Intent::OpenGallery, 40}, {30, 20, Intent::CloseControl, 40}
  }};
  for (const auto& button : buttons) {
    for (uint32_t duration : {0U, 39U, 40U, 599U, 600U, 999U, 1000U, 9999U, 10000U}) {
      const auto expected = duration >= button.threshold && duration < 10000
                              ? button.intent : Intent::None;
      CHECK(gesture(button.x, button.y, duration, waiting) == expected);
    }
    CHECK(gesture(button.x, button.y, button.threshold, waiting, 0xfffffff0U) == button.intent);
  }
  CHECK(gesture(280, 20, 40, {Page::Pet, 1, 0, true, true}) == Intent::OpenControl);
  CHECK(gesture(260, 20, 40, {Page::Control, 1, 0, false, false}) == Intent::Pair);
  CHECK(gesture(260, 20, 1000, waiting) == Intent::None);

  // New local controls work offline and cannot ever become host approvals.
  Context local{Page::Pet,4,0,false,false,true};
  CHECK(gesture(40,20,999,local)==Intent::None);
  CHECK(gesture(40,20,1000,local)==Intent::Motion);
  CHECK(gesture(120,20,599,local)==Intent::None);
  CHECK(gesture(120,20,600,local)==Intent::Dance);
  // Captured real misses near the former lower edge now hit visible 44px buttons.
  CHECK(gesture(114,37,886,local)==Intent::Dance);
  CHECK(gesture(38,42,1000,local)==Intent::Motion);
  CHECK(gesture(120,47,600,local)==Intent::Dance);
  CHECK(gesture(120,48,600,local)==Intent::None);
  // Passive long-press feedback must agree with actual release routing.
  TouchRouter feedback;
  CHECK(feedback.screen(true,120,20,100,local)==Intent::None);
  CHECK(!std::strcmp(feedback.feedback(699,local).phase,"holding"));
  CHECK(!std::strcmp(feedback.feedback(700,local).phase,"release"));
  CHECK(feedback.screen(true,120,20,800,local)==Intent::None);
  CHECK(feedback.screen(false,120,20,900,local)==Intent::Dance);
  CHECK(feedback.feedback(900,local).intent==Intent::None);
  CHECK(feedback.screen(true,120,20,1000,local)==Intent::None);
  CHECK(feedback.screen(true,120,50,1200,local)==Intent::None);
  CHECK(!std::strcmp(feedback.feedback(1700,local).phase,"cancelled"));
  CHECK(feedback.screen(true,120,20,1800,local)==Intent::None);
  CHECK(feedback.screen(false,120,20,1900,local)==Intent::None);
  CHECK(feedback.screen(true,120,20,2000,local)==Intent::None);
  auto changed=local;++changed.generation;
  CHECK(!std::strcmp(feedback.feedback(2700,changed).phase,"context_changed"));
  CHECK(feedback.screen(false,120,20,2700,changed)==Intent::None);
  CHECK(feedback.screen(true,120,20,3000,local)==Intent::None);
  CHECK(!std::strcmp(feedback.feedback(13000,local).phase,"too_long"));
  CHECK(feedback.screen(false,120,20,13000,local)==Intent::None);
  local.motionEnabled=false;
  CHECK(gesture(120,20,1000,local)==Intent::None);
  local.motionEnabled=true;local.awaiting=true;
  CHECK(gesture(120,20,1000,local)==Intent::None);
  for(bool connected:{false,true})for(bool awaiting:{false,true})for(bool motion:{false,true}) {
    Context context{Page::Pet,5,0,connected,awaiting,motion};
    for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
      const auto result=hit(x,y,context);
      CHECK(result==Intent::None || result==Intent::OpenControl || result==Intent::Motion || result==Intent::Dance || result==Intent::Stop);
      ++gridPoints;
    }
    for(auto page:{Page::Pet,Page::Control}) {
      context.page=page;const int x=page==Page::Pet?200:120;
      TouchRouter stop;
      CHECK(stop.screen(true,x,20,100,context)==Intent::Stop);
      CHECK(stop.screen(true,x,20,101,context)==Intent::None);
      CHECK(stop.screen(false,x,20,102,context)==Intent::None);
    }
  }
  TouchRouter cancelledDance;
  local.awaiting=false;
  CHECK(cancelledDance.screen(true,120,20,100,local)==Intent::None);
  local.motionEnabled=false;
  CHECK(cancelledDance.screen(false,120,20,800,local)==Intent::None);
  // Waking the display while a finger is already held must require a fresh touch.
  for(const auto& button:{buttons[6],buttons[7],buttons[12]}) {
    TouchRouter wake;wake.requireRelease();
    CHECK(wake.screen(true,button.x,button.y,100,waiting)==Intent::None);
    CHECK(wake.screen(true,button.x,button.y,2000,waiting)==Intent::None);
    CHECK(wake.screen(false,button.x,button.y,2100,waiting)==Intent::None);
    CHECK(wake.screen(true,button.x,button.y,2200,waiting)==Intent::None);
    CHECK(wake.screen(false,button.x,button.y,3400,waiting)==button.intent);
  }

  // Both approval directions must cancel when their context is lost mid-hold.
  for (const auto& button : {buttons[6], buttons[7]}) {
    for (int scenario = 0; scenario < 8; ++scenario) {
      TouchRouter router;
      Context changed = waiting;
      CHECK(router.screen(true, button.x, button.y, 100, waiting) == Intent::None);
      switch (scenario) {
        case 0: changed.connected = false; break;
        case 1: changed.awaiting = false; break;
        case 2: ++changed.generation; break;
        case 3: ++changed.selected; break;
        case 4: changed.page = Page::Pet; break;
        case 5: router.cancel(); break;
        case 6:
          CHECK(router.screen(true, 0, 239, 300, waiting) == Intent::None);
          CHECK(router.screen(true, button.x, button.y, 400, waiting) == Intent::None);
          break;
        case 7:
          CHECK(router.screen(true, button.x, button.y, 300, ready) == Intent::None);
          break;
      }
      CHECK(router.screen(false, button.x, button.y, 900, changed) == Intent::None);
    }
    TouchRouter lateWaiting;
    CHECK(lateWaiting.screen(true, button.x, button.y, 100, ready) == Intent::None);
    CHECK(lateWaiting.screen(true, button.x, button.y, 200, waiting) == Intent::None);
    CHECK(lateWaiting.screen(false, button.x, button.y, 900, waiting) == Intent::None);

    TouchRouter cancelled;
    CHECK(cancelled.screen(true, button.x, button.y, 100, waiting) == Intent::None);
    cancelled.cancel();
    CHECK(cancelled.screen(true, button.x, button.y, 900, waiting) == Intent::None);
    CHECK(cancelled.screen(false, button.x, button.y, 1000, waiting) == Intent::None);
    CHECK(cancelled.screen(true, button.x, button.y, 1100, waiting) == Intent::None);
    CHECK(cancelled.screen(false, button.x, button.y, 1800, waiting) == button.intent);
  }

  // Use the actual state lease/revision logic with a held approval finger.
  for (int scenario = 0; scenario < 4; ++scenario) {
    State state;
    state.connect(true);
    CHECK(state.receive(0, 0xff6d00, 1, 4, .4f, 100));
    TouchRouter router;
    const uint32_t start = scenario == 0 ? 44500 : 1000;
    CHECK(router.screen(true, 50, 150, start, waitingContext(state, start)) == Intent::None);
    if (scenario == 1) CHECK(state.receive(0, 0xff6d00, 1, 4, .4f, 1300));
    if (scenario == 2) CHECK(state.receive(0, 0xff6d00, 1, 5, .4f, 1300));
    if (scenario == 3) {
      state.connect(false);
      state.connect(true);
      CHECK(state.receive(0, 0xff6d00, 1, 4, .4f, 1300));
    }
    const auto result = router.screen(false, 50, 150, start + 700,
                                      waitingContext(state, start + 700));
    CHECK(result == (scenario == 1 ? Intent::Approve : Intent::None));
  }

  // Six independent slots: fresh/expired/unknown/malformed cannot cross-refresh.
  State parallel;
  parallel.connect(true);
  for (int slot = 0; slot < 6; ++slot)
    CHECK(parallel.receive(slot, 0xff6d00, 1, 4, 1, 100 + 100 * slot));
  for (int expiredThrough = 0; expiredThrough < 6; ++expiredThrough) {
    const uint32_t now = 100 + 100 * expiredThrough + kLeaseMs;
    for (int slot = 0; slot < 6; ++slot) {
      CHECK(parallel.fresh(slot, now) == (slot > expiredThrough));
      CHECK(parallel.awaiting(slot, now) == (slot > expiredThrough));
    }
  }
  parallel.connect(false);
  parallel.connect(true);
  for (int slot = 0; slot < 6; ++slot) {
    CHECK(!parallel.fresh(slot, 100000));
    CHECK(!parallel.awaiting(slot, 100000));
  }

  // All command encodings are tested locally, including key release and padding.
  for (int index = 0; index < 13; ++index) {
    char key[6];
    if (index < 6) std::snprintf(key, sizeof(key), "AG%02d", index);
    else std::snprintf(key, sizeof(key), "ACT%02d", index);
    for (bool pressed : {false, true}) {
      uint8_t bytes[63];
      CHECK(keyFrame(key, pressed, bytes));
      const std::string expected = std::string("{\"m\":\"v.oai.hid\",\"p\":{\"k\":\"") +
        key + "\",\"act\":" + (pressed ? "1" : "0") + "}}\r\n";
      CHECK(bytes[0] == 2 && bytes[1] == expected.size());
      CHECK(std::memcmp(bytes + 2, expected.data(), expected.size()) == 0);
      for (size_t pad = 2 + expected.size(); pad < sizeof(bytes); ++pad) CHECK(bytes[pad] == 0);
      ++frames;
    }
  }
  std::printf("PASS: %u checks; %u screen pixels across 15 contexts; %u duration gestures; %u local HID frames\n",
              checks, gridPoints, gestures, frames);
  std::puts("PASS: release-only actions, both approval guards, drag/cancel/context/lease transitions, six independent slots, millis wrap; NO physical or host action claimed");
}
