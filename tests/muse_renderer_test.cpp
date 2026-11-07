// Production async renderer and real file CRCs; JPEG/LCD and RTOS are host fakes.
#include "MuseRenderer.h"
#include "PetImu.h"
#include <LittleFS.h>
#include <cassert>
#include <cstdio>
static unsigned overlays=0;
static void overlay(M5Canvas&){++overlays;}
static unsigned checkpoints=0;static void checkpoint(){++checkpoints;}
static void frame(MuseRenderer& r,int s,int f) {
 const uint32_t start=micros();
 do {r.draw(static_cast<muse::Clip>(s),f,overlay);delay(1);
     assert(uint32_t(micros()-start)<3000000);
 }while(r.renderedState()!=s || r.renderedFrame()!=f);
}
struct V {float x=0,y=0,z=0;};
struct Imu {struct {V accel{0,0,1},gyro{};} data;unsigned update(){return 3;}const auto& getImuData(){return data;}};
int main() {
 {
  readDelayMs=2;MuseRenderer r;assert(r.begin());readDelayMs=0;r.setSafetyTick(checkpoint);frame(r,0,0);
  for(int f=0;f<8;++f)frame(r,0,f);
  const auto loads=r.loads(),opens=LittleFS.opens;
  for(int n=0;n<10;++n)for(int f=0;f<8;++f)frame(r,0,f);
  assert(r.loads()==loads && loads==8 && LittleFS.opens==opens);
  const auto presents=r.presented();for(int n=0;n<100;++n)r.draw(muse::Clip::Idle,7,overlay);
  assert(r.presented()==presents);r.requestFrame();r.draw(muse::Clip::Idle,7,overlay);assert(r.presented()==presents+1);
  // Block the actual production worker for 500ms; foreground renders and safety still run.
  decodeDelayMs=500;r.draw(muse::Clip::Blink,0,overlay);
  for(int n=0;n<100 && !decoding;++n)delay(1);assert(decoding);
  Imu imu;pet_imu::Monitor guard;unsigned stops=0;auto stop=[&](pet_imu::Trip){++stops;};
  for(unsigned t=0;t<=520;t+=40)guard.poll(imu,t,false,stop);
  assert(guard.arm(520));imu.data.gyro.x=300;
  for(unsigned t=560;t<=600;t+=40) {
    const auto at=micros();r.requestFrame();r.draw(muse::Clip::Blink,0,overlay);
    assert(uint32_t(micros()-at)<50000 && r.renderedState()==0);
    guard.poll(imu,t,false,stop);
  }
  assert(stops==1 && !guard.armed() && decoding);
  // LCD submission itself is synchronous. An abnormally slow transfer must trip
  // on the very next poll; this test does NOT claim an interruptible LCD driver.
  Imu lcdImu;pet_imu::Monitor lcdGuard;unsigned lcdStops=0;
  auto lcdStop=[&](pet_imu::Trip trip){assert(trip==pet_imu::Trip::Continuity);++lcdStops;};
  for(unsigned t=0;t<=520;t+=40)lcdGuard.poll(lcdImu,t,false,lcdStop);
  assert(lcdGuard.arm(520));presentDelayMs=150;r.requestFrame();r.draw(muse::Clip::Blink,0,overlay);presentDelayMs=0;
  lcdGuard.poll(lcdImu,670,false,lcdStop);assert(lcdStops==1 && !lcdGuard.armed());
  decodeDelayMs=0;frame(r,1,0);
  // Cold transitions and every real release resource, including rapid requested changes.
  for(int s=0;s<18;++s)for(int f=0;f<8;++f)frame(r,s,f);
  r.draw(muse::Clip::Idle,3,overlay);r.draw(muse::Clip::Blocked,2,overlay);frame(r,17,7);
  assert(r.failures()==0 && LittleFS.opens==opens);
  assert(checkpoints==r.presented());
  r.pauseForLegacy(true);const auto pausedLoads=r.loads();r.draw(muse::Clip::Idle,1,overlay);delay(10);
  assert(r.loads()==pausedLoads && !decoding);r.pauseForLegacy(false);frame(r,0,1);
 }
 missingFile="/lumi/idle_2.jpg";corruptFile="/lumi/idle_3.jpg";
 {
  MuseRenderer r;assert(r.begin());assert(r.failures()==2);frame(r,0,0);
  const auto opens=LittleFS.opens,presents=r.presented();
  for(int n=0;n<100;++n){r.draw(muse::Clip::Idle,2,overlay);r.draw(muse::Clip::Idle,3,overlay);}
  assert(r.renderedFrame()==0 && r.presented()==presents && LittleFS.opens==opens);
  // A 1Hz diagnostic refresh must still compose over the retained complete
  // frame when the requested resource is bad, without another file read.
  const auto overlaysBefore=overlays;r.requestFrame();r.draw(muse::Clip::Idle,2,overlay);
  assert(overlays==overlaysBefore+1 && r.presented()==presents+1 && r.renderedFrame()==0 && LittleFS.opens==opens);
  failJpeg=true;r.draw(muse::Clip::Dance,0,overlay);
  for(int n=0;n<300 && r.failures()<3;++n)delay(1);
  assert(r.failures()>=3);failJpeg=false;
 }
 missingFile="";corruptFile="";failAllocation=true;
 {MuseRenderer r;assert(!r.begin());r.draw(muse::Clip::Idle,0,overlay);assert(r.presented()==0);}
 failAllocation=false;failTask=true;{MuseRenderer r;assert(!r.begin());}failTask=false;
 puts("PASS production async renderer: 144 CRC resources, eight-frame cache, 500ms decoder isolation, foreground IMU stop, cold/rapid switches, retained complete frame, changed-only commit, missing/corrupt/decoder/allocation/task failures (fake JPEG/LCD/RTOS)");
}
