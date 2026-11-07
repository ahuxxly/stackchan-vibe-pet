// Test-only hardware boundary. main.cpp's functions are compiled unchanged
// except replacing its wall clock with appNow(); LocalAudio uses a real thread.
#include "CompanionCore.h"
#include "PetMotion.h"
#include "PetImu.h"
#include "PetGesture.h"
#include "ServoWarmup.h"
#if __has_include("PowerGateProbe.h")
#include "PowerGateProbe.h"
#endif
#include "LocalAudio.h"
#include "RuntimeHealth.h"
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>
using companion::Page;using companion::Intent;using String=std::string;
inline uint32_t appClock=10000;
uint32_t appNow(){return appClock;}
uint32_t esp_random(){return 12345;}
struct SerialStub{template<class... A>void printf(const char*,A...){}void println(const char*){}} Serial;
struct M5Canvas{explicit M5Canvas(FakeDisplay*){}};
namespace muse{constexpr int kStates=18;}
struct AffectionState{int affection=481;};
struct AffectionApplyResult{AffectionState state;int delta=0;};
struct AffectionStub{AffectionState value;const AffectionState& state(){return value;}bool lastSaveOk(){return true;}
 AffectionApplyResult applyEvent(const char*,int,int,void*,uint32_t){return {value,0};}} affection;
struct FaceStub{void setEnabled(bool){}void setGazeDirection(int){}void setAffectionState(const AffectionState&){}
 void showAffectionDelta(int,uint32_t){}bool petAnimationActive(){return false;}} face;
struct LumiStub{void requestFrame(){}} lumi;
bool lumiSkin=true,legacyReady=false;
uint8_t galleryClip=0;int8_t galleryFrame=-1;uint32_t galleryAt=0;
companion::TouchRouter input;
pet_motion::Controller movement;
pet_imu::Monitor imuGuard;
pet_gesture::Detector rearGesture;
pet_motion::ServoWarmup servoWarmup;
namespace codex_hid {
companion::State data;std::vector<int> actions;int releases=0,advertised=0;
const companion::State& state(){return data;}
void releaseInputs(){++releases;}
bool tapAction(int n){actions.push_back(n);return true;}
bool tapAgent(int n){actions.push_back(n);return true;}
bool microphone(bool on){actions.push_back(on?10:11);return true;}
void advertise(){++advertised;}
}
struct BoardStub {
 struct Pair{int x=0,y=450;};
 struct MotionStub{
   Pair pose;int commands=0,cancelled=0;
   bool isMoving(){return false;}
   Pair getCurrentAngles(){return pose;}
   void moveYaw(int n,int speed){assert(owner==0&&speed<=350);pose.x=n;++commands;}
   void movePitch(int n,int speed){assert(owner==0&&speed<=350);pose.y=n;++commands;}
   void stop(){++cancelled;}
 } Motion;
 bool powered=false,prepared=false,uartOK=true,taskOK=true,gateOK=true,gateReadOK=true;
 int onCalls=0,diagnosticReads=0,failDiagnosticField=-1;
 void setServoPowerEnabled(bool on){if(on){assert(owner==0);++onCalls;}powered=on;}
 bool writeOnOK=true,writeOffOK=true,powerConfigReady=true;unsigned writes=0;
 bool servoPowerConfigReady(){return powerConfigReady;}
 int setServoPowerEnabledChecked(bool on){++writes;if(!(on?writeOnOK:writeOffOK))return 0;setServoPowerEnabled(on);return 1;}
 bool readServoPowerRegister(uint8_t reg,uint8_t* out){++diagnosticReads;*out=reg==7?servoPowerGateLevel():0;return gateReadOK;}
 int servoPowerGateLevel(){return gateReadOK?(powered&&gateOK?1:0):-1;}
 int servoPowerDiagnosticField(unsigned field){++diagnosticReads;return int(field)==failDiagnosticField?-1:field==4?1:0;}
 const char* servoUartInitStage(){return uartOK?"ready":"install";}
 int servoUartInitError(){return uartOK?0:259;}
 int servoFeedbackRaw(int axis){return axis==0?489:714;}
 int servoFeedbackError(int){return 0;}
 int servoFeedbackStatus(int){return 0;}
 bool prepareMotion(){prepared=uartOK&&taskOK;return prepared;}
 bool motionPrepared(){return prepared;}
 bool motionUartReady(){return uartOK;}
 void showRgbColor(int,int,int){}
} M5StackChan;
inline unsigned waitIndex=0;
template<class Predicate>void audioWait(Predicate p,unsigned timeout=8500){
 ++waitIndex;
 const auto start=millis();while(!p()){
  if(millis()-start>=timeout){
   auto a=local_audio::snapshot();auto d=local_audio::diagnostics();
   fprintf(stderr,"wait %u timeout phase=%s error=%s samples=%u mic=%d spk=%d pending=%d allowed=%d cancel=%d working=%d\n",
    waitIndex,local_audio::name(a.phase),local_audio::name(a.error),a.samples,
    micCalls.load(),speakerCalls.load(),int(d.pending),d.allowed,d.cancelPending,d.working);
   std::exit(2);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
}
