#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <M5StackChan.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include "FaceController.h"
#include "AffectionController.h"
#include "CodexHid.h"
#include "CompanionCore.h"
#include "PetMotion.h"
#include "PetPresentation.h"
#include "PetImu.h"
#include "PetGesture.h"
#include "SnapshotTx.h"
#include "ServoWarmup.h"
#include "MuseRenderer.h"
#include "LocalAudio.h"
#include "AudioPresentation.h"
#include "FirmwareVersion.h"
#include "RuntimeHealth.h"
#include "PowerGateProbe.h"

namespace {
using companion::Intent;
using companion::Page;
FaceController face;
MuseRenderer lumi;
muse::Director lumiDirector;
bool lumiSkin=true,legacyReady=false,classicRenderMode=false;
uint8_t galleryClip=0;
int8_t galleryFrame=-1;
uint32_t galleryAt=0;
AffectionController affection;
Preferences preferences;
snapshot_tx::Writer statusWriter;
companion::TouchRouter input;
pet_motion::Controller movement;
pet_presentation::Coordinator presentation;
pet_imu::Monitor imuGuard;
pet_gesture::Detector rearGesture;
pet_motion::ServoWarmup servoWarmup;
static_assert(m5::IMU_Class::sensor_mask_accel==1 && m5::IMU_Class::sensor_mask_gyro==2,
              "IMU adapter requires M5Unified accel/gyro freshness masks");
Page page=Page::Pet;
power_probe::WriteHistory powerWrites;
power_probe::OffSnapshot powerSnapshot;
bool powerCutFault=false,powerSnapshotAccepted=false;
uint32_t powerSnapshotRequests=0;
struct PowerRegisterReader {
  bool readRegister(uint8_t reg,uint8_t* value,size_t bytes) {
    return bytes==1 && M5StackChan.readServoPowerRegister(reg,value);
  }
};
M5Canvas controls(&M5.Display);
M5Canvas bootHealth(&M5.Display);
runtime_health::Monitor runtimeHealth;
bool bootHealthAvailable=false;
uint32_t maxHealthComposeUs=0;
uint32_t pageGeneration=0,lastRender=0,lastTelemetry=0;
uint32_t shakeUntil=0,lastShake=0;
uint32_t micStarted=0,lastMotionTick=0,motionCommands=0;
uint32_t motionEnableAttempts=0,motionPoseAt=0;
int motionPoseYaw=0,motionPosePitch=0;
bool motionPoseCaptured=false;
uint32_t lastHostCue=0;
uint32_t pairRequests=0,screenPresses=0,screenReleases=0;
uint32_t danceRequests=0,danceNoticeUntil=0;
const char* danceResult="none";
const char* danceNotice="";
companion::TouchRouter::Feedback touchFeedback,lastTouchFeedback;
Intent lastTouchAction=Intent::None;
int pressX=0,pressY=0,pressMinX=0,pressMaxX=0,pressMinY=0,pressMaxY=0;
bool screenWasDown=false;
bool previousWaiting=false;
bool touchReady=false,petting=false,shaking=false,screenOn=true,micOn=false;
bool servoPowered=false,lastStrokeLong=false,canvasReady=false,fsReady=false,hidReady=false;
bool discardSerialLine=false;
bool localAudioReady=false;
const char* audioNotice="Hold REC, release; wait for SPEAK NOW";
uint8_t rearRaw[3]={};
uint32_t rewardDuration=0,rewardSession=0,rewardCooldown=0,rewardSeq=0,feedbackUntil=0;
int rewardBefore=0,rewardAfter=0,rewardDelta=0;
const char* rewardReason="none";
char petFeedback[48]="";
const char* motionNotice="";uint32_t motionNoticeUntil=0,disabledDancePresses=0;
const char* motionGateReason="off";
const char* motionFaultLabel="";
const char* motionFaultDetail="";
int motionStartPowerLevel=-1;
// Per-attempt, read-only evidence. GPIO configuration is sampled one register
// per warm-up tick; the USB diagnostic command never polls the hardware.
std::array<int,6> motionPowerConfig{{-1,-1,-1,-1,-1,-1}};
std::array<uint32_t,6> motionPowerAt{};
unsigned motionPowerNext=6,motionDiagReads=0;
uint32_t motionAttemptAt=0;
bool motionUartSampled=false;
const char* motionUartStage="not_sampled";
int motionUartError=0;
std::array<int,2> motionRaw{{-32768,-32768}},motionReplyError{{-1,-1}},motionReplyStatus{{-1,-1}};
uint32_t stopExecutions=0,stopExecutedAt=0;
uint32_t previousLoop=0,maxLoopGap=0,loopCount=0;
uint32_t maxBoardUs=0,maxInputsUs=0,maxRenderUs=0,maxSerialUs=0,maxTelemetryUs=0;
int storedYawZero=-1,storedPitchZero=-1;
bool storedServoNamespace=false;
int selected=0,lastX=0,lastY=0;
String serialLine;
struct PetHudSnapshot {
  std::array<uint32_t,6> colors{};
  int affection=0;
  uint8_t status=0; // unknown, linked, needs you
  bool moving=false,armed=false,imuReady=false,starting=false;
  const char* danceLabel="HOLD DANCE";
  uint8_t danceProgress=0;
  const char* moveLabel="HOLD MOVE";
  uint8_t moveProgress=0;
  std::array<char,48> feedback{};
  pet_motion::Activity activity=pet_motion::Activity::None;
  bool operator==(const PetHudSnapshot& other) const {
    return colors==other.colors && affection==other.affection && status==other.status &&
           moving==other.moving && armed==other.armed && imuReady==other.imuReady &&
           starting==other.starting && activity==other.activity &&
           danceLabel==other.danceLabel && danceProgress==other.danceProgress &&
           moveLabel==other.moveLabel && moveProgress==other.moveProgress && feedback==other.feedback;
  }
};
PetHudSnapshot petHud;
bool petHudValid=false;

void applyAffection(const AffectionApplyResult& result,uint32_t now) {
  if (page==Page::Pet && screenOn) {
    if(legacyReady && !lumiSkin) {
      face.setAffectionState(result.state);
      if(result.delta) face.showAffectionDelta(result.delta,now);
    }
  }
}
bool selectedFresh(uint32_t now) {return codex_hid::state().fresh(selected,now);}
bool selectedWaiting(uint32_t now) {return codex_hid::state().awaiting(selected,now);}
bool selectedApproval(uint32_t now) {return codex_hid::state().approvalEligible(selected,now);}
uint32_t hostColor(int id,uint32_t now) {
  const auto& state=codex_hid::state();
  if(!state.displayKnown(id,now)) return 0x40444f;
  const auto& a=state.agents[id];
  return a.effect && a.brightness>.02f ? a.color : 0x20242b;
}
uint16_t rgb565(uint32_t rgb) {return M5.Display.color565((rgb>>16)&255,(rgb>>8)&255,rgb&255);}
int requestServoPower(bool enabled) {
  powerSnapshot.cancel(); // Any write invalidates a pending OFF-only snapshot.
  const uint32_t started=millis();
  const int result=M5StackChan.setServoPowerEnabledChecked(enabled);
  powerWrites.record(enabled,result,started,millis(),motionEnableAttempts);
  if(!enabled)powerCutFault=result!=1;
  return result;
}
bool powerOffContext() {
  return !powerCutFault && !servoPowered && !movement.enabled() && !servoWarmup.pending() &&
    !imuGuard.armed() && !micOn && !local_audio::busy() &&
    local_audio::snapshot().phase==local_audio::Phase::Off &&
    !M5.Speaker.isRunning() && !M5.Mic.isRunning();
}
void updatePowerDiagnostics() {
  PowerRegisterReader reader;
  powerSnapshot.tick(reader,[]{return millis();},powerOffContext(),motionEnableAttempts);
}
void motionOff(pet_motion::StopReason reason=pet_motion::StopReason::User) {
  servoWarmup.cancel();
  // Request rail cutoff first; retain a failed write as unconfirmed physical OFF.
  const bool wasPowered=servoPowered;
  requestServoPower(false);servoPowered=false;
  if(wasPowered) {
    if(M5StackChan.motionPrepared())M5StackChan.Motion.stop();
    ++pageGeneration;
  }
  imuGuard.disarm();
  motionGateReason=pet_motion::reasonName(reason);
  movement.disable(millis(),reason);if(legacyReady)face.setGazeDirection(-1);petHudValid=false;
}
void failMotion(const char* reason,const char* label,const char* detail) {
  motionOff(pet_motion::StopReason::Pose);
  motionGateReason=reason;motionFaultLabel=label;motionFaultDetail=detail;
  motionNoticeUntil=0; // Retain the actual fault until a deliberate new attempt.
}
void failWarmup() {
  const char* reason=servoWarmup.result();
  failMotion(reason,"SERVO ERR",!strcmp(reason,"invalid_feedback")?"No servo feedback. Motion is OFF.":
    !strcmp(reason,"pose_range")?"Pose outside safe start range. Motion OFF.":
    !strcmp(reason,"unstable")?"Pose not stable. Motion is OFF.":"Pose check timed out. Motion is OFF.");
}
void failImu(pet_imu::Trip reason) {
  motionOff(pet_motion::StopReason::Imu);
  motionGateReason=pet_imu::name(reason);motionFaultLabel="IMU STOP";motionNoticeUntil=0;
  switch(reason) {
    case pet_imu::Trip::Continuity:motionFaultDetail="IMU sample gap. Motion OFF; set down.";break;
    case pet_imu::Trip::Stale:motionFaultDetail="IMU data stale. Motion OFF; set down.";break;
    case pet_imu::Trip::Tilt:motionFaultDetail="Tilt detected. Motion OFF; set down.";break;
    case pet_imu::Trip::Strong:motionFaultDetail="Strong motion. Motion OFF; set down.";break;
    case pet_imu::Trip::LowGravity:motionFaultDetail="Low gravity. Motion OFF; set down.";break;
    default:motionFaultDetail="IMU data invalid. Motion is OFF.";break;
  }
}
void switchPage(Page next) {
  motionOff(pet_motion::StopReason::Page);
  local_audio::allowPage(false); // cancels and erases local PCM before leaving
  codex_hid::releaseInputs(); micOn=false;
  input.cancel(); ++pageGeneration;
  page=next; petHudValid=false;lumi.requestFrame();
  if(next==Page::Audio){local_audio::allowPage(!powerCutFault);audioNotice=powerCutFault?"Power OFF unconfirmed; use power key":"Hold REC, release; wait for SPEAK NOW";}
  if(legacyReady)face.setEnabled(screenOn && page==Page::Pet && !lumiSkin);
  if(page==Page::Pet && legacyReady && !lumiSkin) {
    face.setAffectionState(affection.state());
  }
  lastRender=0;
}
void setScreen(bool enabled) {
  screenOn=enabled; petHudValid=false; input.cancel(); ++pageGeneration;
  codex_hid::releaseInputs();micOn=false;
  local_audio::allowPage(enabled && page==Page::Audio && !powerCutFault);
  if(legacyReady)face.setEnabled(enabled && page==Page::Pet && !lumiSkin);
  lumi.requestFrame();
  if(enabled){input.requireRelease();M5.Display.wakeup();M5.Display.setBrightness(100);}
  else{M5StackChan.showRgbColor(0,0,0);M5.Display.setBrightness(0);M5.Display.sleep();motionOff(pet_motion::StopReason::Screen);}
}
void execute(Intent action,uint32_t now) {
  if(action==Intent::None)return;
  if(action==Intent::Stop){++stopExecutions;stopExecutedAt=now;motionOff();local_audio::stop();audioNotice="STOP: recording cleared";return;}
  if(action==Intent::OpenAudio){switchPage(Page::Audio);return;}
  if(action==Intent::CloseAudio){switchPage(Page::Control);return;}
  if(page==Page::Audio) {
    if(powerCutFault){audioNotice="Power OFF unconfirmed; use power key";return;}
    if(servoPowered || movement.enabled() || servoWarmup.pending()){motionOff();audioNotice="Motion stopped; try again";return;}
    local_audio::Command command=local_audio::Command::None;
    if(action==Intent::AudioTone)command=local_audio::Command::Tone;
    if(action==Intent::AudioRecord)command=local_audio::Command::Record;
    if(action==Intent::AudioPlay)command=local_audio::Command::Play;
    if(action==Intent::AudioClear)command=local_audio::Command::Clear;
    if(command!=local_audio::Command::None) {
      const bool accepted=local_audio::request(command);
      audioNotice=accepted?(command==local_audio::Command::Clear?"Stopping and clearing audio":"Local only | STOP clears audio"):!localAudioReady?"Audio unavailable":
        command==local_audio::Command::Play&&!local_audio::snapshot().ready?"Record first":"Busy; wait or STOP";
      ++pageGeneration;
    }
    return; // audio page never routes an event to Codex HID
  }
  if(action==Intent::OpenControl){switchPage(Page::Control);return;}
  if(action==Intent::CloseControl){switchPage(Page::Pet);return;}
  if(action==Intent::OpenGallery){galleryClip=0;galleryFrame=-1;galleryAt=now;switchPage(Page::Gallery);return;}
  if(page==Page::Gallery) {
    if(action==Intent::GalleryPrevious)galleryClip=(galleryClip+muse::kStates-1)%muse::kStates;
    if(action==Intent::GalleryNext)galleryClip=(galleryClip+1)%muse::kStates;
    if(action==Intent::ToggleSkin)lumiSkin=!lumiSkin;
    galleryAt=now;galleryFrame=-1;lumi.requestFrame();++pageGeneration;return;
  }
  if(action==Intent::Pair){++pairRequests;codex_hid::advertise();return;}
  if(action>=Intent::Agent0 && action<=Intent::Agent5) {
    const int i=static_cast<int>(action)-static_cast<int>(Intent::Agent0);
    if(codex_hid::tapAgent(i)){selected=i;++pageGeneration;}return;
  }
  // All calls below originate ONLY in the screen release router, never rear touch.
  if(action==Intent::Approve && selectedApproval(now)) codex_hid::tapAction(7);
  if(action==Intent::Reject && selectedApproval(now)) codex_hid::tapAction(8);
  if(action==Intent::Send){codex_hid::releaseInputs();micOn=false;codex_hid::tapAction(12);}
  if(action==Intent::Fast) codex_hid::tapAction(6);
  if(action==Intent::Fork) codex_hid::tapAction(9);
  if(action==Intent::Mic) {
    motionOff(pet_motion::StopReason::Microphone);
    if(codex_hid::microphone(!micOn)){micOn=!micOn;micStarted=now;}
  }
  if(action==Intent::Dance) {
    ++danceRequests;
    if(petting || (!lumiSkin && legacyReady && face.petAnimationActive())){danceResult="pet_animation";danceNotice="PET BUSY";}
    else if(shaking){danceResult="shaking";danceNotice="WAIT";}
    else if(selectedWaiting(now)){danceResult="awaiting";danceNotice="WAIT";}
    else if(movement.request(pet_motion::Activity::Dance,now)){danceResult="started";danceNotice="DANCING";}
    else{danceResult="motion_busy_or_off";danceNotice="MOVE BUSY";}
    danceNoticeUntil=now+2000;
    Serial.printf("[input] dance request=%u result=%s petting=%d animation=%d activity=%s\n",
                  danceRequests,danceResult,petting,!lumiSkin && legacyReady && face.petAnimationActive(),pet_motion::name(movement.activity()));
  }
  if(action==Intent::Motion) {
    if(movement.enabled() || servoWarmup.pending()){motionOff();return;}
    ++motionEnableAttempts;
    motionAttemptAt=now;motionPowerConfig.fill(-1);motionPowerAt.fill(0);motionPowerNext=6;
    motionStartPowerLevel=-1;motionPoseCaptured=false;motionDiagReads=0;
    motionRaw.fill(-32768);motionReplyError.fill(-1);motionReplyStatus.fill(-1);
    motionUartSampled=false;motionUartStage="not_sampled";motionUartError=0;
    motionNoticeUntil=now+2500;
    if(powerCutFault){motionGateReason="power_off_failed";motionNotice="POWER ERR";return;}
    if(!M5StackChan.servoPowerConfigReady()){failMotion("power_config","POWER ERR","Power configuration unverified. Motion OFF.");return;}
    if(!lumiSkin){motionGateReason="legacy_skin";motionNotice="USE LUMI";return;}
    if(local_audio::busy()){motionGateReason="audio_busy";motionNotice="AUDIO BUSY";return;}
    if(micOn){motionGateReason="host_mic";motionNotice="MIC ACTIVE";return;}
    if(shaking){motionGateReason="shaking";motionNotice="WAIT";return;}
    if(page!=Page::Pet)switchPage(Page::Pet);
    if(!imuGuard.arm(now)) {
      motionGateReason="imu_wait";motionNotice="WAIT";
      Serial.println("[motion] enable refused: need fresh, steady IMU for 500 ms");return;
    }
    motionNoticeUntil=0;motionFaultLabel=motionFaultDetail="";motionGateReason="starting";
    const int powerWrite=requestServoPower(true);servoPowered=powerWrite==1;
    if(powerWrite!=1){failMotion("power_write","POWER ERR","Power write failed. Motion OFF requested.");return;}
    motionStartPowerLevel=M5StackChan.servoPowerGateLevel();
    motionPowerConfig[0]=motionStartPowerLevel;motionPowerAt[0]=millis();
    if(motionStartPowerLevel<0){failMotion("power_read","POWER ERR","Power read failed. Motion OFF.");return;}
    if(motionStartPowerLevel!=1){failMotion("power_gate","POWER ERR","Servo power gate not HIGH. Motion OFF.");return;}
    motionPowerNext=1;
    servoWarmup.start(now);
    ++pageGeneration;petHudValid=false;
    Serial.println("[motion] power warm-up; waiting for two valid stable poses");
  }
}
void updateInput(uint32_t now) {
  const auto& touch=M5.Touch.getDetail();
  const bool down=touch.isPressed();
  const bool pressed=down && !screenWasDown,released=!down && screenWasDown;
  if(pressed)++screenPresses;
  if(released)++screenReleases;
  screenWasDown=down;
  if(down){lastX=touch.x;lastY=touch.y;}
  if(!screenOn){input.cancel();return;}
  const auto& s=codex_hid::state();
  const companion::Context context=companion::inputContext(page,pageGeneration,s,selected,
    movement.enabled(),servoWarmup.pending() || imuGuard.ready(now),now);
  if(pressed) {
    if(page==Page::Pet && lastX>=4 && lastX<82 && lastY>=4 && lastY<48 &&
       !context.motionReady && !context.motionEnabled) {
      motionGateReason="imu_wait";motionNotice="WAIT";motionNoticeUntil=now+2500;
    }
    if(page==Page::Pet && lastX>=88 && lastX<166 && lastY>=4 && lastY<48 &&
       (!movement.enabled() || selectedWaiting(now))) {
      ++disabledDancePresses;danceResult=selectedWaiting(now)?"awaiting":"motion_off";
      danceNotice=selectedWaiting(now)?"WAIT":"MOVE FIRST";danceNoticeUntil=now+2500;
    }
    pressX=pressMinX=pressMaxX=lastX;pressY=pressMinY=pressMaxY=lastY;
  } else if(down) {
    pressMinX=std::min(pressMinX,lastX);pressMaxX=std::max(pressMaxX,lastX);
    pressMinY=std::min(pressMinY,lastY);pressMaxY=std::max(pressMaxY,lastY);
  }
  const auto before=input.feedback(now,context);
  const auto action=input.screen(down,lastX,lastY,now,context);
  touchFeedback=input.feedback(now,context);
  if(pressed)Serial.printf("[input] press x=%d y=%d candidate=%d motion=%d\n",lastX,lastY,
                          static_cast<int>(touchFeedback.intent),movement.enabled());
  if(released) {
    lastTouchFeedback=before;lastTouchAction=action;
    Serial.printf("[input] release start=%d,%d end=%d,%d bounds=%d,%d,%d,%d candidate=%d ms=%u phase=%s action=%d\n",
                  pressX,pressY,lastX,lastY,pressMinX,pressMaxX,pressMinY,pressMaxY,
                  static_cast<int>(before.intent),before.heldMs,before.phase,static_cast<int>(action));
    if(before.intent==Intent::Motion && action==Intent::None) {
      motionNotice=!strcmp(before.phase,"holding")?"HOLD 1s":"CANCELLED";motionNoticeUntil=now+2000;
    }
    if(before.intent==Intent::Dance && action==Intent::None) {
      danceNotice=!strcmp(before.phase,"holding")?"HOLD 0.6s":"CANCELLED";
      danceNoticeUntil=now+2000;
    }
  }
  execute(action,now);
}
void updatePet(uint32_t now) {
  const auto& levels=M5StackChan.TouchSensor.getIntensities();
  for(int i=0;i<3;++i)rearRaw[i]=levels[i];
  const bool contact=pet_gesture::contact(rearRaw[0],rearRaw[1],rearRaw[2]);
  // Rear touch cannot call execute() or HID. Timer starts at first raw contact.
  input.rear(contact,now);
  const auto event=rearGesture.update(contact,screenOn,now);
  touchReady=rearGesture.ready();petting=rearGesture.active();
  if(event.began) {
    lastStrokeLong=false;
    if(page==Page::Pet)movement.request(pet_motion::Activity::Pet,now);
    Serial.printf("[pet] begin session=%u raw=%u,%u,%u\n",event.session,rearRaw[0],rearRaw[1],rearRaw[2]);
  }
  if(event.ended) {
    lastStrokeLong=event.longStroke;rewardBefore=affection.state().affection;
    rewardDuration=event.durationMs;rewardSession=event.session;
    rewardCooldown=affection.petCooldownRemaining(now);
    const auto result=event.longStroke?affection.applyEvent("petting",1,1,nullptr,now):affection.debugAdjust(-1);
    rewardDelta=result.delta;rewardAfter=result.state.affection;rewardSeq=result.state.seq;
    rewardReason=!event.longStroke?"short_stroke":rewardAfter==1000?"cap":rewardCooldown?"cooldown":"long_stroke";
    snprintf(petFeedback,sizeof(petFeedback),"LOVE %+d | %s",rewardDelta,!event.longStroke?"STROKE 3s":rewardAfter==1000?"MAX":rewardCooldown?"COOLDOWN":"THANK YOU");
    feedbackUntil=now+4000;applyAffection(result,now);lastShake=now;
    Serial.printf("[pet] end session=%u ms=%u reason=%s delta=%d before=%d after=%d seq=%u\n",
      rewardSession,rewardDuration,rewardReason,rewardDelta,rewardBefore,rewardAfter,rewardSeq);
  }
  if(shaking && int32_t(now-shakeUntil)>=0) {
    shaking=false;
  }
  applyAffection(affection.update(now,!servoPowered),now);
}
void updateImu(uint32_t now) {
  // Always sampled, including active dance, rear-touch calibration and petting.
  // Only the cosmetic reaction is masked for servo movement/cooldown.
  const bool cosmetic=screenOn && !petting && !movement.active() && uint32_t(now-lastShake)>4500;
  const auto event=imuGuard.poll(M5.Imu,now,cosmetic,[](pet_imu::Trip reason) {
    failImu(reason);
    Serial.printf("[motion] IMU stop: %s\n",pet_imu::name(reason));
  });
  if(event.dizzy) {
    lastShake=now;shakeUntil=now+2200;shaking=true;
    motionOff(pet_motion::StopReason::Shake);
    applyAffection(affection.applyEvent("shake",1,1,nullptr,now),now);
    Serial.println("[pet] IMU dizzy reaction");
  }
}
void button(int x,int y,int w,int h,const char* label,uint16_t fill,uint16_t border=0x39e7) {
  controls.fillRoundRect(x,y,w,h,8,fill);controls.drawRoundRect(x,y,w,h,8,border);
  controls.setTextDatum(middle_center);controls.setTextColor(TFT_WHITE,fill);
  controls.setFont(&fonts::Font2);controls.setTextSize(1);
  controls.drawString(label,x+w/2,y+h/2);
}
void drawHealthFooter(M5Canvas& target) {
  const uint32_t at=micros();
  target.fillRect(0,runtime_health::kY,runtime_health::kWidth,runtime_health::kHeight,0x0841);
  target.setFont(&fonts::Font0);target.setTextSize(1);target.setTextDatum(top_left);
  target.setTextColor(runtimeHealth.ready()?0x7df7:0xfd20,0x0841);
  target.drawString(runtimeHealth.text(),1,runtime_health::kY);
  maxHealthComposeUs=std::max(maxHealthComposeUs,uint32_t(micros()-at));
  // RAM composition only; the caller submits its one existing complete frame.
}
void showBootHealth(runtime_health::Boot step) {
  if(!bootHealthAvailable)return;
  char label[32];snprintf(label,sizeof(label),"BOOT %u %s",unsigned(step),runtime_health::name(step));
  bootHealth.fillScreen(0x0841);bootHealth.setFont(&fonts::Font0);
  bootHealth.setTextSize(1);bootHealth.setTextDatum(top_left);
  bootHealth.setTextColor(TFT_WHITE,0x0841);bootHealth.drawString(label,1,0);
  bootHealth.pushSprite(0,runtime_health::kY); // Startup only: one composed 320x8 stripe.
}
void drawControl(uint32_t now) {
  if(!canvasReady)return;
  controls.fillScreen(0x0841);
  button(4,3,54,34,"PET",0x1947);
  controls.setTextDatum(middle_center);controls.setTextColor(TFT_WHITE,0x0841);
  button(64,3,62,34,"STOP",0x9106);
  button(132,3,82,34,"AUDIO",0x1947);
  const auto& s=codex_hid::state();
  const auto link=codex_hid::connectionInfo();
  const char* linkLabel=s.connected?"LINKED":!link.available?"BLE ERROR":
                        link.connected?"LINKING":link.advertising?"ON MAC":link.failed?"RETRY":"PAIR";
  button(220,3,96,34,linkLabel,s.connected?0x2388:0x3188);
  if(s.connected) {
    for(int i=0;i<6;++i) {
      char label[18];snprintf(label,sizeof(label),"%d %s",i+1,s.fresh(i,now)?(s.awaiting(i,now)?"WAIT":"CODEX"):s.cached(i,now)?"CACHED":"UNKNOWN");
      button(8+104*(i%3),48+40*(i/3),96,36,label,rgb565(hostColor(i,now)),i==selected?TFT_WHITE:0x39e7);
    }
    if(!codex_hid::receivedStatusMessages()) {
      controls.setFont(&fonts::Font0);controls.setTextColor(0xbdf7,0x0841);
      controls.drawString("Open Codex on your Mac",160,127);
    }
  } else {
    controls.setFont(&fonts::Font2);controls.setTextColor(TFT_WHITE,0x0841);
    controls.drawString(!link.available?"Bluetooth unavailable":link.connected?"Mac is connecting...":
                        link.advertising?"Mac: Settings > Bluetooth":link.failed?"Could not start Bluetooth":"Tap PAIR to connect",160,59);
    controls.drawString("VibeStackChan Pet",160,84);
    controls.setTextColor(0xbdf7,0x0841);
    controls.drawString(link.connected?"Waiting for encrypted HID":link.advertising?"Choose this name, then Connect":
                        link.failed?"Tap RETRY":"Then connect on your Mac",160,109);
  }
  const bool waiting=selectedApproval(now);
  button(8,132,96,48,"HOLD OK",waiting?0x2388:0x2104);
  button(112,132,96,48,"HOLD NO",waiting?0x9106:0x2104);
  button(216,132,96,48,"SEND",s.connected?0x294b:0x2104);
  button(8,190,74,42,"FAST",0x2106);
  button(88,190,74,42,"FORK",0x2106);
  button(168,190,74,42,micOn?"MIC STOP":"MAC MIC",micOn?0x9106:0x2106);
  button(248,190,64,42,"LOOKS",0x1947);
  drawHealthFooter(controls);controls.pushSprite(0,0);
}
void drawAudio(uint32_t now) {
  (void)now;if(!canvasReady)return;
  const auto a=local_audio::snapshot();
  controls.fillScreen(0x0841);
  button(4,3,72,34,"BACK",0x1947);button(88,3,94,34,"STOP",0x9106);
  controls.setTextDatum(middle_center);controls.setFont(&fonts::Font2);
  controls.setTextColor(TFT_WHITE,0x0841);controls.drawString("LOCAL AUDIO",252,20);
  const auto message=local_audio::presentation(a);
  controls.setTextColor(message.recording?TFT_RED:TFT_WHITE,0x0841);controls.drawString(message.title,160,57);
  controls.setTextColor(0xbdf7,0x0841);controls.setFont(&fonts::Font0);
  controls.drawString(message.detail,160,82);
  const bool recordHeld=touchFeedback.intent==Intent::AudioRecord && !local_audio::active(a.phase);
  controls.drawString(recordHeld?(!strcmp(touchFeedback.phase,"release")?"RELEASE TO PREPARE":"HOLD REC FOR 1 SECOND"):
    a.phase==local_audio::Phase::Off?audioNotice:message.hint,160,106);
  button(8,128,146,44,"TONE 150ms",0x1947);
  button(166,128,146,44,recordHeld&&!strcmp(touchFeedback.phase,"release")?"RELEASE":"HOLD REC",a.mic?0x9106:0x1947);
  button(8,184,146,44,"PLAY ONCE",a.ready?0x2388:0x2104);
  button(166,184,146,44,"STOP / CLEAR",0x2106);
  drawHealthFooter(controls);controls.pushSprite(0,0);
}
PetHudSnapshot capturePetHud(uint32_t now) {
  PetHudSnapshot next;
  for(int i=0;i<6;++i)next.colors[i]=hostColor(i,now);
  next.affection=affection.state().affection;
  next.status=selectedFresh(now)?(selectedWaiting(now)?2:1):codex_hid::state().cached(selected,now)?3:0;
  next.moving=movement.active();next.armed=movement.enabled();next.activity=movement.activity();
  next.imuReady=imuGuard.ready(now);
  next.starting=servoWarmup.pending();
  next.moveLabel=next.starting?"STARTING":next.armed?"MOVE ON":!lumiSkin?"USE LUMI":
    *motionFaultLabel?motionFaultLabel:next.imuReady?"HOLD MOVE":"WAIT";
  next.danceLabel=next.status==2?"WAIT":next.activity==pet_motion::Activity::Dance?"DANCING":!next.armed?"MOVE FIRST":"HOLD DANCE";
  if(danceNoticeUntil && int32_t(danceNoticeUntil-now)>0)next.danceLabel=danceNotice;
  if(touchFeedback.intent==Intent::Dance) {
    next.danceLabel=!strcmp(touchFeedback.phase,"release")?"RELEASE":
                    !strcmp(touchFeedback.phase,"holding")?"HOLD...":"CANCELLED";
    if(!strcmp(touchFeedback.phase,"holding") || !strcmp(touchFeedback.phase,"release"))
      next.danceProgress=std::min<uint32_t>(6,touchFeedback.heldMs/100);
  }
  if(motionNoticeUntil && int32_t(motionNoticeUntil-now)>0)next.moveLabel=motionNotice;
  if(touchFeedback.intent==Intent::Motion) {
    next.moveLabel=!strcmp(touchFeedback.phase,"release")?"RELEASE":!strcmp(touchFeedback.phase,"holding")?"HOLD...":"CANCELLED";
    next.moveProgress=std::min<uint32_t>(10,touchFeedback.heldMs/100);
  }
  if(powerCutFault){next.moveLabel="POWER ERR";snprintf(next.feedback.data(),next.feedback.size(),"Power OFF unconfirmed. Use power key.");}
  else if(*motionFaultDetail)snprintf(next.feedback.data(),next.feedback.size(),"%s",motionFaultDetail);
  else if(!next.armed && !next.starting && !next.imuReady)snprintf(next.feedback.data(),next.feedback.size(),"Set down; wait still. MOVE is disabled.");
  else if(petting)snprintf(next.feedback.data(),next.feedback.size(),"STROKE %.1fs / 3s - RELEASE",rearGesture.duration(now)/1000.f);
  else if(feedbackUntil && int32_t(feedbackUntil-now)>0)snprintf(next.feedback.data(),next.feedback.size(),"%s",petFeedback);
  else if(!affection.lastSaveOk())snprintf(next.feedback.data(),next.feedback.size(),"SAVE FAILED - RETRY PENDING");
  return next;
}
void drawPetHud(M5Canvas& target) {
  // Erase/redraw only in RAM. The face and HUD share one finished LCD frame.
  const uint16_t bg=lumiSkin?M5.Display.color565(7,16,28):TFT_BLACK;
  target.fillRect(0,0,40,240,bg);target.fillRect(280,0,40,240,bg);
  target.setTextDatum(middle_center);target.setFont(&fonts::Font0);target.setTextSize(1);
  auto localButton=[&](int x,int w,const char* label,uint16_t color){
    target.fillRoundRect(x,4,w,44,6,color);target.setTextColor(TFT_WHITE,color);
    target.drawString(label,x+w/2,26);
  };
  localButton(4,78,petHud.moveLabel,petHud.armed?0x2388:0x1947);
  if(petHud.moveProgress)target.fillRect(8,43,petHud.moveProgress*7,2,TFT_WHITE);
  localButton(88,78,petHud.danceLabel,
              petHud.armed && petHud.status!=2?0x294b:0x2104);
  if(petHud.danceProgress)target.fillRect(92,43,petHud.danceProgress*11,2,TFT_WHITE);
  localButton(172,72,"STOP",0x9106);localButton(254,62,"CODEX",0x1947);
  for(int i=0;i<6;++i){target.fillCircle(18,60+i*26,6,rgb565(petHud.colors[i]));}
  target.setTextColor(0xbdf7,bg);target.setTextDatum(bottom_center);
  target.fillRect(44,220,232,20,bg);
  char status[64];
  snprintf(status,sizeof(status),"%s | %s | LOVE %d",petHud.status==2?"NEEDS YOU":petHud.status==1?"CODEX LINK":petHud.status==3?"LAST STATE":"UNKNOWN",
           petHud.armed?pet_motion::name(petHud.activity):"MOVE OFF",petHud.affection);
  target.drawString(status,160,229); // Leave the bottom 8px for independent loop evidence.
  if(petHud.feedback[0]) {
    target.fillRoundRect(44,198,232,20,4,0x1947);target.setTextColor(TFT_WHITE,0x1947);
    target.setTextDatum(middle_center);target.drawString(petHud.feedback.data(),160,208);
  }
  drawHealthFooter(target);
}
void drawPet(uint32_t now) {
  const auto next=capturePetHud(now);
  if(!petHudValid || !(next==petHud)) {
    petHud=next;petHudValid=true;lumi.requestFrame();if(legacyReady)face.requestFrame();
  }
  if(lumiSkin) {
    const auto clip=lumiDirector.update(codex_hid::state(),selected,
      {petting,shaking,movement.activity()==pet_motion::Activity::Dance,movement.gaze()},now);
    const uint8_t frame=clip==muse::Clip::Look?muse::gazeFrame(movement.gaze()):muse::frame(clip,lumiDirector.age(now));
    lumi.draw(clip,frame,drawPetHud);
  } else if(legacyReady) {
    face.update(now);face.presentFrame();
  }
}
void drawGalleryHud(M5Canvas& target) {
  target.setTextDatum(middle_center);target.setFont(&fonts::Font0);target.setTextSize(1);
  auto item=[&](int x,int y,int w,int h,const char* label,uint16_t color) {
    target.fillRoundRect(x,y,w,h,6,color);target.setTextColor(TFT_WHITE,color);
    target.drawString(label,x+w/2,y+h/2);
  };
  item(4,3,62,34,"PET",0x1947);item(88,3,94,34,"STOP",0x9106);
  item(220,3,96,34,lumiSkin?"USE: LUMI":"USE: CLASSIC",0x1947);
  item(4,190,60,42,"<",0x1947);item(256,190,60,42,">",0x1947);
  target.setTextColor(0x9e79);target.drawString("PREVIEW / NO ACTION",160,44);
  char label[48];snprintf(label,sizeof(label),"%02u/18 %s",galleryClip+1,muse::name(static_cast<muse::Clip>(galleryClip)));
  target.fillRect(66,220,188,20,M5.Display.color565(7,16,28));
  target.drawString(label,160,226);
  drawHealthFooter(target);
}
void drawGallery(uint32_t now) {
  const auto clip=static_cast<muse::Clip>(galleryClip);
  lumi.draw(clip,galleryFrame>=0?galleryFrame:muse::frame(clip,uint32_t(now-galleryAt),true),drawGalleryHud);
}
void updateLights(uint32_t now) {
  static uint32_t previous=0xffffffff;
  const uint32_t color=screenOn?hostColor(selected,now):0;
  if(color==previous)return;previous=color;
  const float scale=selectedFresh(now)?0.12f:0.025f;
  M5StackChan.showRgbColor(((color>>16)&255)*scale,((color>>8)&255)*scale,(color&255)*scale);
}
void updateHostCue(uint32_t now) {
  const bool waiting=selectedWaiting(now);
  // A fresh host transition may ask for attention, but never causes approval.
  // The user must have explicitly enabled motion; pet interactions take priority.
  if(waiting && !previousWaiting && screenOn && !petting && !shaking &&
     uint32_t(now-lastHostCue)>=12000) {
    movement.request(pet_motion::Activity::Attention,now);lastHostCue=now;
  }
  previousWaiting=waiting;
}
void updateMotion(uint32_t now) {
  if(uint32_t(now-lastMotionTick)<40)return;
  lastMotionTick=now;
  if(servoWarmup.pending()) {
    if(!screenOn || page!=Page::Pet || micOn || shaking || !imuGuard.armed()) {
      motionOff(pet_motion::StopReason::Imu);return;
    }
    const auto request=servoWarmup.poll(now);
    if(request==pet_motion::ServoWarmup::Poll::None && motionPowerNext<6) {
      const unsigned field=motionPowerNext++;
      motionPowerConfig[field]=M5StackChan.servoPowerDiagnosticField(field);
      motionPowerAt[field]=millis();
      if(motionPowerConfig[field]<0)motionPowerNext=6; // no retry storm after NACK
      return; // At most one diagnostic I2C transaction this loop; no UART read.
    }
    if(request==pet_motion::ServoWarmup::Poll::Expired) {
      failWarmup();
      Serial.println("[motion] power warm-up expired without valid stable pose");return;
    }
    if(request==pet_motion::ServoWarmup::Poll::ReadPose) {
      // First reached only after the user's HOLD MOVE and 600 ms powered warm-up.
      const bool prepared=M5StackChan.prepareMotion();
      motionUartSampled=true;motionUartStage=M5StackChan.servoUartInitStage();
      motionUartError=M5StackChan.servoUartInitError();
      if(!prepared) {
        failMotion(M5StackChan.motionUartReady()?"task_init":"uart_init","INIT ERR",
          M5StackChan.motionUartReady()?"Servo task unavailable. Motion OFF.":"Servo UART unavailable. Motion OFF.");return;
      }
      const auto pose=M5StackChan.Motion.getCurrentAngles();
      motionPoseYaw=pose.x;motionPosePitch=pose.y;motionPoseAt=millis();motionPoseCaptured=true;
      ++motionDiagReads;
      for(int axis=0;axis<2;++axis) {
        motionRaw[axis]=M5StackChan.servoFeedbackRaw(axis);
        motionReplyError[axis]=M5StackChan.servoFeedbackError(axis);
        // The protocol retains an older status byte on a missing response.
        motionReplyStatus[axis]=motionRaw[axis]>=0 && motionReplyError[axis]==0?
          M5StackChan.servoFeedbackStatus(axis):-1;
      }
      Serial.printf("[motion] warm-up pose yaw=%d pitch=%d (tenths of degree)\n",pose.x,pose.y);
      if(servoWarmup.accept(pose.x,pose.y,millis())) {
        if(!movement.enable(pose.x,pose.y,now,esp_random())) {
          failMotion("pose_range","POSE ERR","Pose outside safe start range. Motion OFF.");return;
        }
        motionGateReason="enabled";
        ++pageGeneration;petHudValid=false;
        Serial.println("[motion] enabled after stable pose qualification");
      } else if(!servoWarmup.pending()) {
        failWarmup();
      }
    }
    return; // No movement target is sent during power-up qualification.
  }
  const bool busy=movement.enabled() && M5StackChan.Motion.isMoving();
  const pet_motion::Guard guard{page==Page::Pet,screenOn,micOn,shaking,petting,selectedWaiting(now),screenWasDown};
  const bool wasActive=movement.active();
  const auto result=movement.tick(now,busy,guard);
  if(result.powerOff){motionOff(movement.reason());return;}
  if(result.move || (wasActive && !movement.active())) {
    const auto pose=M5StackChan.Motion.getCurrentAngles();
    const bool poseOk=result.move?movement.posePermits(result,pose.x,pose.y):movement.poseAtRestPermits(pose.x,pose.y);
    Serial.printf("[motion] rest yaw=%d pitch=%d target_axis=%s target=%d pose_ok=%d\n",
                  pose.x,pose.y,result.move?(result.axis==pet_motion::Axis::Yaw?"yaw":"pitch"):"complete",result.target,poseOk);
    if(!poseOk) {
      failMotion("pose_tracking","POSE ERR","Servo did not reach safe pose. Motion OFF.");return;
    }
    if(!result.move)return; // Final step also requires real home readback.
    if(result.axis==pet_motion::Axis::Yaw)M5StackChan.Motion.moveYaw(result.target,result.speed);
    else M5StackChan.Motion.movePitch(result.target,result.speed);
    movement.commandSent(result);
    ++motionCommands;lastShake=now;
  }
}
void motionDiagnostics() {
  const uint32_t now=millis();
  JsonDocument d;
  d["kind"]="motion_diag";d["uptime_ms"]=now;
  d["fw"]=firmware_version::kBuild;d["loop"]=runtimeHealth.completed();
  d["frames"]=lumiSkin?lumi.presented():face.presentedFrames();
  d["imu_samples"]=imuGuard.samples();d["imu_ready"]=imuGuard.ready(now);
  d["imu_last"]=imuGuard.lastGapMs();d["imu_max"]=imuGuard.maxGapMs();
  d["imu_gaps"]=imuGuard.gapCount();d["imu_trips"]=imuGuard.trips();
  d["outputs"]=(servoPowered?1u:0u)|(movement.enabled()?2u:0u)|(servoWarmup.pending()?4u:0u)|
    (M5.Speaker.isRunning()?8u:0u)|(M5.Mic.isRunning()?16u:0u)|(local_audio::busy()?32u:0u)|
    (micOn?64u:0u)|(codex_hid::sentActions()?128u:0u);
  d["inputs"]=screenPresses+screenReleases+rearGesture.session();
  d["attempt"]=motionEnableAttempts;d["attempt_at_ms"]=motionAttemptAt;
  d["attempt_age_ms"]=motionEnableAttempts?uint32_t(now-motionAttemptAt):0;
  d["reason"]=motionGateReason;d["requested_power"]=servoPowered;
  d["targets"]=motionCommands;d["pending"]=servoWarmup.pending();
  // All values below are historical caches; timestamps can differ by field.
  const char* names[]={"input","direction","output","pull_up","pull_down","open_drain"};
  for(unsigned i=0;i<6;++i){d["power"][names[i]]=motionPowerConfig[i];d["power_at_ms"][i]=motionPowerAt[i];}
  d["uart_sampled"]=motionUartSampled;d["uart_stage"]=motionUartStage;d["uart_error"]=motionUartError;
  d["pose_sampled"]=motionPoseCaptured;d["pose_reads"]=motionDiagReads;
  if(motionPoseCaptured) {
    d["pose_at_ms"]=motionPoseAt;d["pose_age_ms"]=uint32_t(now-motionPoseAt);
    for(unsigned i=0;i<2;++i) {
      d["raw"][i]=motionRaw[i];d["reply_error"][i]=motionReplyError[i];
      d["reply_status"][i]=motionReplyStatus[i];
    }
    d["yaw"]=motionPoseYaw;d["pitch"]=motionPosePitch;
  }
  statusWriter.send(d,Serial);
}
void powerDiagnostics() {
  JsonDocument d;
  d["kind"]="power_diag_off";d["fw"]=firmware_version::kBuild;d["uptime_ms"]=millis();
  d["off_context"]=powerOffContext();d["cutoff_unconfirmed"]=powerCutFault;
  d["requests"]=powerSnapshotRequests;d["accepted"]=powerSnapshotAccepted;
  d["state"]=power_probe::name(powerSnapshot.state);d["attempt"]=powerSnapshot.attempt;
  d["started_ms"]=powerSnapshot.started;
  const power_probe::WriteRecord* writes[]={&powerWrites.enable,&powerWrites.disable};
  const char* names[]={"on","off"};
  for(unsigned i=0;i<2;++i) {
    const auto& w=*writes[i];auto v=d["writes"][names[i]].to<JsonObject>();
    v["sampled"]=w.sampled;v["result"]=static_cast<int>(w.result);
    v["started_ms"]=w.started;v["finished_ms"]=w.finished;v["attempt"]=w.attempt;
  }
  auto fields=d["fields"].to<JsonArray>();
  for(unsigned i=0;i<powerSnapshot.samples.size();++i) {
    const auto& s=powerSnapshot.samples[i];auto f=fields.add<JsonObject>();
    f["reg"]=power_probe::OffSnapshot::registers[i];f["valid"]=s.valid;f["value"]=s.value;
    f["started_ms"]=s.started;f["finished_ms"]=s.finished;
  }
  statusWriter.send(d,Serial); // Cached fields only; one whole packet, no GPIO read here.
}
void telemetry() {
  const uint32_t telemetryAt=micros(),now=millis();
  const auto& s=codex_hid::state();
  JsonDocument d;
  d["kind"]="vibe_pet_status";d["uptime_ms"]=now;
  firmware_version::stampTelemetry(d);
  d["board"]=static_cast<int>(M5.getBoard());d["width"]=M5.Display.width();d["height"]=M5.Display.height();
  d["assets"]=lumiSkin?"lumi-v1":legacyReady?faceAssetModeName(face.faceAssetStatus().mode):"classic-pending";d["filesystem"]=fsReady;
  d["page"]=page==Page::Pet?"pet":page==Page::Gallery?"gallery":page==Page::Audio?"audio":"control";d["hid_connected"]=s.connected;
  d["skin"]=lumiSkin?"lumi":"classic";d["visual_preview"]=page==Page::Gallery;
  d["lumi_state"]=muse::name(page==Page::Gallery?static_cast<muse::Clip>(galleryClip):lumiDirector.clip());
  d["lumi_rendered_state"]=lumi.renderedState();d["lumi_rendered_frame"]=lumi.renderedFrame();
  d["lumi_asset_failures"]=lumi.failures();d["lumi_loads"]=lumi.loads();d["lumi_max_decode_us"]=lumi.maxDecodeUs();
  d["lumi_max_draw_us"]=lumi.maxDrawUs();d["lumi_startup_max_read_us"]=lumi.maxReadUs();
  d["lumi_misses"]=lumi.misses();d["lumi_pending"]=lumi.pending();
  d["max_board_us"]=maxBoardUs;d["max_inputs_us"]=maxInputsUs;d["max_render_us"]=maxRenderUs;
  d["max_serial_us"]=maxSerialUs;d["max_telemetry_us"]=maxTelemetryUs;
  d["telemetry_sent"]=statusWriter.sent();d["telemetry_backpressure"]=statusWriter.backpressure();
  d["telemetry_partial"]=statusWriter.partial();d["telemetry_oversize"]=statusWriter.oversize();
  d["loop_max_gap_ms"]=maxLoopGap;d["loop_count"]=loopCount;
  d["health_completed_loop"]=runtimeHealth.completed();d["health_boot_canvas"]=bootHealthAvailable;
  d["health_compose_max_us"]=maxHealthComposeUs;
  d["imu_samples"]=imuGuard.samples();d["imu_max_gap_ms"]=imuGuard.maxGapMs();
  d["imu_last_gap_ms"]=imuGuard.lastGapMs();d["imu_gap_count"]=imuGuard.gapCount();
  d["servo_zero_namespace"]=storedServoNamespace;d["servo_zero_yaw_raw"]=storedYawZero;d["servo_zero_pitch_raw"]=storedPitchZero;
  d["rear_raw_0"]=rearRaw[0];d["rear_raw_1"]=rearRaw[1];d["rear_raw_2"]=rearRaw[2];
  d["rear_session"]=rearGesture.session();d["rear_duration_ms"]=rearGesture.duration(now);
  d["reward_session"]=rewardSession;d["reward_duration_ms"]=rewardDuration;d["reward_reason"]=rewardReason;
  d["reward_delta"]=rewardDelta;d["reward_before"]=rewardBefore;d["reward_after"]=rewardAfter;
  d["reward_seq"]=rewardSeq;d["reward_cooldown_ms"]=rewardCooldown;
  d["affection_seq"]=affection.state().seq;d["mood"]=affection.state().mood;d["confusion"]=affection.state().confusion;
  d["affection_load_ok"]=affection.loadOk();d["affection_save_ok"]=affection.lastSaveOk();d["affection_save_pending"]=affection.pendingSave();
  d["affection_save_attempts"]=affection.saveAttempts();d["affection_saved_bytes"]=affection.savedAffectionBytes();
  d["affection_saved_seq_bytes"]=affection.savedSequenceBytes();d["affection_persisted"]=affection.persistedAffection();
  d["affection_persisted_seq"]=affection.persistedSeq();d["dance_disabled_presses"]=disabledDancePresses;
  d["speaker_running"]=M5.Speaker.isRunning();d["microphone_running"]=M5.Mic.isRunning();d["servo_powered"]=servoPowered;
  const auto audio=local_audio::snapshot();
  d["local_audio_available"]=localAudioReady;d["local_audio_phase"]=local_audio::name(audio.phase);
  d["local_audio_error"]=local_audio::name(audio.error);d["local_audio_samples"]=audio.samples;
  d["local_audio_peak"]=audio.peak;d["local_audio_rms"]=audio.rms;d["local_audio_clipped"]=audio.clipped;
  d["local_audio_dc"]=audio.dc;d["local_audio_ac_rms"]=audio.acRms;
  d["local_audio_ready"]=audio.ready;d["local_audio_seq"]=audio.sequence;d["local_audio_stop_ms"]=audio.stopMs;
  d["local_audio_prepare_remaining_ms"]=audio.prepareRemainingMs;d["local_audio_target_samples"]=local_audio::kSamples;
  d["local_audio_rate"]=local_audio::kRate;d["local_audio_record_ms"]=local_audio::kRecordMs;
  d["local_audio_tone_volume"]=local_audio::kToneVolume;d["local_audio_playback_volume"]=local_audio::kPlaybackVolume;
  const auto audioDebug=local_audio::diagnostics();
  d["local_audio_page_allowed"]=audioDebug.allowed;d["local_audio_cancel_pending"]=audioDebug.cancelPending;
  d["local_audio_working"]=audioDebug.working;d["local_audio_pending_command"]=static_cast<int>(audioDebug.pending);
  d["motion_initialized"]=M5StackChan.motionPrepared();d["quiet_boot"]=true;
  d["motion_uart_ready"]=M5StackChan.motionUartReady();
  d["servo_raw_yaw"]=M5StackChan.servoFeedbackRaw(0);d["servo_raw_pitch"]=M5StackChan.servoFeedbackRaw(1);
  d["servo_error_yaw"]=M5StackChan.servoFeedbackError(0);d["servo_error_pitch"]=M5StackChan.servoFeedbackError(1);
  d["servo_status_yaw"]=M5StackChan.servoFeedbackStatus(0);d["servo_status_pitch"]=M5StackChan.servoFeedbackStatus(1);
  d["servo_power_gate"]=M5StackChan.servoPowerGateLevel();
  d["power_on_write"]=static_cast<int>(powerWrites.enable.result);
  d["power_off_write"]=static_cast<int>(powerWrites.disable.result);
  d["power_cut_unconfirmed"]=powerCutFault;d["power_snapshot"]=power_probe::name(powerSnapshot.state);
  d["power_config_ready"]=M5StackChan.servoPowerConfigReady();
  d["power_config_stage"]=M5StackChan.servoPowerConfigStage();
  d["power_config_error"]=M5StackChan.servoPowerConfigError();
  d["power_config_reg"]=M5StackChan.servoPowerConfigRegister();
  d["power_config_actual"]=M5StackChan.servoPowerConfigActual();
  const auto link=codex_hid::connectionInfo();
  d["advertising"]=link.advertising;d["ble_connected"]=link.connected;d["ble_encrypted"]=link.encrypted;
  d["hid_subscribed"]=link.subscribed;d["pairing_requested"]=link.requested;d["pairing_failed"]=link.failed;
  d["hid_epoch"]=link.generation;d["pair_requests"]=pairRequests;
  d["screen_presses"]=screenPresses;d["screen_releases"]=screenReleases;
  d["touch_x"]=lastX;d["touch_y"]=lastY;
  d["touch_last_candidate"]=static_cast<int>(lastTouchFeedback.intent);
  d["touch_last_ms"]=lastTouchFeedback.heldMs;d["touch_last_phase"]=lastTouchFeedback.phase;
  d["touch_last_action"]=static_cast<int>(lastTouchAction);
  d["stop_executions"]=stopExecutions;
  if(stopExecutions)d["stop_executed_age_ms"]=uint32_t(now-stopExecutedAt);
  d["dance_requests"]=danceRequests;d["dance_result"]=danceResult;
  d["face_pet_animation"]=lumiSkin?petting:legacyReady && face.petAnimationActive();
  d["hid_rx"]=codex_hid::receivedMessages();d["hid_status_rx"]=codex_hid::receivedStatusMessages();
  d["host_poll_rx"]=s.hostPollCount;d["host_poll_seen"]=s.hostPollSeen;d["host_live"]=s.hostLive(now);
  if(s.hostPollSeen)d["host_poll_age_ms"]=uint32_t(now-s.hostPollAt);
  d["hid_tx"]=codex_hid::sentActions();d["hid_invalid"]=codex_hid::invalidMessages();
  d["selected"]=selected;d["fresh"]=s.fresh(selected,now);d["affection"]=affection.state().affection;
  auto agents=d["agents"].to<JsonArray>();
  for(int i=0;i<6;++i) {
    auto a=agents.add<JsonObject>();a["slot"]=i;a["fresh"]=s.fresh(i,now);a["awaiting"]=s.awaiting(i,now);
    a["cached"]=s.cached(i,now);a["display_known"]=s.displayKnown(i,now);a["approval_eligible"]=s.approvalEligible(i,now);a["seen"]=s.agents[i].seen;a["color"]=s.agents[i].color;a["effect"]=s.agents[i].effect;
    if(s.agents[i].seen)a["age_ms"]=uint32_t(now-s.agents[i].receivedAt);
  }
  d["rear_touch_ready"]=touchReady;d["petting"]=petting;d["motion_enabled"]=movement.enabled();
  d["motion_activity"]=pet_motion::name(movement.activity());d["motion_commands"]=motionCommands;
  d["motion_remaining_ms"]=movement.remainingMs(now);d["motion_stop_reason"]=pet_motion::reasonName(movement.reason());
  d["motion_enable_attempts"]=motionEnableAttempts;d["motion_pose_captured"]=motionPoseCaptured;
  d["motion_start_pending"]=servoWarmup.pending();d["motion_start_reads"]=servoWarmup.reads();
  d["motion_gate_reason"]=motionGateReason;d["motion_fault"]=motionFaultDetail;
  d["motion_start_power_level"]=motionStartPowerLevel;d["motion_start_result"]=servoWarmup.result();
  if(motionPoseCaptured) {
    d["motion_pose_yaw"]=motionPoseYaw;d["motion_pose_pitch"]=motionPosePitch;
    d["motion_pose_age_ms"]=uint32_t(now-motionPoseAt);
  }
  d["imu_guard_armed"]=imuGuard.armed();d["imu_ready"]=imuGuard.ready(now);
  d["imu_trip"]=pet_imu::name(imuGuard.lastTrip());d["imu_trips"]=imuGuard.trips();
  d["pet_buffered"]=lumiSkin?lumi.ready():legacyReady && face.bufferedFrameReady();d["pet_frames"]=lumiSkin?lumi.presented():face.presentedFrames();
  d["pet_max_present_us"]=lumiSkin?lumi.maxPresentUs():face.maxPresentMicros();
  d["free_heap"]=ESP.getFreeHeap();d["min_heap"]=ESP.getMinFreeHeap();d["free_psram"]=ESP.getFreePsram();
  d["largest_dma"]=heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
  statusWriter.send(d,Serial);maxTelemetryUs=std::max(maxTelemetryUs,uint32_t(micros()-telemetryAt));
}
void dumpFrame() {
  if(servoPowered || movement.enabled() || servoWarmup.pending()) {Serial.println("FRAME REFUSED motion must be OFF");return;}
  // Reads only this firmware's own display, not the camera or Mac screen.
  if(legacyReady && !lumiSkin)face.printFrameDiagnostics();
  Serial.printf("FRAME P6 %d %d\n",M5.Display.width(),M5.Display.height());
  uint8_t row[320*3];
  for(int y=0;y<M5.Display.height();++y) {
    M5.Display.readRectRGB(0,y,320,1,row);
    const uint32_t rowAt=millis();
    while(Serial.availableForWrite()<static_cast<int>(sizeof(row))) {
      if(uint32_t(millis()-rowAt)>2000){Serial.println("FRAME ABORT backpressure");return;}
      updateImu(millis());delay(1);
    }
    if(Serial.write(row,sizeof(row))!=sizeof(row)){Serial.println("FRAME ABORT partial");return;}
    updateImu(millis());delay(1);
  }
  Serial.println("\nFRAME END");
}
bool serialCommands() {
  // Yield to IMU/touch/STOP after one complete command. A byte cap alone can
  // dispatch eight status snapshots (8 * 16 bytes) before returning to loop.
  // The 2 ms intake budget does not preempt an individual command handler.
  const uint32_t serialStarted=micros();
  unsigned serialBudget=128;
  while(serialBudget-- && uint32_t(micros()-serialStarted)<2000 && Serial.available()>0) {
    const char ch=Serial.read();
    if(ch=='\n') {
      bool statusAttempted=false;
      JsonDocument d;
      if(!discardSerialLine && !deserializeJson(d,serialLine)) {
        const char* op=d["op"]|"";
        if(!strcmp(op,"status")){telemetry();statusAttempted=true;}
        else if(!strcmp(op,"motion_diag")){motionDiagnostics();statusAttempted=true;}
        else if(!strcmp(op,"power_diag_off")) {
          if(d["start"].is<bool>() && d["start"].as<bool>()) {
            ++powerSnapshotRequests;
            powerSnapshotAccepted=powerSnapshot.request(millis(),powerOffContext(),motionEnableAttempts);
          }
          powerDiagnostics();statusAttempted=true;
        }
        else if(!strcmp(op,"timing_reset")){maxLoopGap=0;imuGuard.resetTimingStats();maxBoardUs=maxInputsUs=maxRenderUs=maxSerialUs=maxTelemetryUs=0;}
        else if(!strcmp(op,"view"))switchPage(!strcmp(d["page"]|"","control")?Page::Control:Page::Pet);
        else if(!strcmp(op,"frame"))dumpFrame();
        else if(!strcmp(op,"preview") && d["state"].is<int>() && d["state"].as<int>()>=0 && d["state"].as<int>()<muse::kStates) {
          // Explicitly labelled visual gallery; entry cuts motor power and releases HID.
          galleryClip=d["state"].as<int>();galleryFrame=-1;
          if(d["frame"].is<int>() && d["frame"].as<int>()>=0 && d["frame"].as<int>()<8)galleryFrame=d["frame"].as<int>();
          galleryAt=millis();switchPage(Page::Gallery);
        }
        else if(!strcmp(op,"skin") && (!strcmp(d["name"]|"","lumi") || !strcmp(d["name"]|"","classic"))) {
          switchPage(Page::Pet);lumiSkin=!strcmp(d["name"]|"","lumi");petHudValid=false;
        }
        // No USB operation can pair, approve, inject fake host state, record or move motors.
      }
      serialLine="";discardSerialLine=false;
      return statusAttempted; // Leave subsequent bytes queued for the next loop.
    } else if(ch!='\r' && !discardSerialLine) {
      if(serialLine.length()<192)serialLine+=ch;else{serialLine="";discardSerialLine=true;}
    }
  }
  return false; // A partial line/discard state is retained across loop iterations.
}
}

void setup() {
  M5StackChan.begin();
  requestServoPower(false);
  M5.Mic.end();M5.Speaker.end();
  M5.Display.setBrightness(100);
  bootHealth.setPsram(true);bootHealth.setColorDepth(16);
  bootHealthAvailable=bootHealth.createSprite(runtime_health::kWidth,runtime_health::kHeight)!=nullptr;
  showBootHealth(runtime_health::Boot::Safe);
  showBootHealth(runtime_health::Boot::Audio);
  localAudioReady=local_audio::begin();
  showBootHealth(runtime_health::Boot::UsbInit);
  Serial.begin(115200);
  Serial.setTxBufferSize(snapshot_tx::Writer::kCapacity);Serial.setTxTimeoutMs(2);
  showBootHealth(runtime_health::Boot::Storage);
  fsReady=LittleFS.begin(false); // Never format existing flash implicitly.
  affection.begin(&preferences);
  Preferences servoSettings;storedServoNamespace=servoSettings.begin("servo",true);
  if(storedServoNamespace){storedYawZero=servoSettings.getInt("zero_pos_1",-1);storedPitchZero=servoSettings.getInt("zero_pos_2",-1);servoSettings.end();}
  face.setFrameOverlay(drawPetHud);
  lumi.setSafetyTick([]{updateImu(millis());});
  showBootHealth(runtime_health::Boot::Assets);
  if(!lumi.begin()) {
    // Resource allocation failure keeps the known original skin available.
    lumiSkin=false;face.begin();legacyReady=true;face.setAffectionState(affection.state());
  }
  showBootHealth(runtime_health::Boot::Canvas);
  controls.setPsram(true);controls.setColorDepth(16);
  canvasReady=controls.createSprite(320,240)!=nullptr;
  showBootHealth(runtime_health::Boot::Hid);
  hidReady=codex_hid::begin();
  input.requireRelease(); // A finger held across a reboot is not a new command.
  showBootHealth(runtime_health::Boot::UsbLog);
  Serial.printf("[boot] vibe-pet fs=%d hid=%d canvas=%d PSRAM=%u reset=%d\n",fsReady,hidReady,canvasReady,ESP.getPsramSize(),static_cast<int>(esp_reset_reason()));
  showBootHealth(runtime_health::Boot::UsbStatus);
  telemetry();
  showBootHealth(runtime_health::Boot::Loop);
  bootHealth.deleteSprite(); // No diagnostic display task or retained boot pixel buffer.
}
void loop() {
  snapshot_tx::Writer::service(Serial); // Also progress USB when no status is due.
  const uint32_t boardAt=micros();M5StackChan.update();maxBoardUs=std::max(maxBoardUs,uint32_t(micros()-boardAt));
  const uint32_t now=millis();
  if(loopCount)maxLoopGap=std::max(maxLoopGap,uint32_t(now-previousLoop));previousLoop=now;++loopCount;
  codex_hid::update(now);
  if(M5.BtnPWR.wasHold())setScreen(false);
  else if(M5.BtnPWR.wasClicked())setScreen(!screenOn);
  if(micOn && (!codex_hid::state().connected || uint32_t(now-micStarted)>60000)) {
    codex_hid::releaseInputs();micOn=false;
  }
  const uint32_t inputsAt=micros();updateImu(now);updateInput(now);updatePet(now);
  maxInputsUs=std::max(maxInputsUs,uint32_t(micros()-inputsAt));
  updateHostCue(now);updateMotion(now);
  updatePowerDiagnostics(); // At most one checked I2C read, only on explicit OFF request.
  updateImu(millis()); // Keep diagnostic and rendering costs in separate sample intervals.
  const bool wantsClassic=!lumiSkin && page==Page::Pet;
  if(wantsClassic!=classicRenderMode) {
    motionOff(pet_motion::StopReason::Page);
    if(classicRenderMode && legacyReady)presentation.update(face,false,false,false,false,-1,now);
    lumi.pauseForLegacy(wantsClassic);classicRenderMode=wantsClassic;
  }
  if(wantsClassic && !legacyReady) {
    motionOff(pet_motion::StopReason::Page);face.begin();legacyReady=true;face.setAffectionState(affection.state());
  }
  if(legacyReady && classicRenderMode)presentation.update(face,screenOn && page==Page::Pet && !lumiSkin,petting,shaking,lastStrokeLong,movement.gaze(),now);
  if(runtimeHealth.takeChanged(screenOn)) {
    lumi.requestFrame();if(legacyReady)face.requestFrame();
  }
  const uint32_t renderAt=micros();
  if(screenOn && uint32_t(now-lastRender)>=50) {
    lastRender=now;
    if(page==Page::Pet)drawPet(now);else if(page==Page::Gallery)drawGallery(now);
    else if(page==Page::Audio)drawAudio(now);else drawControl(now);
  }
  maxRenderUs=std::max(maxRenderUs,uint32_t(micros()-renderAt));
  // A complete LCD submission and USB diagnostics must not accumulate into one
  // unsampled interval. Always check actual time again after rendering.
  updateImu(millis());
  updateLights(millis());const uint32_t serialAt=micros();
  const bool statusAttempted=serialCommands();
  maxSerialUs=std::max(maxSerialUs,uint32_t(micros()-serialAt));
  updateImu(millis());
  // Requested and periodic snapshots share one opportunity per loop, including
  // backpressure failures. Use fresh time: the loop's earlier `now` may predate
  // the completed request and would underflow against a new lastTelemetry.
  const uint32_t telemetryNow=millis();
  if(statusAttempted)lastTelemetry=telemetryNow;
  else if(uint32_t(telemetryNow-lastTelemetry)>=5000){lastTelemetry=telemetryNow;telemetry();}
  delay(3);
  const uint32_t completedAt=millis();
  runtimeHealth.complete(completedAt,loopCount,imuGuard.samples(),imuGuard.lastGapMs(),
                         imuGuard.gapCount(),imuGuard.ready(completedAt));
}
