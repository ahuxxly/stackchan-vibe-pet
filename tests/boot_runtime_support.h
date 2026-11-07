// Hardware boundaries for the actual setup/loop/serialCommands/telemetry test.
// LCD/JPEG, sensors, persistence and codecs are fake. The USB methods, complete
// status constructor, main scheduling and IMU monitor are production code.
#pragma once
#include <ArduinoJson.h>
#include "boot_usb_model.h"
#include "SnapshotTx.h"
#include "CompanionCore.h"
#include "PetMotion.h"
#include "PetImu.h"
#include "PetGesture.h"
#include "ServoWarmup.h"
#if __has_include("PowerGateProbe.h")
#include "PowerGateProbe.h"
#endif
#include "MusePolicy.h"
#include "LocalAudio.h"
#include "FirmwareVersion.h"
#if __has_include("RuntimeHealth.h")
#include "RuntimeHealth.h"
#endif
#include "health_canvas_stub.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
using companion::Page;using companion::Intent;using String=std::string;
inline uint32_t appNow(){return usb_model::now;}
inline uint32_t esp_random(){return 12345;}
inline int esp_reset_reason(){return 1;}
constexpr int MALLOC_CAP_DMA=1;
inline int heap_caps_get_largest_free_block(int){return 122868;}
struct EspFake{unsigned getFreeHeap(){return 133732;}unsigned getMinFreeHeap(){return 129720;}
 unsigned getFreePsram(){return 6803175;}unsigned getPsramSize(){return 8386007;}} ESP;
struct FakeDisplay{int width(){return 320;}int height(){return 240;}void setBrightness(int){}
 uint16_t color565(int r,int g,int b){return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);}};
struct FakeImu{struct V{float x=0,y=0,z=0;};struct {V accel{0,0,1},gyro{};} data;
 int update(){return 3;}const auto& getImuData(){return data;}};
struct CodecFake{bool isRunning(){return false;}void end(){} };
struct ButtonFake{bool wasHold(){return false;}bool wasClicked(){return false;}};
struct M5Fake{FakeDisplay Display;FakeImu Imu;CodecFake Mic,Speaker;ButtonFake BtnPWR;
 int getBoard(){return 27;}} M5;
struct Preferences{bool begin(const char*,bool){return true;}int getInt(const char* key,int){return !strcmp(key,"zero_pos_1")?489:570;}void end(){}} preferences;
struct FilesystemFake{bool begin(bool format){assert(!format);return true;}} LittleFS;
struct AffectionState{int affection=481,mood=0,confusion=0;unsigned seq=81;};
struct AffectionApplyResult{AffectionState state;int delta=0;};
struct AffectionFake{AffectionState value;void begin(Preferences*){}const auto& state(){return value;}
 bool lastSaveOk(){return true;}bool loadOk(){return true;}bool pendingSave(){return false;}
 unsigned saveAttempts(){return 0;}unsigned savedAffectionBytes(){return 0;}unsigned savedSequenceBytes(){return 0;}
 int persistedAffection(){return 481;}unsigned persistedSeq(){return 81;}
 AffectionApplyResult applyEvent(const char*,int,int,void*,uint32_t){return {value,0};}} affection;
struct FaceFake{void requestFrame(){}void setFrameOverlay(void(*)(M5Canvas&)){}void begin(){}void setAffectionState(const AffectionState&){}
 void setGazeDirection(int){}bool petAnimationActive(){return false;}bool bufferedFrameReady(){return true;}
 unsigned presentedFrames(){return 0;}unsigned maxPresentMicros(){return 0;}
 struct Assets{int mode=0;};Assets faceAssetStatus(){return {};}} face;
inline const char* faceAssetModeName(int){return "classic";}
struct LumiFake{unsigned frames=0;void setSafetyTick(void(*)()){}bool begin(){usb_model::advance(12900);return true;}
 void requestFrame(){}void pauseForLegacy(bool){}bool ready(){return true;}
 int renderedState(){return frames?0:-1;}int renderedFrame(){return frames?int(frames%8):-1;}
 unsigned failures(){return 0;}unsigned loads(){return 8;}unsigned maxDecodeUs(){return 12000;}
 unsigned maxDrawUs(){return 50000;}unsigned maxReadUs(){return 10000;}unsigned misses(){return 1;}
 bool pending(){return false;}unsigned presented(){return frames;}unsigned maxPresentUs(){return 34000;}} lumi;
muse::Director lumiDirector;
bool lumiSkin=true,legacyReady=false,classicRenderMode=false;
uint8_t galleryClip=0;int8_t galleryFrame=-1;uint32_t galleryAt=0;
struct PresentationFake{template<class... Args>void update(Args&&...){}} presentation;
snapshot_tx::Writer statusWriter;
companion::TouchRouter input;
pet_motion::Controller movement;
pet_imu::Monitor imuGuard;
pet_gesture::Detector rearGesture;
pet_motion::ServoWarmup servoWarmup;
namespace codex_hid {
inline companion::State data;
inline bool begin(){return true;}inline void update(uint32_t){}
inline const companion::State& state(){return data;}inline void releaseInputs(){}
struct Info{bool advertising=false,connected=false,encrypted=false,subscribed=false,requested=false,failed=false;unsigned generation=0;};
inline Info connectionInfo(){return {};}
inline unsigned receivedMessages(){return 0;}inline unsigned receivedStatusMessages(){return 0;}
inline unsigned sentActions(){return 0;}inline unsigned invalidMessages(){return 0;}
}
namespace local_audio {
inline bool begin(){return true;}inline bool busy(){return false;}
inline Snapshot snapshot(){return {};}
// The actual production diagnostics getters are tested separately. No worker is
// started in this scheduling fixture; all codecs remain OFF.
#ifdef BOOT_HAS_DIAGNOSTICS
inline Diagnostics diagnostics(){return {false,false,false,Command::None};}
#endif
}
struct BoardFake{
 unsigned onCalls=0,uartCalls=0;
 void begin(){}void update(){usb_model::advance(1);}
 void setServoPowerEnabled(bool on){if(on){++onCalls;assert(false);}}
 int setServoPowerEnabledChecked(bool on){setServoPowerEnabled(on);return 1;}
 unsigned registerReads=0;bool registerOK=true;uint8_t registerValue=0;uint32_t registerCost=1;
 bool readServoPowerRegister(uint8_t,uint8_t* out){++registerReads;*out=registerValue;usb_model::advance(registerCost);return registerOK;}
 bool motionPrepared(){return false;}bool motionUartReady(){return false;}
 bool prepareMotion(){++uartCalls;assert(false);return false;}
 bool servoPowerConfigReady(){return true;}
 const char* servoPowerConfigStage(){return "ready_off";}
 const char* servoPowerConfigError(){return "none";}
 int servoPowerConfigRegister(){return 7;}
 int servoPowerConfigActual(){return 0;}
 int servoPowerGateLevel(){return 0;}
 int servoPowerDiagnosticField(unsigned){assert(false);return -1;}
 const char* servoUartInitStage(){assert(false);return "not_started";}
 int servoUartInitError(){assert(false);return 0;}
 int servoFeedbackRaw(int){return -32768;}int servoFeedbackError(int){return 0;}int servoFeedbackStatus(int){return 0;}
 void showRgbColor(int,int,int){}
 struct Pair{int x=0,y=450;};
 struct MotionFake{void stop(){}bool isMoving(){return false;}Pair getCurrentAngles(){assert(false);return {};}
 void moveYaw(int,int){assert(false);}void movePitch(int,int){assert(false);}} Motion;
} M5StackChan;
HWCDC Serial;
