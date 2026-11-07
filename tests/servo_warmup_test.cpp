#include "ServoWarmup.h"
#include "PetImu.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using pet_motion::ServoWarmup;
struct Vector {float x=0,y=0,z=0;};
struct Imu {
  struct Sample {Vector accel{0,0,1},gyro{};} data;
  unsigned update(){return 3;}
  const Sample& getImuData(){return data;}
};
int main() {
  for(uint32_t start:{0U,0xfffffe00U}) {
    ServoWarmup w;
    assert(w.poll(start)==ServoWarmup::Poll::None);
    assert(w.start(start) && !w.start(start+500)); // repeated start cannot extend deadline
    assert(w.poll(start+180)==ServoWarmup::Poll::None); // observed failing power-on interval
    assert(w.poll(start+599)==ServoWarmup::Poll::None);
    assert(w.poll(start+600)==ServoWarmup::Poll::ReadPose);
    assert(!w.accept(-1280,0,start+600)); // actual captured BSP failure/clamp values
    assert(w.poll(start+699)==ServoWarmup::Poll::None);
    assert(w.poll(start+700)==ServoWarmup::Poll::ReadPose);
    assert(!w.accept(0,400,start+700)); // one apparently valid read is insufficient
    assert(w.poll(start+800)==ServoWarmup::Poll::ReadPose);
    assert(w.accept(3,403,start+800) && !w.pending() && w.reads()==3);
    assert(!w.accept(3,403,start+800)); // completed sample cannot enable twice

    ServoWarmup never;assert(never.start(start));
    for(uint32_t elapsed=600;elapsed<1500;elapsed+=100) {
      assert(never.poll(start+elapsed)==ServoWarmup::Poll::ReadPose);
      assert(!never.accept(-1280,0,start+elapsed));
    }
    assert(never.reads()==9 && never.poll(start+1500)==ServoWarmup::Poll::Expired);
    assert(!never.pending() && never.poll(start+1600)==ServoWarmup::Poll::None);

    ServoWarmup unstable;unstable.start(start);
    for(uint32_t elapsed=600;elapsed<1500;elapsed+=100) {
      unstable.poll(start+elapsed);
      assert(!unstable.accept(elapsed%200?30:0,400,start+elapsed));
    }
    assert(unstable.poll(start+1500)==ServoWarmup::Poll::Expired);
    ServoWarmup cancelled;cancelled.start(start);cancelled.poll(start+600);
    cancelled.cancel();assert(!cancelled.accept(0,400,start+600));
    assert(cancelled.poll(start+800)==ServoWarmup::Poll::None);
    assert(cancelled.start(start+1000)); // explicit new operation starts from zero samples
    cancelled.poll(start+1600);assert(!cancelled.accept(0,400,start+1600));
    cancelled.poll(start+1700);assert(cancelled.accept(0,400,start+1700));

    ServoWarmup delayed;delayed.start(start);delayed.poll(start+600);
    assert(!delayed.accept(0,400,start+1500) && !delayed.pending());
    ServoWarmup broken;broken.start(start);broken.poll(start+600);
    assert(!broken.accept(0,400,start+600));broken.poll(start+700);
    assert(!broken.accept(0,0,start+700));broken.poll(start+800);
    assert(!broken.accept(0,400,start+800)); // invalid sample breaks the stable pair
  }

  // Main-loop integration: raw IMU polling continues during warm-up; no motor
  // target exists before qualification, and an IMU trip cancels pending startup.
  for(int scenario=0;scenario<3;++scenario) {
    Imu imu;pet_imu::Monitor guard;ServoWarmup warmup;pet_motion::Controller motion;
    bool power=false;unsigned cut=0;
    auto off=[&](pet_imu::Trip){++cut;power=false;warmup.cancel();motion.disable(0,pet_motion::StopReason::Imu);};
    for(uint32_t t=0;t<=520;t+=40)guard.poll(imu,t,false,off);
    assert(guard.arm(520));assert(warmup.start(520));power=true;
    for(uint32_t elapsed=40;elapsed<=1000;elapsed+=40) {
      const uint32_t now=520+elapsed;
      if(scenario==1 && elapsed>=80)imu.data.gyro.x=300;
      if(scenario==2 && elapsed<280)continue; // missed-loop stale-data trip
      guard.poll(imu,now,false,off);
      const auto request=warmup.poll(now);
      if(request==ServoWarmup::Poll::ReadPose) {
        assert(elapsed>=600 && power);
        if(warmup.accept(0,400,now))assert(motion.enable(0,400,now,1));
      }
      if(elapsed<700 || scenario!=0)assert(!motion.enabled());
      assert(!motion.tick(now,false,{}).move); // no movement during qualification/idle window
    }
    assert(power==(scenario==0));assert(cut==(scenario==0?0U:1U));
    assert(motion.enabled()==(scenario==0));assert(!warmup.pending());
  }
  std::puts("PASS: production servo warm-up; 600 ms minimum, bounded 1500 ms deadline, two valid stable poses, captured invalid readings, cancellation/no replay, wrap, and continuous IMU trip dispatch; fake transport only");
}
