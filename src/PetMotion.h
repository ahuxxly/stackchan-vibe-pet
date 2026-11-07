#pragma once
// Hardware-independent, bounded choreography. Angles are BSP tenths of a degree;
// speed is its 0..1000 spring parameter, NOT degrees/second. No HID dependency.
#include <cstdint>

namespace pet_motion {
enum class Axis { Yaw, Pitch };
enum class Activity { None, Look, Pet, Attention, Dance };
enum class StopReason { User, Page, Screen, Microphone, Shake, Session, Timeout, Pose, Imu };
struct Guard {
  bool petPage=true, screenOn=true, microphone=false, shaking=false;
  bool petting=false, awaiting=false, touching=false;
};
struct Output { bool move=false, powerOff=false; Axis axis=Axis::Yaw; int target=0, speed=0; };
struct Step { Axis axis; int offset, speed; uint32_t holdMs; int8_t gaze; };
inline const char* name(Activity activity) {
  switch(activity) {
    case Activity::Look:return "LOOK"; case Activity::Pet:return "PET";
    case Activity::Attention:return "ATTENTION"; case Activity::Dance:return "DANCE";
    default:return "IDLE";
  }
}
inline const char* reasonName(StopReason reason) {
  switch(reason) {
    case StopReason::Page:return "page"; case StopReason::Screen:return "screen";
    case StopReason::Microphone:return "microphone"; case StopReason::Shake:return "shake";
    case StopReason::Session:return "session"; case StopReason::Timeout:return "timeout";
    case StopReason::Pose:return "pose";case StopReason::Imu:return "imu";default:return "user";
  }
}
class Controller {
 public:
  static constexpr uint32_t kSessionMs=600000, kBurstMs=12000, kStepTimeoutMs=3000;
  static constexpr int kYawLimit=1000, kPitchMin=100, kPitchMax=800;
  // Reserve space for the complete normal profile; never silently clip a step.
  static bool validHome(int yaw,int pitch) {return yaw>=-650 && yaw<=650 && pitch>=200 && pitch<=650;}
  bool enable(int yaw,int pitch,uint32_t now,uint32_t seed) {
    if(!validHome(yaw,pitch)) {disable(now,StopReason::Pose);return false;}
    homeYaw_=yaw;homePitch_=pitch;enabled_=true;enabledAt_=now;
    expectedYaw_=yaw;expectedPitch_=pitch;
    activity_=Activity::None;gaze_=-1;inFlight_=false;hadPet_=false;
    random_=seed?seed:1;lastRest_=now;idleDelay_=nextIdleDelay();return true;
  }
  void disable(uint32_t now,StopReason reason=StopReason::User) {
    enabled_=false;activity_=Activity::None;inFlight_=false;gaze_=-1;
    lastRest_=now;reason_=reason;
  }
  bool enabled() const {return enabled_;}
  bool active() const {return activity_!=Activity::None;}
  Activity activity() const {return activity_;}
  StopReason reason() const {return reason_;}
  int8_t gaze() const {return gaze_;}
  uint32_t remainingMs(uint32_t now) const {
    const uint32_t age=now-enabledAt_;
    return enabled_ && age<kSessionMs ? kSessionMs-age : 0;
  }
  bool poseAtRestPermits(int yaw,int pitch) const {
    if(!enabled_ || yaw < -kYawLimit || yaw > kYawLimit ||
       pitch < kPitchMin || pitch > kPitchMax)return false;
    // Compare to the last dispatched target, not an expanded envelope around
    // home. A stalled/displaced servo must not authorize the following step.
    return yaw-expectedYaw_>=-50 && yaw-expectedYaw_<=50 &&
           pitch-expectedPitch_>=-30 && pitch-expectedPitch_<=30;
  }
  void commandSent(const Output& out) {
    if(!out.move)return;
    if(out.axis==Axis::Yaw)expectedYaw_=out.target;else expectedPitch_=out.target;
  }
  bool posePermits(const Output& out,int yaw,int pitch) const {
    if(!out.move || !poseAtRestPermits(yaw,pitch) || out.speed<1 || out.speed>350)return false;
    if(out.axis==Axis::Yaw)return out.target>=-kYawLimit && out.target<=kYawLimit &&
      out.target-yaw>=-550 && out.target-yaw<=550;
    return out.target>=kPitchMin && out.target<=kPitchMax &&
      out.target-pitch>=-180 && out.target-pitch<=180;
  }
  bool request(Activity wanted,uint32_t now) {
    if(!enabled_ || wanted==Activity::None || uint32_t(now-enabledAt_)>=kSessionMs) return false;
    if(wanted==Activity::Pet) {
      if(hadPet_ && uint32_t(now-lastPet_)<4000) return false;
      lastPet_=now;hadPet_=true;
    } else if(activity_==Activity::Pet || activity_==Activity::Attention) return false;
    if(wanted==Activity::Look && active())return false;
    // A manual dance cannot repeatedly extend an existing dance or interrupt petting.
    if(wanted==Activity::Dance && activity_==Activity::Dance)return false;
    activity_=wanted;step_=0;inFlight_=false;startedAt_=now;stepAt_=now;gaze_=-1;
    lookSign_=(nextRandom()&1)?1:-1;
    return true;
  }
  Output tick(uint32_t now,bool motorBusy,const Guard& guard) {
    if(!enabled_)return {};
    if(!guard.screenOn)return cut(now,StopReason::Screen);
    if(!guard.petPage)return cut(now,StopReason::Page);
    if(guard.microphone)return cut(now,StopReason::Microphone);
    if(guard.shaking)return cut(now,StopReason::Shake);
    if(uint32_t(now-enabledAt_)>=kSessionMs)return cut(now,StopReason::Session);
    if(!active()) {
      if(!guard.petting && !guard.awaiting && !guard.touching &&
         uint32_t(now-lastRest_)>=idleDelay_)request(Activity::Look,now);
      else return {};
    }
    if(uint32_t(now-startedAt_)>=kBurstMs)return cut(now,StopReason::Timeout);
    if(motorBusy && uint32_t(now-stepAt_)>=kStepTimeoutMs)return cut(now,StopReason::Timeout);
    if(inFlight_) {
      if(motorBusy || uint32_t(now-stepAt_)<currentStep().holdMs)return {};
      inFlight_=false;++step_;
      if(step_>=count()) {
        activity_=Activity::None;gaze_=-1;lastRest_=now;idleDelay_=nextIdleDelay();return {};
      }
    }
    if(motorBusy)return {}; // Never command a second axis while either is moving.
    const auto step=currentStep();
    int target=(step.axis==Axis::Yaw?homeYaw_:homePitch_)+step.offset;
    if((step.axis==Axis::Yaw && (target < -kYawLimit || target > kYawLimit)) ||
       (step.axis==Axis::Pitch && (target < kPitchMin || target > kPitchMax)))return cut(now,StopReason::Pose);
    inFlight_=true;stepAt_=now;gaze_=step.gaze;
    return {true,false,step.axis,target,step.speed};
  }
 private:
  bool enabled_=false,inFlight_=false,hadPet_=false;
  Activity activity_=Activity::None;
  StopReason reason_=StopReason::User;
  int homeYaw_=0,homePitch_=400,expectedYaw_=0,expectedPitch_=400,lookSign_=1;
  uint8_t step_=0;int8_t gaze_=-1;
  uint32_t enabledAt_=0,startedAt_=0,stepAt_=0,lastRest_=0,idleDelay_=15000,lastPet_=0,random_=1;
  uint32_t nextRandom(){random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;return random_;}
  uint32_t nextIdleDelay(){return 15000+nextRandom()%15001;}
  Output cut(uint32_t now,StopReason reason){disable(now,reason);Output out;out.powerOff=true;return out;}
  uint8_t count() const {return activity_==Activity::Dance?4:activity_==Activity::Look?3:5;}
  Step currentStep() const {
    // Reuse upstream gaze directions (0 right, 8 left, 4 down, 12 up, 16 center).
    // Only one target axis per step, all sequences explicitly end at the captured home.
    // Upstream pet-fw: look +/-25 degrees, small nod maps to +15 physical
    // degrees. Speed 350 is checked with the pinned BSP spring (not deg/s).
    static constexpr Step look[]={{Axis::Yaw,-250,350,1000,0},{Axis::Yaw,250,350,1000,8},{Axis::Yaw,0,350,900,16}};
    static constexpr Step nod[]={{Axis::Yaw,0,350,800,-1},{Axis::Pitch,150,350,800,-1},{Axis::Pitch,0,350,800,-1},
                                 {Axis::Pitch,100,350,800,-1},{Axis::Pitch,0,350,800,-1}};
    // Four sequential steps leave settling time inside the unchanged 12 s cap.
    // A deliberate 30 degree turn / 15 degree bow, below upstream look-away
    // (35 degrees yaw) and inside our unchanged absolute mechanical limits.
    static constexpr Step dance[]={{Axis::Yaw,-300,350,650,0},{Axis::Pitch,150,350,650,4},
      {Axis::Yaw,0,350,800,16},{Axis::Pitch,0,350,800,16}};
    if(activity_==Activity::Dance) {
      auto value=dance[step_];
      if(value.axis==Axis::Yaw && value.offset) {
        value.offset*=lookSign_;if(lookSign_<0)value.gaze=8;
      }
      return value;
    }
    if(activity_==Activity::Look) {
      auto value=look[step_];value.offset*=lookSign_;
      if(lookSign_<0 && value.gaze!=16)value.gaze=value.gaze==0?8:0;
      return value;
    }
    return nod[step_];
  }
};
} // namespace pet_motion
