#pragma once
// Hardware-independent policy used by the real firmware and host regression tests.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace companion {
constexpr uint32_t kLeaseMs = 45000;
// The installed host sends changed lighting only and polls device.status every
// 60 seconds. This is a separate, bounded host lease, not a content timestamp.
constexpr uint32_t kHostPollLeaseMs = 90000;
enum class Page { Pet, Control, Gallery, Audio };
enum class Intent {
  None, OpenControl, CloseControl, Agent0, Agent1, Agent2, Agent3, Agent4, Agent5,
  Approve, Reject, Send, Fast, Fork, Mic, Motion, Pair, Dance, Stop,
  OpenGallery, GalleryPrevious, GalleryNext, ToggleSkin,
  OpenAudio, CloseAudio, AudioTone, AudioRecord, AudioPlay, AudioClear
};
struct Agent {
  uint32_t color=0, receivedAt=0, revision=0;
  float brightness=0, speed=0;
  int effect=0;
  bool seen=false;
  uint8_t known=0;
};
class State {
 public:
  std::array<Agent,6> agents{};
  bool connected=false;
  uint32_t generation=0;
  bool hostPollSeen=false;
  uint32_t hostPollAt=0, hostPollCount=0;
  void connect(bool value) {
    if (value==connected) return;
    connected=value; ++generation;
    hostPollSeen=false;hostPollAt=0;
    for (auto& a:agents) a=Agent{};
  }
  bool hostLive(uint32_t now) const {
    return connected && hostPollSeen && uint32_t(now-hostPollAt)<kHostPollLeaseMs;
  }
  void receiveHostPoll(uint32_t now) {
    if(!connected)return;
    // Once evidence has expired, a later battery query cannot resurrect an old
    // snapshot. Require new content; also cancel any old held input context.
    bool invalidated=false;
    for(int i=0;i<6;++i)if(agents[i].known && !displayKnown(i,now)) {
      agents[i]=Agent{};invalidated=true;
    }
    if(invalidated)++generation;
    hostPollSeen=true;hostPollAt=now;++hostPollCount;
  }
  bool receive(int id,uint32_t color,float brightness,int effect,float speed,uint32_t now) {
    return receivePatch(id,15,color,brightness,effect,speed,now);
  }
  bool receivePatch(int id,uint8_t fields,uint32_t color,float brightness,int effect,float speed,uint32_t now) {
    if (!connected || id<0 || id>=6 || fields>15 ||
        ((fields&1) && color>0xffffff) ||
        ((fields&2) && (!std::isfinite(brightness) || brightness<0 || brightness>1)) ||
        ((fields&4) && (effect<0 || effect>16)) ||
        ((fields&8) && (!std::isfinite(speed) || speed<0 || speed>1))) return false;
    auto& a=agents[id];
    if(!(fields&1))color=a.color;
    if(!(fields&2))brightness=a.brightness;
    if(!(fields&4))effect=a.effect;
    if(!(fields&8))speed=a.speed;
    const bool changed=!fresh(id,now) || a.color!=color || a.brightness!=brightness ||
                       a.speed!=speed || a.effect!=effect;
    const uint32_t revision=a.revision+(changed?1:0);
    const uint8_t known=a.known|fields;
    // A partial first report may accumulate, but cannot invent missing color/intensity/effect.
    a={color,now,revision,brightness,speed,effect,(known&7)==7,known};
    return true;
  }
  bool fresh(int id,uint32_t now) const {
    return id>=0 && id<6 && connected && agents[id].seen &&
           uint32_t(now-agents[id].receivedAt)<kLeaseMs;
  }
  bool displayKnown(int id,uint32_t now) const {
    // A poll proves communication service liveness, NOT upstream state health.
    // These colors are explicitly labelled CACHED; never grant input authority.
    return id>=0 && id<6 && connected && agents[id].seen &&
           (fresh(id,now) || hostLive(now));
  }
  bool cached(int id,uint32_t now) const {
    return displayKnown(id,now) && !fresh(id,now);
  }
  bool approvalEligible(int id,uint32_t now) const {
    return awaiting(id,now) && uint32_t(now-agents[id].receivedAt)<kLeaseMs;
  }
  bool awaiting(int id,uint32_t now) const {
    if (!fresh(id,now)) return false;
    const auto& a=agents[id];
    const unsigned r=(a.color>>16)&255, g=(a.color>>8)&255, b=a.color&255;
    // Upstream host awaiting-* color, NOT a claim about the exact permission type.
    return a.effect!=0 && a.brightness>.02f && r>=220 && g>=70 && g<=150 && b<=48;
  }
};
struct Context { Page page; uint32_t generation; int selected; bool connected; bool awaiting; bool motionEnabled=false; bool motionReady=true; uint32_t hostEpoch=0,hostRevision=0; };
inline Context inputContext(Page page,uint32_t pageGeneration,const State& state,int selected,
                            bool motionEnabled,bool motionReady,uint32_t now) {
  // Local pages do not borrow the host's content revision or connection epoch.
  // Dance still cancels when the selected host enters an awaiting state.
  if(page!=Page::Control)return {page,pageGeneration,0,false,
    page==Page::Pet && state.awaiting(selected,now),motionEnabled,motionReady};
  // Keep identities separate: after reconnect, epoch+2 and revision-2 must not
  // collide with a held input from the previous session.
  return {page,pageGeneration,selected,state.connected,state.approvalEligible(selected,now),
    motionEnabled,motionReady,state.generation,state.agents[selected].revision};
}
inline Intent hit(int x,int y,const Context& c) {
  if (x<0 || x>=320 || y<0 || y>=240) return Intent::None;
  if(c.page==Page::Audio) {
    if(y>=3 && y<40 && x>=4 && x<76)return Intent::CloseAudio;
    if(y>=3 && y<40 && x>=88 && x<182)return Intent::Stop;
    if(y>=128 && y<172) {
      if(x>=8 && x<154)return Intent::AudioTone;
      if(x>=166 && x<312)return Intent::AudioRecord;
    }
    if(y>=184 && y<228) {
      if(x>=8 && x<154)return Intent::AudioPlay;
      if(x>=166 && x<312)return Intent::AudioClear;
    }
    return Intent::None;
  }
  // A visual preview has no route to host input, pairing, recording or motion.
  if(c.page==Page::Gallery) {
    if(y<40 && x<68)return Intent::CloseControl;
    if(y<40 && x>=88 && x<182)return Intent::Stop;
    if(y<40 && x>=220)return Intent::ToggleSkin;
    if(y>=190 && y<232 && x>=4 && x<64)return Intent::GalleryPrevious;
    if(y>=190 && y<232 && x>=256 && x<316)return Intent::GalleryNext;
    return Intent::None;
  }
  if (c.page==Page::Pet) {
    if(x>=254 && x<316 && y>=4 && y<48)return Intent::OpenControl;
    if(y>=4 && y<48) {
      if(x>=4 && x<82 && (c.motionEnabled || c.motionReady))return Intent::Motion;
      if(x>=88 && x<166 && c.motionEnabled && !c.awaiting)return Intent::Dance;
      if(x>=172 && x<244)return Intent::Stop;
    }
    return Intent::None;
  }
  if (x<60 && y<40) return Intent::CloseControl;
  if (x>=64 && x<126 && y<40) return Intent::Stop;
  if (x>=132 && x<214 && y<40) return Intent::OpenAudio;
  if (x>=248 && x<312 && y>=190 && y<232) return Intent::OpenGallery;
  if (x>=220 && y<40 && !c.connected) return Intent::Pair;
  if (!c.connected) return Intent::None;
  for (int row=0;row<2;++row) for (int col=0;col<3;++col) {
    if (x>=8+104*col && x<104+104*col && y>=48+40*row && y<84+40*row)
      return static_cast<Intent>(static_cast<int>(Intent::Agent0)+row*3+col);
  }
  if (y>=132 && y<180) {
    if (x>=8 && x<104 && c.awaiting) return Intent::Approve;
    if (x>=112 && x<208 && c.awaiting) return Intent::Reject;
    if (x>=216 && x<312) return Intent::Send;
  }
  if (y>=190 && y<232) {
    if (x>=8 && x<82) return Intent::Fast;
    if (x>=88 && x<162) return Intent::Fork;
    if (x>=168 && x<242) return Intent::Mic;
    if (x>=248 && x<312) return Intent::OpenGallery;
  }
  return Intent::None;
}
class TouchRouter {
 public:
  struct Feedback { Intent intent=Intent::None; uint32_t heldMs=0; const char* phase="idle"; };
  // Read-only UI/diagnostics. Reading feedback cannot emit or re-arm an input.
  Feedback feedback(uint32_t now,const Context& c) const {
    if(!down_ || candidate_==Intent::None)return {};
    const uint32_t age=now-startedAt_;
    if(!valid_)return {candidate_,age,"cancelled"};
    if(c.page!=start_.page || c.generation!=start_.generation || c.selected!=start_.selected ||
       c.hostEpoch!=start_.hostEpoch || c.hostRevision!=start_.hostRevision ||
       c.motionEnabled!=start_.motionEnabled || (candidate_==Intent::Motion && c.motionReady!=start_.motionReady) ||
       (awaitingRelevant() && c.awaiting!=start_.awaiting) || c.connected!=start_.connected)
      return {candidate_,age,"context_changed"};
    if(age>=10000)return {candidate_,age,"too_long"};
    const uint32_t threshold=(candidate_==Intent::Motion || candidate_==Intent::AudioRecord)?1000:
      (candidate_==Intent::Dance || candidate_==Intent::Approve || candidate_==Intent::Reject)?600:40;
    return {candidate_,age,age>=threshold?"release":"holding"};
  }
  Intent rear(bool,uint32_t) const { return Intent::None; }
  void cancel() { candidate_=Intent::None; valid_=false; }
  void requireRelease() {cancel();blocked_=true;}
  Intent screen(bool down,int x,int y,uint32_t now,const Context& c) {
    if(blocked_){down_=down;if(!down)blocked_=false;cancel();return Intent::None;}
    const Intent target=hit(x,y,c);
    if (down && !down_) {
      candidate_=target; start_=c; startedAt_=now; valid_=target!=Intent::None;
      // STOP is immediate on touch-down; it never waits for a release or hold.
      if(target==Intent::Stop){down_=true;cancel();return Intent::Stop;}
    } else if (down && down_ && target!=candidate_) valid_=false;
    Intent result=Intent::None;
    if (!down && down_ && valid_ && target==candidate_ && c.page==start_.page &&
        c.generation==start_.generation && c.selected==start_.selected &&
        c.hostEpoch==start_.hostEpoch && c.hostRevision==start_.hostRevision &&
        c.connected==start_.connected && (!awaitingRelevant() || c.awaiting==start_.awaiting) &&
        c.motionEnabled==start_.motionEnabled && (candidate_!=Intent::Motion || c.motionReady==start_.motionReady)) {
      const bool guarded=candidate_==Intent::Approve || candidate_==Intent::Reject;
      if ((!guarded || (c.awaiting && uint32_t(now-startedAt_)>=600)) &&
          (candidate_!=Intent::Motion || uint32_t(now-startedAt_)>=1000) &&
          (candidate_!=Intent::AudioRecord || uint32_t(now-startedAt_)>=1000) &&
          (candidate_!=Intent::Dance || uint32_t(now-startedAt_)>=600) &&
          uint32_t(now-startedAt_)>=40 && uint32_t(now-startedAt_)<10000) result=candidate_;
    }
    down_=down;
    if (!down) cancel();
    return result;
  }
 private:
  bool awaitingRelevant() const {
    return candidate_==Intent::Dance || candidate_==Intent::Approve || candidate_==Intent::Reject;
  }
  bool down_=false, valid_=false, blocked_=false;
  Intent candidate_=Intent::None;
  Context start_{Page::Pet,0,0,false,false};
  uint32_t startedAt_=0;
};
inline bool keyFrame(const char* key,bool pressed,uint8_t (&out)[63]) {
  if (!key) return false;
  const size_t n=std::strlen(key);
  bool valid=n==4 && key[0]=='A' && key[1]=='G' && key[2]=='0' && key[3]>='0' && key[3]<='5';
  if (n==5 && std::strncmp(key,"ACT",3)==0 && key[3]>='0' && key[3]<='1' && key[4]>='0' && key[4]<='9') {
    const int action=(key[3]-'0')*10+(key[4]-'0'); valid=action>=6 && action<=12;
  }
  if (!valid) return false;
  std::memset(out,0,63); out[0]=2;
  const int length=std::snprintf(reinterpret_cast<char*>(out+2),61,
    "{\"m\":\"v.oai.hid\",\"p\":{\"k\":\"%s\",\"act\":%u}}\r\n",key,pressed?1U:0U);
  if (length<=0 || length>=61) return false;
  out[1]=static_cast<uint8_t>(length); return true;
}
} // namespace companion
