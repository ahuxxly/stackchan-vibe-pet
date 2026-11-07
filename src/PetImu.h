#pragma once
#include <cmath>
#include <cstdint>

// M5Unified accel is in g and gyro in degrees/second. Cosmetic shake suppression
// must never suppress the independent motor interlock. No HID/hardware output here.
namespace pet_imu {
enum class Trip { None, Strong, LowGravity, Tilt, Invalid, Stale, Continuity };
inline const char* name(Trip trip) {
  switch(trip) {
    case Trip::Strong:return "strong";case Trip::LowGravity:return "low_gravity";
    case Trip::Tilt:return "tilt";case Trip::Invalid:return "invalid";
    case Trip::Stale:return "stale";case Trip::Continuity:return "sampling_gap";default:return "none";
  }
}
struct Result {Trip trip=Trip::None;bool dizzy=false;};
class Monitor {
 public:
  static constexpr uint32_t kPollMs=40,kContinuousMs=120,kStaleMs=250,kQuietMs=500;
  bool ready(uint32_t now) const {
    return !armed_ && valid_ && quiet_ && uint32_t(now-lastValid_)<=kContinuousMs &&
           uint32_t(now-quietSince_)>=kQuietMs;
  }
  bool arm(uint32_t now) {
    if(!ready(now))return false;
    homeX_=lastX_/lastNorm_;homeY_=lastY_/lastNorm_;homeZ_=lastZ_/lastNorm_;
    armed_=true;strong_=low_=tilt_=gesture_=0;lastTrip_=Trip::None;return true;
  }
  void disarm() {
    if(armed_)quiet_=false;
    armed_=false;strong_=low_=tilt_=gesture_=0;
  }
  bool armed() const {return armed_;}
  Trip lastTrip() const {return lastTrip_;}
  uint32_t trips() const {return trips_;}
  uint32_t samples() const {return sampleCount_;}
  uint32_t maxGapMs() const {return maxGapMs_;}
  uint32_t lastGapMs() const {return lastGapMs_;}
  uint32_t gapCount() const {return gapCount_;}
  void resetTimingStats(){maxGapMs_=lastGapMs_=gapCount_=0;}

  // This exact adapter and dispatch are used by main and the sample-stream tests.
  // Both accel and gyro must be fresh (M5Unified masks 1 and 2 respectively).
  template<class Imu,class Stop> Result poll(Imu& imu,uint32_t now,bool cosmeticAllowed,Stop stop) {
    Result result;
    if(armed_ && (!valid_ || uint32_t(now-lastValid_)>=kStaleMs))result=cut(Trip::Stale);
    else if(armed_ && uint32_t(now-lastValid_)>kContinuousMs)result=cut(Trip::Continuity);
    else if(!polled_ || uint32_t(now-lastPoll_)>=kPollMs) {
      polled_=true;lastPoll_=now;
      if((static_cast<unsigned>(imu.update())&3U)==3U) {
        const auto& data=imu.getImuData();
        result=sample(now,data.accel.x,data.accel.y,data.accel.z,
                      data.gyro.x,data.gyro.y,data.gyro.z,cosmeticAllowed);
      } else if(!valid_ || uint32_t(now-lastValid_)>kContinuousMs) {
        quiet_=false;strong_=low_=tilt_=gesture_=0;
      }
    }
    if(result.trip!=Trip::None)stop(result.trip);
    return result;
  }
 private:
  bool armed_=false,valid_=false,quiet_=false,polled_=false;
  uint8_t strong_=0,low_=0,tilt_=0,gesture_=0;
  uint32_t lastValid_=0,lastPoll_=0,quietSince_=0,trips_=0;
  uint32_t sampleCount_=0,maxGapMs_=0,lastGapMs_=0,gapCount_=0;
  float lastX_=0,lastY_=0,lastZ_=1,lastNorm_=1,homeX_=0,homeY_=0,homeZ_=1;
  Trip lastTrip_=Trip::None;
  static uint8_t count(uint8_t value,bool active){return active?(value<3?value+1:3):0;}
  Result cut(Trip reason) {
    disarm();quiet_=false;lastTrip_=reason;++trips_;return {reason,false};
  }
  Result sample(uint32_t now,float ax,float ay,float az,float gx,float gy,float gz,bool cosmetic) {
    const float acc=std::sqrt(ax*ax+ay*ay+az*az),gyro=std::sqrt(gx*gx+gy*gy+gz*gz);
    if(!std::isfinite(acc) || !std::isfinite(gyro)) {
      valid_=false;quiet_=false;strong_=low_=tilt_=gesture_=0;
      return armed_?cut(Trip::Invalid):Result{};
    }
    if(valid_) {
      lastGapMs_=uint32_t(now-lastValid_);
      if(lastGapMs_>maxGapMs_)maxGapMs_=lastGapMs_;
      if(lastGapMs_>kContinuousMs)++gapCount_;
    }
    ++sampleCount_;
    if(!valid_ || uint32_t(now-lastValid_)>kContinuousMs) {
      quiet_=false;strong_=low_=tilt_=gesture_=0;
    }
    valid_=true;lastValid_=now;lastX_=ax;lastY_=ay;lastZ_=az;lastNorm_=acc;
    const bool calm=std::fabs(acc-1.0f)<=0.15f && gyro<=20.0f;
    if(!calm)quiet_=false;
    else if(!quiet_){quiet_=true;quietSince_=now;}
    const bool strong=std::fabs(acc-1.0f)>1.1f || gyro>260.0f;
    if(armed_) {
      strong_=count(strong_,strong);
      low_=count(low_,acc<0.45f);
      // Relative gravity needs no installed-axis assumption. The 30-degree
      // limit is unchanged for the normal profile. Its own pitch delta is 15
      // degrees; modeled normal motion must pass this same detector.
      // Dynamic acceleration may also trip it: stopping is preferable to masking.
      const bool tilted=acc>=0.65f && acc<=1.35f &&
        (ax*homeX_+ay*homeY_+az*homeZ_)/acc<0.8660254f;
      tilt_=count(tilt_,tilted);
      if(strong_>=2)return cut(Trip::Strong);
      if(low_>=2)return cut(Trip::LowGravity);
      if(tilt_>=2)return cut(Trip::Tilt);
    }
    // Only the emotional reaction has a motion/petting/cooldown mask.
    gesture_=count(gesture_,cosmetic && strong);
    if(gesture_>=3){gesture_=0;return {Trip::None,true};}
    return {};
  }
};
} // namespace pet_imu
