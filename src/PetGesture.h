#pragma once
#include <cstdint>

// Raw rear touch only. No host input or motor commands are possible here.
namespace pet_gesture {
inline bool contact(uint8_t a,uint8_t b,uint8_t c){return a>=1 || b>=1 || c>=1;}
struct Event {bool began=false,ended=false,longStroke=false;uint32_t durationMs=0,session=0;};
class Detector {
 public:
  static constexpr uint32_t kReadyMs=1000,kDebounceMs=140,kReleaseMs=180,kLongMs=3000;
  Event update(bool contact,bool screenOn,uint32_t now) {
    Event out;
    if(!screenOn){ready_=false;clear_=tracking_=active_=releasing_=false;return out;}
    if(!ready_) {
      if(contact)clear_=false;
      else if(!clear_){clear_=true;clearAt_=now;}
      else if(uint32_t(now-clearAt_)>=kReadyMs)ready_=true;
      return out;
    }
    if(contact) {
      if(!tracking_){tracking_=true;startedAt_=now;}
      releasing_=false;
      if(!active_ && uint32_t(now-startedAt_)>=kDebounceMs) {
        active_=true;++session_;out.began=true;out.session=session_;
      }
    } else if(tracking_) {
      if(!releasing_){releasing_=true;releasedAt_=now;}
      if(uint32_t(now-releasedAt_)>=kReleaseMs) {
        out.ended=active_;out.durationMs=uint32_t(releasedAt_-startedAt_);
        out.longStroke=out.durationMs>=kLongMs;out.session=session_;
        tracking_=active_=releasing_=false;
      }
    }
    return out;
  }
  bool ready()const{return ready_;}
  bool active()const{return active_;}
  uint32_t session()const{return session_;}
  uint32_t duration(uint32_t now)const{return tracking_?uint32_t((releasing_?releasedAt_:now)-startedAt_):0;}
 private:
  bool ready_=false,clear_=false,tracking_=false,active_=false,releasing_=false;
  uint32_t clearAt_=0,startedAt_=0,releasedAt_=0,session_=0;
};
}
