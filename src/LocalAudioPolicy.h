#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Production state machine. Driver calls run ONLY on the local audio worker.
// No network, storage, HID, motor, or implicit playback path exists here.
namespace local_audio {
constexpr uint32_t kRate=16000, kRecordMs=5000, kPrepareMs=1000;
constexpr uint32_t kSamples=kRate*kRecordMs/1000, kChunk=320;
constexpr size_t kPcmBytes=kSamples*sizeof(int16_t);
constexpr uint32_t kToneMs=150, kPrepareDeadlineMs=2500;
constexpr uint32_t kRecordDeadlineMs=kRecordMs+1500, kPlayDeadlineMs=kRecordMs+1500;
// M5's master volume is squared. Keep the already-audible short tone quiet;
// raise speech one step from the user-validated64 to80; PCM/mic gain unchanged.
constexpr uint8_t kToneVolume=24, kPlaybackVolume=80;
enum class Command { None, Tone, Record, Play, Clear };
enum class Phase { Off, Starting, Preparing, Tone, Recording, Ready, Playing, Stopping, Error };
enum class Error { None, Memory, SpeakerStart, Playback, MicStart, Capture, Timeout, Cancelled };
inline const char* name(Phase p) {
  switch(p){case Phase::Off:return "OFF";case Phase::Starting:return "STARTING";
    case Phase::Preparing:return "PREPARING";case Phase::Tone:return "TONE";case Phase::Recording:return "RECORDING";
    case Phase::Ready:return "READY";case Phase::Playing:return "PLAYING";
    case Phase::Stopping:return "STOPPING";case Phase::Error:return "ERROR";}
  return "ERROR";
}
inline const char* name(Error e) {
  switch(e){case Error::None:return "none";case Error::Memory:return "memory";
    case Error::SpeakerStart:return "speaker_start";case Error::Playback:return "playback";
    case Error::MicStart:return "mic_start";case Error::Capture:return "capture";
    case Error::Timeout:return "timeout";case Error::Cancelled:return "stopped";}
  return "unknown";
}
struct Snapshot {
  Phase phase=Phase::Off;Error error=Error::None;
  uint32_t samples=0,peak=0,clipped=0,elapsedMs=0,sequence=0,stopMs=0,prepareRemainingMs=0;
  float rms=0,dc=0,acRms=0;
  bool ready=false,mic=false,speaker=false,available=false;
};
inline bool active(Phase p) {
  return p==Phase::Starting || p==Phase::Preparing || p==Phase::Tone || p==Phase::Recording ||
         p==Phase::Playing || p==Phase::Stopping;
}
template<class Driver> class Session {
 public:
  Session(Driver& driver,int16_t* pcm):driver_(driver),pcm_(pcm){state_.available=pcm!=nullptr;}
  const Snapshot& state()const{return state_;}
  bool request(Command command,uint32_t now) {
    if(command==Command::Clear){stop();return true;}
    if(active(state_.phase))return false;
    if(command==Command::None)return false;
    if(!pcm_){fail(Error::Memory);return false;}
    if(command==Command::Play && !state_.ready)return false;
    state_.error=Error::None;state_.elapsedMs=0;started_=now;++state_.sequence;
    state_.phase=Phase::Starting;
    if(command==Command::Record) {
      erase(); // only one deliberate, bounded capture; discard previous recording
      // Preparation is visual and silent: neither codec is started yet.
      state_.prepareRemainingMs=kPrepareMs;state_.phase=Phase::Preparing;
      return true;
    }
    owner_=Owner::Speaker;
    if(!driver_.startSpeaker(command==Command::Tone?kToneVolume:kPlaybackVolume)){fail(Error::SpeakerStart);return false;}
    state_.speaker=true;
    const bool ok=command==Command::Tone?driver_.tone(660,kToneMs):driver_.play(pcm_,state_.samples,kRate);
    if(!ok || !driver_.playing()){fail(Error::Playback);return false;}
    state_.phase=command==Command::Tone?Phase::Tone:Phase::Playing;
    return true;
  }
  void tick(uint32_t now) {
    if(!active(state_.phase))return;
    state_.elapsedMs=uint32_t(now-started_);
    if(state_.phase==Phase::Preparing) {
      if(state_.elapsedMs>=kPrepareDeadlineMs){fail(Error::Timeout);return;}
      state_.prepareRemainingMs=state_.elapsedMs<kPrepareMs?kPrepareMs-state_.elapsedMs:0;
      if(state_.prepareRemainingMs)return;
      owner_=Owner::Mic;state_.phase=Phase::Starting;
      if(!driver_.startMic()){fail(Error::MicStart);return;}
      state_.mic=true;state_.phase=Phase::Recording;
      started_=now;state_.elapsedMs=0;queueChunk();return;
    }
    if(state_.phase==Phase::Recording) {
      if(driver_.captureFault()){fail(Error::Capture);return;}
      if(state_.elapsedMs>=kRecordDeadlineMs){fail(Error::Timeout);return;}
      if(driver_.remaining())return;
      // Driver completion is an acquire/release barrier for the PCM writes.
      state_.samples+=queued_;
      queued_=0;
      if(state_.samples<kSamples){queueChunk();return;}
      endOwner();measure();state_.ready=true;state_.phase=Phase::Ready;return;
    }
    const uint32_t limit=state_.phase==Phase::Tone?600:kPlayDeadlineMs;
    if(state_.elapsedMs>=limit){fail(Error::Timeout);return;}
    if(!driver_.playing()){endOwner();state_.phase=state_.ready?Phase::Ready:Phase::Off;}
  }
  void stop() {
    state_.phase=Phase::Stopping;endOwner();erase();
    state_.phase=Phase::Off;state_.error=Error::Cancelled;++state_.sequence;
  }
 private:
  enum class Owner { None, Mic, Speaker };
  Driver& driver_;int16_t* pcm_;Snapshot state_;Owner owner_=Owner::None;
  uint32_t started_=0,queued_=0;
  void endOwner() {
    // Never call idle Speaker.end() while Mic owns the shared I2S1 port.
    if(owner_==Owner::Mic)driver_.stopMic();
    else if(owner_==Owner::Speaker)driver_.stopSpeaker();
    owner_=Owner::None;state_.mic=state_.speaker=false;
  }
  void erase() {
    if(pcm_)std::memset(pcm_,0,kSamples*sizeof(int16_t));
    queued_=state_.samples=state_.peak=state_.clipped=state_.prepareRemainingMs=0;
    state_.rms=state_.dc=state_.acRms=0;state_.ready=false;
  }
  void fail(Error error) {
    endOwner();erase();state_.phase=Phase::Error;state_.error=error;
  }
  bool queueChunk() {
    queued_=std::min(kChunk,kSamples-state_.samples);
    if(!driver_.record(pcm_+state_.samples,queued_,kRate)){fail(Error::Capture);return false;}
    return true;
  }
  void measure() {
    uint64_t sum=0;int64_t signedSum=0;
    for(size_t i=0;i<state_.samples;++i) {
      const int32_t value=pcm_[i];const uint32_t mag=value<0?-value:value;
      state_.peak=std::max(state_.peak,mag);sum+=uint64_t(mag)*mag;signedSum+=value;
      if(mag>=32751)++state_.clipped; // ES7210 driver saturates at INT16 limits minus16
    }
    state_.rms=state_.samples?std::sqrt(double(sum)/state_.samples):0;
    state_.dc=state_.samples?double(signedSum)/state_.samples:0;
    const double meanSquare=state_.samples?double(sum)/state_.samples:0;
    state_.acRms=std::sqrt(std::max(0.0,meanSquare-double(state_.dc)*state_.dc));
  }
};
} // namespace local_audio
