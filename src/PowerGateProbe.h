#pragma once
#include <array>
#include <cstdint>

// Transaction success is not proof of GPIO level or VM voltage.
namespace power_probe {
enum class WriteResult : int {Unavailable=-1,Failed=0,Succeeded=1};
struct WriteRecord {
  bool sampled=false;WriteResult result=WriteResult::Unavailable;
  uint32_t started=0,finished=0,attempt=0;
};
struct WriteHistory {
  WriteRecord enable,disable;
  void record(bool on,int result,uint32_t started,uint32_t finished,uint32_t attempt) {
    (on?enable:disable)={true,static_cast<WriteResult>(result),started,finished,attempt};
  }
};

enum class State {Idle,Pending,Complete,ReadFailed,InputHigh,Cancelled,Expired};
inline const char* name(State state) {
  switch(state) {
    case State::Idle:return "idle";case State::Pending:return "pending";
    case State::Complete:return "complete";case State::ReadFailed:return "read_failed";
    case State::InputHigh:return "input_high";case State::Cancelled:return "cancelled";
    default:return "expired";
  }
}
struct Sample {int value=-1;bool valid=false;uint32_t started=0,finished=0;};
class OffSnapshot {
 public:
  static constexpr std::array<uint8_t,8> registers{0x07,0x03,0x05,0x09,0x0b,0x13,0x02,0x07};
  static constexpr uint32_t intervalMs=40,budgetMs=500,restartMs=5000;
  std::array<Sample,8> samples{};
  State state=State::Idle;unsigned index=0;uint32_t attempt=0,started=0;

  void cancel(){if(state==State::Pending)state=State::Cancelled;}

  // offContext means software motion/power request/warmup and audio are all OFF.
  // The first/last input samples independently check the actual register is LOW.
  bool request(uint32_t now,bool offContext,uint32_t attemptId) {
    if(!offContext || state==State::Pending || (everStarted_ && uint32_t(now-started)<restartMs))return false;
    samples={};index=0;attempt=attemptId;started=now;lastRead_=now;
    everStarted_=true;readBefore_=false;state=State::Pending;return true;
  }
  // Reader intentionally has ONLY readRegister; this type cannot command power.
  // One synchronous driver call cannot be preempted by this deadline. Integration
  // must measure its duration against the unchanged IMU/STOP constraints.
  template<class Reader,class Clock> void tick(Reader& reader,Clock clock,bool offContext,uint32_t currentAttempt) {
    if(state!=State::Pending)return;
    // An ON attempt may fail and return OFF between ticks; never mix that new
    // post-cutoff configuration into a snapshot begun before the attempt.
    if(!offContext || currentAttempt!=attempt){state=State::Cancelled;return;}
    const auto now=clock();
    if(uint32_t(now-started)>=budgetMs){state=State::Expired;return;}
    if(readBefore_ && uint32_t(now-lastRead_)<intervalMs)return;
    uint8_t value=0;auto& sample=samples[index];sample.started=now;
    const bool ok=reader.readRegister(registers[index],&value,1);
    sample.finished=clock();lastRead_=sample.finished;readBefore_=true;
    if(!ok){state=State::ReadFailed;return;} // bytes from a failed read remain unknown
    sample.valid=true;sample.value=value;
    if(uint32_t(sample.finished-started)>=budgetMs){state=State::Expired;return;}
    if((index==0 || index==7) && (value&1)){state=State::InputHigh;return;}
    if(++index==registers.size())state=State::Complete;
  }
 private:
  bool everStarted_=false,readBefore_=false;uint32_t lastRead_=0;
};
} // namespace power_probe
