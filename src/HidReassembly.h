#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace companion {
// Transport framing only: ArduinoJson validates the bounded completed payload.
class HidReassembly {
 public:
  bool append(const uint8_t* data,size_t count,uint32_t now,uint32_t epoch) {
    if(epoch!=epoch_ || (used_ && uint32_t(now-started_)>2000)) clear();
    epoch_=epoch;
    if(!data || count<2 || data[0]!=2 || data[1]>61 || data[1]>count-2 || !data[1]) {clear();return false;}
    const size_t n=data[1];
    if(n>2048-used_) {clear();return false;}
    // Wire JSON must not contain raw NULs; these could hide trailing data.
    if(std::memchr(data+2,0,n)) {clear();return false;}
    if(!used_)started_=now;
    std::memcpy(buffer_.data()+used_,data+2,n);used_+=n;buffer_[used_]=0;
    return true;
  }
  void clear(){used_=0;buffer_[0]=0;}
  const char* data()const{return buffer_.data();}
  size_t size()const{return used_;}
 private:
  std::array<char,2049> buffer_{};
  size_t used_=0;
  uint32_t started_=0,epoch_=0;
};
}
