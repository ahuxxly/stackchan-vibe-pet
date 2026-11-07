#include "PetImu.h"
#include "PetMotion.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>

struct Vector {float x=0,y=0,z=0;};
struct Sample {Vector accel{0,0,1},gyro{};};
struct FakeImu {
  Sample data;unsigned mask=3,updates=0,reads=0;
  unsigned update(){++updates;return mask;}
  const Sample& getImuData(){++reads;return data;}
};
static unsigned samples=0,stops=0;
struct Rig {
  FakeImu imu;pet_imu::Monitor monitor;pet_motion::Controller motion;
  bool powered=false;unsigned stopCalls=0,commands=0;
  pet_imu::Result poll(uint32_t now,bool cosmetic=false) {
    ++samples;
    return monitor.poll(imu,now,cosmetic,[&](pet_imu::Trip) {
      // Same dispatch as main: detector -> stop callback -> motion disabled/power off.
      ++stopCalls;++stops;powered=false;motion.disable(now,pet_motion::StopReason::Imu);
    });
  }
  uint32_t warm(uint32_t start=0) {
    assert(!monitor.ready(start) && !monitor.arm(start));
    for(uint32_t elapsed=0;elapsed<=520;elapsed+=40)poll(start+elapsed);
    assert(monitor.ready(start+520));return start+520;
  }
  void enable(uint32_t now,pet_motion::Activity activity=pet_motion::Activity::Dance) {
    assert(monitor.arm(now));assert(motion.enable(0,400,now,1));powered=true;
    assert(motion.request(activity,now));assert(motion.tick(now,false,{}).move);++commands;
  }
  void assertStopped(uint32_t now,pet_imu::Trip reason) {
    assert(stopCalls==1 && !powered && !monitor.armed() && !motion.enabled());
    assert(monitor.lastTrip()==reason && motion.reason()==pet_motion::StopReason::Imu);
    // No direct shaking flag is injected at any point in this test.
    for(uint32_t elapsed=0;elapsed<=13000;elapsed+=40)
      assert(!motion.tick(now+elapsed,false,{}).move);
    assert(!monitor.arm(now)); // Requires a new steady interval, never automatic re-arm.
  }
};
int main() {
  for(uint32_t start:{0U,0xfffffe00U}) {
    for(auto activity:{pet_motion::Activity::Look,pet_motion::Activity::Pet,
                       pet_motion::Activity::Attention,pet_motion::Activity::Dance}) {
      for(int hazard=0;hazard<4;++hazard) {
        Rig r;const uint32_t now=r.warm(start);r.enable(now,activity);
        pet_imu::Trip expected;
        if(hazard==0){r.imu.data.gyro.x=300;expected=pet_imu::Trip::Strong;}
        else if(hazard==1){r.imu.data.accel.z=2.3f;expected=pet_imu::Trip::Strong;}
        else if(hazard==2){r.imu.data.accel.z=0.1f;expected=pet_imu::Trip::LowGravity;}
        else {r.imu.data.accel={0,0.8660254f,0.5f};expected=pet_imu::Trip::Tilt;}
        assert(r.poll(now+40).trip==pet_imu::Trip::None && r.powered);
        assert(r.poll(now+80).trip==expected);r.assertStopped(now+80,expected);
        r.poll(now+120);assert(r.stopCalls==1); // no duplicate stop callback
      }
    }
  }
  // Synthetic benign samples exercise self-motion filtering without masking safety.
  // They are not measured BSP/servo traces and do not prove a false-positive rate.
  Rig benign;auto now=benign.warm();benign.enable(now);
  for(unsigned n=1;n<=100;++n) {
    benign.imu.data.accel={0,0.12f,0.99f};benign.imu.data.gyro={30,0,0};
    assert(benign.poll(now+n*40).trip==pet_imu::Trip::None);
  }
  assert(benign.powered && benign.stopCalls==0);
  benign.imu.data.gyro.x=300;benign.poll(now+4040);
  benign.imu.data={};benign.poll(now+4080);assert(benign.powered); // one spike resets
  benign.imu.data.gyro.x=300;benign.poll(now+4120);
  assert(benign.poll(now+4280).trip==pet_imu::Trip::Continuity);
  benign.assertStopped(now+4280,pet_imu::Trip::Continuity);

  // Relative gravity works irrespective of installed axis; magnitude remains 1 g.
  Rig axis;axis.imu.data.accel={1,0,0};now=axis.warm();axis.enable(now);
  axis.imu.data.accel={0.5f,0,0.8660254f};axis.poll(now+40);
  assert(axis.poll(now+80).trip==pet_imu::Trip::Tilt);axis.assertStopped(now+80,pet_imu::Trip::Tilt);

  for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
    Rig r;now=r.warm();r.enable(now);r.imu.data.gyro.x=invalid;
    assert(r.poll(now+40).trip==pet_imu::Trip::Invalid);r.assertStopped(now+40,pet_imu::Trip::Invalid);
  }
  for(unsigned mask:{0U,1U,2U}) {
    Rig r;now=r.warm(0xfffffe00U);r.enable(now);const auto reads=r.imu.reads;r.imu.mask=mask;
    r.poll(now+40);assert(r.powered && r.imu.reads==reads);
    assert(r.poll(now+200).trip==pet_imu::Trip::Continuity);r.assertStopped(now+200,pet_imu::Trip::Continuity);
  }
  Rig late;now=late.warm();late.enable(now);
  // Even a new valid sample cannot hide an already missed safety deadline.
  assert(late.poll(now+251).trip==pet_imu::Trip::Stale);late.assertStopped(now+251,pet_imu::Trip::Stale);
  Rig settling;now=settling.warm();settling.enable(now);
  assert(settling.poll(now+180).trip==pet_imu::Trip::Continuity);
  settling.assertStopped(now+180,pet_imu::Trip::Continuity);

  // Actual poll adapter, including wrap: every formerly blind interval fails closed.
  for(uint32_t start:{0U,0xfffffe00U})for(uint32_t gap:{119U,120U,121U,154U,249U,250U})
    for(int hazard=0;hazard<3;++hazard) {
      Rig r;now=r.warm(start);r.enable(now);
      if(hazard==0)r.imu.data.gyro.x=300;
      if(hazard==1)r.imu.data.accel.z=.1f;
      if(hazard==2)r.imu.data.accel={0,.8660254f,.5f};
      const auto first=r.poll(now+gap);
      auto expected=gap>=250?pet_imu::Trip::Stale:gap>120?pet_imu::Trip::Continuity:
        hazard==0?pet_imu::Trip::Strong:hazard==1?pet_imu::Trip::LowGravity:pet_imu::Trip::Tilt;
      if(gap<=120)assert(first.trip==pet_imu::Trip::None && r.poll(now+gap*2).trip==expected);
      else assert(first.trip==expected);
      r.assertStopped(now+gap*2,expected);
      r.imu.data={};for(unsigned i=1;i<=15;++i)r.poll(now+gap*2+i*40);
      assert(r.monitor.ready(now+gap*2+600) && !r.monitor.armed());
    }

  Rig unsteady;
  unsteady.imu.data.gyro.x=25;
  for(uint32_t t=0;t<=800;t+=40)unsteady.poll(t);
  assert(!unsteady.monitor.ready(800) && !unsteady.monitor.arm(800));
  unsteady.imu.data={};
  for(uint32_t t=840;t<=1360;t+=40)unsteady.poll(t);
  assert(unsteady.monitor.ready(1360));
  unsteady.monitor.arm(1360);unsteady.monitor.disarm();
  assert(!unsteady.monitor.ready(1360));

  Rig face;now=face.warm();face.imu.data.gyro.z=300;
  for(unsigned n=1;n<=20;++n)assert(!face.poll(now+n*40,false).dizzy);
  assert(!face.poll(now+840,true).dizzy);assert(!face.poll(now+880,true).dizzy);
  assert(face.poll(now+920,true).dizzy && face.stopCalls==0); // raw stream to cosmetic event
  face.poll(now+960,true);face.poll(now+1000,false);
  assert(!face.poll(now+1040,true).dizzy && !face.poll(now+1080,true).dizzy);
  assert(face.poll(now+1120,true).dizzy); // suppressed samples never accumulate/replay
  std::printf("PASS: production IMU adapter/detector/stop dispatch; %u sample polls, %u trips; active dance/pet/look/attention, masked cosmetics, relative tilt, low gravity, noise, invalid/missing data, stale/wrap and re-arm guards; synthetic samples only\n",samples,stops);
}
