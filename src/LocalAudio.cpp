#include "LocalAudio.h"
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

namespace local_audio {
namespace {
std::atomic<bool> allowed{false},cancelled{false},available{false},working{false};
std::atomic<Command> pending{Command::None};
std::atomic<uint32_t> cancelledAt{0};
bool permitted(){return allowed.load()&&!cancelled.load();}
class Driver {
  bool micOwned=false,speakerOwned=false;
 public:
  bool startMic() {
    if(!permitted() || M5.Speaker.isRunning())return false;
    micOwned=true;
    if(!M5.Mic.begin())return false;
    uint8_t reset=0,gain1=0,gain2=0,format=0;
    // Verify actual codec ACK/configuration; callback success alone is insufficient.
    return permitted() && M5.Mic.isRunning() &&
      M5.In_I2C.readRegister(0x40,0x00,&reset,1,400000) && reset==0x41 &&
      M5.In_I2C.readRegister(0x40,0x43,&gain1,1,400000) && gain1==0x1b &&
      M5.In_I2C.readRegister(0x40,0x44,&gain2,1,400000) && gain2==0x1b &&
      M5.In_I2C.readRegister(0x40,0x11,&format,1,400000) && format==0x60;
  }
  void stopMic(){if(micOwned){M5.Mic.end();micOwned=false;}}
  bool record(int16_t* pcm,size_t n,uint32_t rate){return permitted() && M5.Mic.record(pcm,n,rate,false);}
  size_t remaining(){return M5.Mic.pendingSamples();}
  bool captureFault(){return M5.Mic.captureFailed();}
  bool startSpeaker(uint8_t volume) {
    if(!permitted() || M5.Mic.isRunning())return false;
    speakerOwned=true;
    M5.Speaker.setVolume(volume);M5.Speaker.setAllChannelVolume(255);
    if(!M5.Speaker.begin())return false;
    uint8_t status[2]={};
    return permitted() && M5.Speaker.isRunning() && M5.In_I2C.readRegister(0x36,0x04,status,2,400000) &&
      status[0]==0x40 && status[1]==0x40;
  }
  void stopSpeaker(){if(speakerOwned){M5.Speaker.end();speakerOwned=false;}}
  bool tone(uint32_t frequency,uint32_t duration){return permitted() && M5.Speaker.tone(frequency,duration,0,false);}
  bool play(const int16_t* pcm,size_t n,uint32_t rate){return permitted() && M5.Speaker.playRaw(pcm,n,rate,false,1,0,false);}
  bool playing(){return M5.Speaker.isPlaying();}
};
portMUX_TYPE snapshotLock=portMUX_INITIALIZER_UNLOCKED;
Snapshot published;
int16_t* recording=nullptr;
void publish(const Snapshot& value) {
  portENTER_CRITICAL(&snapshotLock);published=value;portEXIT_CRITICAL(&snapshotLock);
}
void worker(void*) {
  Driver driver;Session<Driver> session(driver,recording);
  uint32_t lastStopMs=0;
  publish(session.state());
  for(;;) {
    if(cancelled.load() || (!allowed.load() && (active(session.state().phase)||session.state().ready))) {
      // Publish the busy phase before consuming its request flag. busy() reads
      // the phase last, so a caller cannot see an idle gap during this handoff.
      working.store(true);
      auto value=session.state();value.phase=Phase::Stopping;publish(value);
      cancelled.exchange(false);pending.store(Command::None);
      session.stop();value=session.state();lastStopMs=millis()-cancelledAt.load();value.stopMs=lastStopMs;publish(value);
      working.store(false);
    } else {
      Command command=Command::None;
      if(pending.load()!=Command::None) {
        working.store(true);auto value=session.state();value.phase=Phase::Starting;publish(value);
        command=pending.exchange(Command::None);
      }
      // If a request arrives after the load above, leave it pending until the
      // next iteration instead of consuming it without publishing Starting.
      if(command!=Command::None && allowed.load()) {
        session.request(command,millis());
      }
      session.tick(millis());auto value=session.state();value.stopMs=lastStopMs;publish(value);working.store(active(value.phase));
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
}
bool begin() {
  // _begin_audio at M5.begin installed callbacks/configuration, not active drivers.
  const auto mic=M5.Mic.config();const auto spk=M5.Speaker.config();
  if(M5.getBoard()!=m5::board_t::board_M5StackChan || mic.pin_data_in!=14 ||
     spk.pin_data_out!=13 || mic.i2s_port!=I2S_NUM_1 || spk.i2s_port!=I2S_NUM_1)return false;
  auto config=mic;config.sample_rate=kRate;config.over_sampling=1;
  // Preserve the board's two-channel codec path and unity sample conversion
  // (magnification / (oversampling*2)); do not blindly boost input/noise.
  config.input_channel=m5::input_channel_t::input_stereo;
  config.magnification=2;config.noise_filter_level=0;
  config.dma_buf_len=128;config.dma_buf_count=4;config.task_pinned_core=0;
  M5.Mic.config(config);
  auto speaker=spk;speaker.sample_rate=kRate;speaker.dma_buf_len=128;
  speaker.dma_buf_count=4;speaker.task_pinned_core=0;M5.Speaker.config(speaker);
  recording=static_cast<int16_t*>(heap_caps_calloc(kSamples,sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!recording)return false;
  TaskHandle_t task=nullptr;
  if(xTaskCreatePinnedToCore(worker,"local_audio",4096,nullptr,2,&task,0)!=pdPASS) {
    heap_caps_free(recording);recording=nullptr;return false;
  }
  available.store(true);return true;
}
void allowPage(bool value){allowed.store(value);if(!value)stop();}
void stop(){if(!available.load())return;cancelledAt.store(millis());cancelled.store(true);}
bool request(Command command) {
  // CLEAR has the same cancellation/erase semantics as STOP, including during
  // preparation or active audio. Do not enqueue it behind the job it must cancel.
  if(command==Command::Clear) {
    if(!allowed.load() || !available.load())return false;
    stop();return true;
  }
  if(!allowed.load()||!available.load()||cancelled.load()||busy())return false;
  Command empty=Command::None;return pending.compare_exchange_strong(empty,command);
}
bool busy(){return working.load() || cancelled.load() || pending.load()!=Command::None || active(snapshot().phase);}
Snapshot snapshot(){portENTER_CRITICAL(&snapshotLock);auto result=published;portEXIT_CRITICAL(&snapshotLock);return result;}
Diagnostics diagnostics(){return {allowed.load(),cancelled.load(),working.load(),pending.load()};}
} // namespace local_audio
