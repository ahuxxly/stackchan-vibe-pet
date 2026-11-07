#pragma once
#include <ArduinoJson.h>
#include <array>
#include <cstdint>

// Offer one complete JSON line only when the USB queue reports enough space.
// HWCDC's short lock/progress timeout is not an overall write deadline if queue
// space changes concurrently; callers must also limit snapshots per loop.
namespace snapshot_tx {
class Writer {
 public:
  static constexpr size_t kCapacity=8192;
  template<class Stream> static void service(Stream& stream) {
    // The hash-pinned HWCDC patch also services pending TX with a zero-wait
    // lock and a 20 ms retry cadence, independently of connected or packet room.
    // This offers queued bytes to the ISR; it does NOT prove host receipt or
    // freshness. Keep service independent of complete JSON admission below.
    (void)stream.isConnected();
  }
  template<class Stream> bool send(const JsonDocument& doc,Stream& stream) {
    service(stream);
    const size_t required=measureJson(doc);
    if(required+1>buffer_.size()){++oversize_;return false;}
    if(stream.availableForWrite()<static_cast<int>(required+1)){++backpressure_;return false;}
    const size_t size=serializeJson(doc,buffer_.data(),buffer_.size());
    buffer_[size]='\n';
    if(stream.write(reinterpret_cast<const uint8_t*>(buffer_.data()),size+1)!=size+1){++partial_;return false;}
    ++sent_;return true;
  }
  uint32_t sent()const{return sent_;}
  uint32_t backpressure()const{return backpressure_;}
  uint32_t partial()const{return partial_;}
  uint32_t oversize()const{return oversize_;}
 private:
  std::array<char,kCapacity> buffer_{};
  uint32_t sent_=0,backpressure_=0,partial_=0,oversize_=0;
};
}
