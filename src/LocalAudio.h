#pragma once
#include "LocalAudioPolicy.h"
namespace local_audio {
// begin configures and allocates only: no codec, speaker, or mic is started.
bool begin();
void allowPage(bool enabled);
bool request(Command command);
void stop();
bool busy();
Snapshot snapshot();
// Diagnostic flags only; reading them never starts a codec. Individual atomics
// may straddle a worker transition, so the live busy() gate remains authoritative.
struct Diagnostics {bool allowed,cancelPending,working; Command pending;};
Diagnostics diagnostics();
} // namespace local_audio
