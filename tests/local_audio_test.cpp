#include "LocalAudioPolicy.h"
#include "CompanionCore.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace local_audio;
struct Driver {
 int owner=0,starts=0,ends=0;bool startOK=true,playOK=true,queueOK=true,fault=false,playingNow=false;
 uint8_t volume=0;size_t pending=0;int16_t* destination=nullptr;size_t count=0;
 bool startMic(){assert(!owner);owner=1;++starts;return startOK;}
 void stopMic(){assert(owner==1);owner=0;pending=0;destination=nullptr;++ends;}
 bool startSpeaker(uint8_t v){assert(!owner);volume=v;owner=2;++starts;return startOK;}
 void stopSpeaker(){assert(owner==2);owner=0;playingNow=false;++ends;}
 bool tone(uint32_t hz,uint32_t ms){assert(owner==2 && volume==24 && hz==660 && ms==150);playingNow=playOK;return playOK;}
 bool play(const int16_t*,size_t n,uint32_t rate){assert(owner==2 && volume==80 && n==80000 && rate==16000);playingNow=playOK;return playOK;}
 bool playing(){return playingNow;}
 bool record(int16_t* p,size_t n,uint32_t rate){assert(owner==1 && !pending && rate==16000 && n==320);pending=n;destination=p;count=n;return queueOK;}
 size_t remaining(){return pending;}
 bool captureFault(){return fault;}
 void complete(int16_t positive=1000,int16_t negative=-1000){assert(destination && pending);for(size_t i=0;i<count;++i)destination[i]=(i%2)?positive:negative;pending=0;}
};
int main(){
 std::array<int16_t,kSamples> pcm{};Driver d;Session<Driver> s(d,pcm.data());
 assert(pcm.size()*sizeof(int16_t)==160000);
 assert(s.state().phase==Phase::Off && !s.state().ready && d.starts==0);
 assert(!s.request(Command::Play,0));assert(s.request(Command::Tone,0));
 assert(!s.request(Command::Record,20));d.playingNow=false;s.tick(151);
 assert(s.state().phase==Phase::Off && !d.owner && d.ends==1);
 int starts=d.starts;assert(s.request(Command::Record,1000));
 assert(s.state().phase==Phase::Preparing && !s.state().mic && !d.owner && d.starts==starts);
 s.tick(1999);assert(s.state().prepareRemainingMs==1 && d.starts==starts && !d.pending);
 s.tick(2000);assert(s.state().phase==Phase::Recording && s.state().mic && d.owner==1);
 s.tick(2001);assert(s.state().samples==0); // enqueue is not completion
 for(unsigned i=0;i<250;++i){d.complete();s.tick(2020+20*i);
   if(i==49)assert(s.state().samples==16000 && !s.state().ready && s.state().phase==Phase::Recording);
 }
 assert(s.state().phase==Phase::Ready && s.state().ready && s.state().samples==80000);
 assert(s.state().peak==1000 && s.state().rms==1000 && s.state().dc==0 && s.state().acRms==1000 && !s.state().clipped && !d.owner);
 starts=d.starts;s.tick(10000);assert(d.starts==starts); // no auto playback
 assert(s.request(Command::Play,11000));s.tick(15999);assert(s.state().phase==Phase::Playing);
 d.playingNow=false;s.tick(16001);assert(s.state().ready && !d.owner);s.stop();
 assert(!s.state().ready && !s.state().samples && !d.owner);
 for(auto v:pcm)assert(v==0);
 // STOP in preparation must never power a codec, including on later ticks.
 starts=d.starts;assert(s.request(Command::Record,20000));s.tick(20500);s.stop();s.tick(22000);
 assert(d.starts==starts && s.state().phase==Phase::Off && !s.state().prepareRemainingMs);
 assert(s.request(Command::Record,23000));s.tick(25500);
 assert(s.state().error==Error::Timeout && d.starts==starts); // no delayed surprise capture
 // Cancel pending recording and retry; no stale descriptor or leaked owner.
 assert(s.request(Command::Record,26000));s.tick(27000);assert(s.request(Command::Clear,27010));assert(!d.owner && !d.pending);
 assert(s.request(Command::Record,28000));s.tick(29000);d.fault=true;s.tick(29010);
 assert(s.state().phase==Phase::Error && s.state().error==Error::Capture && !d.owner);d.fault=false;
 assert(s.request(Command::Record,30000));s.tick(31000);s.tick(37500);
 assert(s.state().error==Error::Timeout && !d.owner && !s.state().ready);
 d.startOK=false;assert(!s.request(Command::Tone,40000));assert(s.state().error==Error::SpeakerStart && !d.owner);
 assert(s.request(Command::Record,41000));s.tick(42000);assert(s.state().error==Error::MicStart && !d.owner);d.startOK=true;
 d.playOK=false;assert(!s.request(Command::Tone,43000));assert(s.state().error==Error::Playback && !d.owner);d.playOK=true;
 d.queueOK=false;assert(s.request(Command::Record,44000));s.tick(45000);assert(s.state().error==Error::Capture && !d.owner);d.queueOK=true;
 assert(s.request(Command::Tone,0xfffffff0U));s.tick(0xfffffff0U+600U);
 assert(s.state().error==Error::Timeout && !d.owner);
 assert(s.request(Command::Record,0xfffffff0U));s.tick(0xfffffff0U+999U);assert(!d.owner);
 s.tick(0xfffffff0U+1000U);assert(s.state().phase==Phase::Recording);s.stop();
 Session<Driver> missing(d,nullptr);assert(!missing.request(Command::Record,0));assert(missing.state().error==Error::Memory);
 // Five-second clipping and silence: wide stats; no hidden normalization or auto-gain.
 for(bool silence:{false,true}) {
  assert(s.request(Command::Record,50000));s.tick(51000);
  for(unsigned i=0;i<250;++i){d.complete(silence?0:32751,silence?0:-32752);s.tick(51020+20*i);}
  assert(s.state().samples==80000 && s.state().ready);
  if(silence)assert(s.state().peak==0 && s.state().rms==0 && s.state().clipped==0);
  else assert(s.state().peak==32752 && s.state().clipped==80000 && s.state().rms>32751);
  if(!silence){assert(s.request(Command::Play,60000));s.tick(66500);assert(s.state().error==Error::Timeout && !d.owner);}
  s.stop();for(auto v:pcm)assert(v==0);
 }
 assert(s.request(Command::Record,70000));s.tick(71000);
 for(unsigned i=0;i<250;++i){d.complete(1000,1000);s.tick(71020+20*i);}
 assert(s.state().rms==1000 && s.state().dc==1000 && s.state().acRms==0);s.stop();
 assert(s.state().rms==0 && s.state().dc==0 && s.state().acRms==0);
 // Complete screen sweep proves local page cannot send host input or start motors.
 using namespace companion;Context c{Page::Audio,1,0,false,false,false};
 for(int y=0;y<240;++y)for(int x=0;x<320;++x){const auto hitResult=hit(x,y,c);
   assert(hitResult==Intent::None || hitResult==Intent::Stop || hitResult==Intent::CloseAudio ||
     hitResult==Intent::AudioTone || hitResult==Intent::AudioRecord || hitResult==Intent::AudioPlay || hitResult==Intent::AudioClear);}
 for(uint32_t duration:{39U,40U,999U,1000U,9999U,10000U}) {
  TouchRouter router;assert(router.screen(true,220,150,0,c)==Intent::None);
  assert(router.screen(true,220,150,duration,c)==Intent::None);
  assert(router.screen(false,220,150,duration,c)==(duration>=1000&&duration<10000?Intent::AudioRecord:Intent::None));
 }
 TouchRouter stop;assert(stop.screen(true,120,20,10,c)==Intent::Stop);
 TouchRouter cancel;cancel.screen(true,220,150,0,c);++c.generation;
 assert(cancel.screen(false,220,150,1100,c)==Intent::None);
 TouchRouter wake;wake.requireRelease();wake.screen(true,220,150,0,c);
 assert(wake.screen(false,220,150,1100,c)==Intent::None);
 puts("PASS production local audio session: half-duplex, explicit capture/play,250x20ms chunks, silentprepare, distinct speech level, finite timeout/wrap, failure/cancel/erase/retry, stats; local UI76800pixels,hold/STOP/context/wake guards (fake hardware)");
}
