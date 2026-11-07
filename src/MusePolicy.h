#pragma once
#include "CompanionCore.h"
#include <algorithm>

// Visual-only policy. No access to hardware, input dispatch, microphones or HID.
namespace muse {
constexpr uint8_t kStates=18,kFrames=8;
constexpr uint32_t kFrameMs=125;
enum class Clip:uint8_t {Idle,Blink,Look,Listen,Think,Focus,Wait,Celebrate,Blocked,
  Disconnected,Cached,Sleep,Pet,Dizzy,Dance,Stretch,Coffee,Welcome};
enum class Playback:uint8_t {Loop,Pingpong,Once};
inline const char* name(Clip c) {
  static const char* names[]={"idle","blink","look","listen","think","focus","wait",
    "celebrate","blocked","disconnected","cached","sleep","pet","dizzy","dance",
    "stretch","coffee","welcome"};
  return names[static_cast<uint8_t>(c)<kStates?static_cast<uint8_t>(c):0];
}
inline Playback playback(Clip c) {
  switch(c) {
    case Clip::Blink:case Clip::Celebrate:case Clip::Dizzy:case Clip::Welcome:return Playback::Once;
    case Clip::Look:case Clip::Listen:case Clip::Think:case Clip::Pet:case Clip::Dance:return Playback::Loop;
    default:return Playback::Pingpong;
  }
}
inline uint8_t frame(Clip c,uint32_t age,bool preview=false) {
  uint32_t tick=age/kFrameMs;
  switch(playback(c)) {
    case Playback::Loop:return tick%kFrames;
    case Playback::Pingpong:tick%=2*kFrames-2;return tick<kFrames?tick:2*kFrames-2-tick;
    case Playback::Once:
      if(preview)tick%=kFrames+8; // Hold the last pose for one second in the gallery.
      return std::min<uint32_t>(tick,kFrames-1);
  }
  return 0;
}
inline uint8_t gazeFrame(int8_t gaze) {
  // Pixel-reviewed look sheet: 2 looks viewer-left, 5 viewer-right, 0 centre.
  return gaze==0?5:gaze==8?2:0;
}
enum class Host:uint8_t {Unknown,Cached,Idle,Work,Wait,Done,Blocked};
inline Host host(const companion::State& s,int selected,uint32_t now) {
  if(!s.displayKnown(selected,now))return Host::Unknown;
  if(!s.fresh(selected,now))return Host::Cached;
  if(s.awaiting(selected,now))return Host::Wait;
  const auto& a=s.agents[selected];
  if(!a.effect || a.brightness<=.02f)return Host::Idle;
  const int r=(a.color>>16)&255,g=(a.color>>8)&255,b=a.color&255;
  // Read only the host's existing lighting cue. Never infer tool calls or text.
  if(b>r+40 && b>g+20)return Host::Work;
  if(g>r+40 && g>b+20)return Host::Done;
  if(r>g+70 && r>b+70)return Host::Blocked;
  return Host::Idle; // An unrecognised colour is not invented task progress.
}
struct Local {bool pet=false,dizzy=false,dance=false;int8_t gaze=-1;};
class Director {
 public:
  Clip update(const companion::State& s,int selected,const Local& local,uint32_t now) {
    const Host nextHost=host(s,selected,now);
    if(!started_){started_=true;bootAt_=hostAt_=clipAt_=now;host_=nextHost;slot_=selected;epoch_=s.generation;}
    if(nextHost!=host_ || selected!=slot_ || epoch_!=s.generation) {
      host_=nextHost;slot_=selected;epoch_=s.generation;hostAt_=now;
    }
    Clip next=Clip::Idle;
    if(local.dizzy)next=Clip::Dizzy;
    else if(local.pet)next=Clip::Pet;
    else if(local.dance)next=Clip::Dance;
    else if(local.gaze>=0)next=Clip::Look;
    else if(host_==Host::Wait)next=Clip::Wait;
    else if(host_==Host::Blocked)next=Clip::Blocked;
    else if(host_==Host::Cached)next=Clip::Cached;
    else if(host_==Host::Work)next=uint32_t(now-hostAt_)<2000?Clip::Think:Clip::Focus;
    else if(host_==Host::Done && uint32_t(now-hostAt_)<2000)next=Clip::Celebrate;
    else if(uint32_t(now-bootAt_)<1500)next=Clip::Welcome;
    else if(host_==Host::Unknown)next=Clip::Disconnected;
    else if(uint32_t(now-bootAt_)%7000>=6000)next=Clip::Blink;
    if(next!=clip_){clip_=next;clipAt_=now;}
    return clip_;
  }
  Clip clip()const{return clip_;}
  uint32_t age(uint32_t now)const{return uint32_t(now-clipAt_);}
 private:
  bool started_=false;Host host_=Host::Unknown;Clip clip_=Clip::Welcome;
  uint32_t bootAt_=0,hostAt_=0,clipAt_=0,epoch_=0;int slot_=0;
};
inline uint32_t crc32(const uint8_t* bytes,size_t size,uint32_t crc=0xffffffffU) {
  for(size_t i=0;i<size;++i){crc^=bytes[i];for(int b=0;b<8;++b)crc=(crc>>1)^(0xedb88320U & (0U-(crc&1)));}
  return crc;
}
} // namespace muse
