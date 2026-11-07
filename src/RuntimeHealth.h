#pragma once
#include <cstdint>
#include <cstdio>

// Written only by the foreground loop. No timers, worker callbacks, hardware,
// allocation, USB, host status or input dispatch can advance this evidence.
namespace runtime_health {
constexpr int kWidth=320,kHeight=8,kY=232;
constexpr uint32_t kPublishMs=1000;
enum class Boot:uint8_t {Safe,Audio,UsbInit,Storage,Assets,Canvas,Hid,UsbLog,UsbStatus,Loop};
inline const char* name(Boot step) {
  switch(step) {
    case Boot::Safe:return "SAFE";case Boot::Audio:return "AUDIO";
    case Boot::UsbInit:return "USB INIT";case Boot::Storage:return "STORAGE";
    case Boot::Assets:return "ASSETS";case Boot::Canvas:return "CANVAS";
    case Boot::Hid:return "HID";case Boot::UsbLog:return "USB LOG";
    case Boot::UsbStatus:return "USB STATE";case Boot::Loop:return "LOOP";
  }
  return "?";
}
class Monitor {
 public:
  // Call ONLY after all work of this loop has returned, including telemetry.
  void complete(uint32_t now,uint32_t loop,uint32_t samples,uint32_t gap,
                uint32_t gaps,bool ready) {
    completed_=loop;
    if(published_ && uint32_t(now-publishedAt_)<kPublishMs)return;
    published_=true;publishedAt_=now;changed_=true;shown_=loop;ready_=ready;
    snprintf(text_,sizeof(text_),"L%06lu I%06lu/%lu G%lu %s",
      static_cast<unsigned long>(loop%1000000),static_cast<unsigned long>(samples%1000000),
      static_cast<unsigned long>(gap),static_cast<unsigned long>(gaps),ready?"R":"-");
  }
  bool takeChanged(bool visible) {
    if(!visible || !changed_)return false;
    changed_=false;return true;
  }
  const char* text()const{return text_;}
  uint32_t completed()const{return completed_;}
  uint32_t shown()const{return shown_;}
  bool ready()const{return ready_;}
 private:
  char text_[56]="L------ I------/--- G- -";
  uint32_t completed_=0,shown_=0,publishedAt_=0;
  bool published_=false,changed_=false,ready_=false;
};
}
