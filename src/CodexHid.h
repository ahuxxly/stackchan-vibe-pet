#pragma once
#include "CompanionCore.h"
#include <cstdint>

namespace codex_hid {
bool begin();
void update(uint32_t now);
bool advertise(); // Explicit physical UI action starts pairing; no auto-pair at boot.
bool advertising();
struct ConnectionInfo {
  bool available, advertising, connected, encrypted, subscribed, requested, failed;
  uint32_t generation;
};
ConnectionInfo connectionInfo();
const companion::State& state();
bool tapAgent(int index);
bool tapAction(int action);
bool microphone(bool pressed);
void releaseInputs();
uint32_t receivedMessages();
uint32_t receivedStatusMessages();
uint32_t sentActions();
uint32_t invalidMessages();
}
