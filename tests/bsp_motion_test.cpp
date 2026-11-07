// Pinned unmodified BSP Servo/Animate/Spring, with fake position feedback.
// Gravity and gyro are derived from the generated trajectory and enter the
// same IMU adapter as main. This excludes real vibration, current, UART lag,
// physical inertia and translational acceleration: physical acceptance remains.
#include "PetMotion.h"
#include "PetImu.h"
#include "utils/motion/servo.h"
#include <cstdio>
#include <cassert>
#include <cmath>
static uint32_t clockMs=0;
uint32_t millis(){return clockMs;}
class FakeServo : public stackchan::motion::Servo {
 public:
  explicit FakeServo(int home):physical_(home){set_angle_limit({-1800,1800});}
  bool feedbackValid=true;
  int getCurrentAngle() override{return feedbackValid?physical_:-32768;}
 private:
  void set_angle_impl(int angle) override{physical_=angle;}
  int physical_;
};
struct Imu {
  struct Vec {float x=0,y=0,z=0;};
  struct Data {Vec accel{0,0,1},gyro{};} data;
  unsigned update(){return 3;}
  const Data& getImuData(){return data;}
};
static bool run(pet_motion::Activity activity,uint32_t period,bool hazard) {
  using namespace pet_motion;
  clockMs=0;FakeServo yaw(0),pitch(450);yaw.init();pitch.init();
  Controller c;pet_imu::Monitor monitor;Imu imu;bool cut=false;
  auto stop=[&](pet_imu::Trip){cut=true;c.disable(clockMs,StopReason::Imu);};
  for(clockMs=0;clockMs<=600;clockMs+=40)monitor.poll(imu,clockMs,false,stop);
  clockMs=600;assert(monitor.arm(clockMs));assert(c.enable(0,450,clockMs,1));assert(c.request(activity,clockMs));
  unsigned count=0;int lastYaw=0,lastPitch=450;float peakGyro=0;bool injected=false;uint32_t injectedAt=0;
  for(clockMs=600;clockMs<=600+Controller::kBurstMs;clockMs+=5) {
    if(clockMs%period==0){yaw.update();pitch.update();}
    if(clockMs%40)continue;
    const int y=yaw.getCurrentAngle(),p=pitch.getCurrentAngle();
    const float angle=(p-450)*0.1f*3.14159265f/180.0f;
    imu.data.accel={std::sin(angle),0,std::cos(angle)};
    imu.data.gyro={(p-lastPitch)*2.5f,0,(y-lastYaw)*2.5f};lastYaw=y;lastPitch=p;
    const float gyro=std::hypot(imu.data.gyro.x,imu.data.gyro.z);if(gyro>peakGyro)peakGyro=gyro;
    if(hazard && !injected && gyro>0){injected=true;injectedAt=clockMs;}
    if(injected)imu.data.gyro.x=1000;
    monitor.poll(imu,clockMs,false,stop); // Cosmetic suppression cannot hide a hazard.
    if(cut) {
      assert(hazard && injected && monitor.lastTrip()==pet_imu::Trip::Strong && clockMs==injectedAt+40);
      assert(!c.enabled());
      std::printf("PASS: active %s external strong samples -> adapter cutoff at=%u period=%u (modeled IMU)\n",name(activity),clockMs,period);
      return true;
    }
    const bool busy=yaw.isMoving() || pitch.isMoving(),active=c.active();
    const auto out=c.tick(clockMs,busy,{});
    if(out.powerOff) {
      std::printf("FAIL: %s cutoff=%s elapsed=%u commands=%u period=%u\n",name(activity),reasonName(c.reason()),clockMs-600,count,period);
      return false;
    }
    if(out.move || (active && !c.active())) {
      assert(!busy);
      assert(out.move?c.posePermits(out,y,p):c.poseAtRestPermits(y,p));
    }
    if(out.move) {
      auto& servo=out.axis==Axis::Yaw?yaw:pitch;
      servo.moveWithSpeed(out.target,out.speed);c.commandSent(out);++count;
    }
    if(!c.active()) {
      assert(!hazard && yaw.getCurrentAngle()==0 && pitch.getCurrentAngle()==450);
      assert(monitor.armed() && monitor.trips()==0);
      std::printf("PASS: %s elapsed=%u commands=%u task_period_ms=%u peak_modeled_gyro=%.1f; fake physical readback\n",name(activity),clockMs-600,count,period,peakGyro);
      return true;
    }
  }
  return false;
}
int main() {
  uitk_intl::ui_hal::on_get_tick([]{return clockMs;});
  FakeServo stopped(450);stopped.init();stopped.moveWithSpeed(600,350);
  clockMs=100;stopped.update();stopped.cancelAnimation();const auto held=stopped.getCurrentAngle();
  for(clockMs=120;clockMs<1000;clockMs+=20)stopped.update();
  assert(stopped.getCurrentAngle()==held && !stopped.isMoving());
  stopped.feedbackValid=false;stopped.moveWithSpeed(700,350);assert(!stopped.isMoving());
  stopped.feedbackValid=true;stopped.moveWithSpeed(500,350);assert(stopped.isMoving());
  puts("PASS actual BSP cancel/interpolation: no queued movement after stop; invalid feedback refuses new target");
  bool passed=true;
  for(auto activity:{pet_motion::Activity::Look,pet_motion::Activity::Pet,pet_motion::Activity::Attention,pet_motion::Activity::Dance})
    for(uint32_t period:{20U,25U,30U}) {
      passed=run(activity,period,false)&&passed;
      passed=run(activity,period,true)&&passed;
    }
  return passed?0:1;
}
