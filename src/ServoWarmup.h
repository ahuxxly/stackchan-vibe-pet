#pragma once
#include "PetMotion.h"
#include <cstdint>

// Power-up qualification only: never creates a position/motion command.
// Poll from the main loop so IMU, STOP and page/power controls remain live.
namespace pet_motion {
class ServoWarmup {
 public:
  enum class Poll { None, ReadPose, Expired };
  static constexpr uint32_t kSettleMs=600,kReadIntervalMs=100,kDeadlineMs=1500;
  bool start(uint32_t now) {
    if(pending_)return false;
    pending_=true;started_=now;sampled_=awaiting_=previousValid_=false;reads_=0;result_="no_samples";return true;
  }
  void cancel(){pending_=awaiting_=previousValid_=false;}
  bool pending() const {return pending_;}
  unsigned reads() const {return reads_;}
  const char* result() const {return result_;}
  Poll poll(uint32_t now) {
    if(!pending_)return Poll::None;
    if(uint32_t(now-started_)>=kDeadlineMs){cancel();return Poll::Expired;}
    if(uint32_t(now-started_)<kSettleMs || awaiting_ ||
       (sampled_ && uint32_t(now-lastRead_)<kReadIntervalMs))return Poll::None;
    sampled_=awaiting_=true;lastRead_=now;++reads_;return Poll::ReadPose;
  }
  bool accept(int yaw,int pitch,uint32_t now) {
    if(!pending_ || !awaiting_)return false;
    awaiting_=false;
    if(uint32_t(now-started_)>=kDeadlineMs){cancel();return false;}
    if(!Controller::validHome(yaw,pitch)){
      result_=(yaw==-32768 || pitch==-32768)?"invalid_feedback":"pose_range";
      previousValid_=false;return false;
    }
    const bool stable=previousValid_ && yaw-yaw_>=-10 && yaw-yaw_<=10 &&
                      pitch-pitch_>=-10 && pitch-pitch_<=10;
    result_=stable?"ready":previousValid_?"unstable":"waiting_pair";
    yaw_=yaw;pitch_=pitch;previousValid_=true;
    if(stable){pending_=false;return true;}
    return false;
  }
 private:
  bool pending_=false,sampled_=false,awaiting_=false,previousValid_=false;
  uint32_t started_=0,lastRead_=0;
  unsigned reads_=0;
  int yaw_=0,pitch_=0;
  const char* result_="not_started";
};
} // namespace pet_motion
