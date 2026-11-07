#pragma once
#include <M5Unified.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "MusePolicy.h"

// Worker owns JPEG decoding and its private pixel buffers. Only the main loop
// owns LCD composition. A cache miss never makes the main loop wait.
class MuseRenderer {
 public:
  using Overlay=void(*)(M5Canvas&);
  void setSafetyTick(void(*tick)()){safetyTick_=tick;}
  ~MuseRenderer(){end();}
  bool begin();
  void end();
  // Only call with motor OFF at an explicit legacy-skin transition.
  void pauseForLegacy(bool paused);
  void requestFrame(){dirty_=true;}
  void draw(muse::Clip clip,uint8_t frame,Overlay overlay);
  bool ready()const{return ready_;}
  bool healthy()const{return ready_ && failures_.load()==0;}
  uint32_t presented()const{return presented_;}
  uint32_t maxPresentUs()const{return maxPresentUs_;}
  uint32_t maxDrawUs()const{return maxDrawUs_;}
  uint32_t maxDecodeUs()const{return maxDecodeUs_.load();}
  uint32_t maxReadUs()const{return maxReadUs_;}
  uint32_t failures()const{return failures_.load();}
  uint32_t loads()const{return loads_.load();}
  uint32_t misses()const{return misses_;}
  bool pending()const{return requested_.load()!=lastId_;}
  int renderedState()const{return lastId_>=0?lastId_/8:-1;}
  int renderedFrame()const{return lastId_>=0?lastId_%8:-1;}
 private:
  static constexpr int kCount=muse::kStates*muse::kFrames,kSlots=8,kReading=1000;
  struct Slot {M5Canvas pixels;std::atomic<int> id{-1};}; // -2 writing; >=1000 reader owns it
  M5Canvas canvas_{&M5.Display},lastTile_,decode_;
  std::array<Slot,kSlots> slots_;
  uint8_t* encoded_=nullptr;
  std::array<uint32_t,kCount> offsets_{};
  std::array<std::atomic<bool>,kCount> failed_{};
  std::atomic<int> requested_{-1};
  std::atomic<bool> stopping_{false},running_{false},paused_{false},parked_{false};
  std::atomic<uint32_t> failures_{0},loads_{0},maxDecodeUs_{0};
  TaskHandle_t task_=nullptr;
  void(*safetyTick_)()=nullptr;
  int lastId_=-1;
  bool ready_=false,dirty_=true;
  uint32_t presented_=0,maxPresentUs_=0,maxDrawUs_=0,maxReadUs_=0,misses_=0,seenFailures_=0;
  static void workerEntry(void* self);
  void worker();
  bool cached(int id)const;
  int claimSlot(int wanted);
};
