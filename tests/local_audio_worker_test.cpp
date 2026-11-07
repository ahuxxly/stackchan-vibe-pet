#include "LocalAudio.h"
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <cassert>
#include <cstdio>
using namespace local_audio;
template<class Predicate> void waitFor(Predicate predicate,unsigned deadline=8500){
 const unsigned start=millis();while(!predicate()){assert(millis()-start<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
int main(){
 failAudioMemory=true;assert(!begin());failAudioMemory=false;
 failWorker=true;assert(!begin() && !audioTestMemory);failWorker=false;
 assert(begin());assert(audioTestBytes==160000);waitFor([]{return snapshot().available;});
 assert(micCalls==0&&speakerCalls==0);assert(!request(Command::Record));
 allowPage(true);assert(request(Command::Tone));waitFor([]{return toneCalls==1;});
 assert(!request(Command::Record));waitFor([]{return !busy();});assert(owner==0);
 assert(request(Command::Record));waitFor([]{return snapshot().phase==Phase::Preparing;});
 const int beforePrepare=micCalls;std::this_thread::sleep_for(std::chrono::milliseconds(400));
 assert(micCalls==beforePrepare && owner==0 && !snapshot().mic);
 waitFor([]{return snapshot().ready && !busy();});
 assert(snapshot().samples==80000 && snapshot().rms==1000 && recordCalls==250);
 assert(owner==0&&playCalls==0);assert(request(Command::Play));waitFor([]{return playCalls==1;});
 stop();waitFor([]{return !busy();});assert(!snapshot().ready && owner==0);
 for(int i=0;i<80000;++i)assert(static_cast<int16_t*>(audioTestMemory)[i]==0);
 // Exit during preparation must cancel without ever starting the mic.
 int beforePrepareCancel=micCalls;assert(request(Command::Record));
 waitFor([]{return snapshot().phase==Phase::Preparing;});allowPage(false);
 waitFor([]{return !busy();});assert(micCalls==beforePrepareCancel && owner==0);
 allowPage(true);
 // A 250ms codec start cannot block caller/safety polling; exit during startup
 // must prevent recording from being queued after the late begin returns.
 micDelay=250;int captures=recordCalls;int micStarts=micCalls;
 assert(request(Command::Record));waitFor([&]{return micCalls>micStarts;});
 auto start=millis();unsigned safetyTicks=0;
 while(millis()-start<100){snapshot();++safetyTicks;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(safetyTicks>30);allowPage(false);waitFor([]{return !busy();});
 assert(recordCalls==captures && owner==0&&!snapshot().ready);micDelay=0;
 // No tone queued when screen is switched off during amplifier startup.
 allowPage(true);speakerDelay=250;int tones=toneCalls;int speakers=speakerCalls;
 assert(request(Command::Tone));waitFor([&]{return speakerCalls>speakers;});allowPage(false);
 waitFor([]{return !busy();});assert(toneCalls==tones && owner==0);speakerDelay=0;
 // Readback rejects second-channel gain or codec format drift as well.
 for(int reg:{0x44,0x11}){badCodecReg=reg;allowPage(true);auto next=snapshot().sequence+1;assert(request(Command::Record));
  waitFor([&]{return snapshot().sequence==next && snapshot().phase==Phase::Error && !busy();});assert(owner==0&&snapshot().error==Error::MicStart);
 }
 badCodecReg=-1;
 // Codec ACK failure exposes error and releases the one owned port.
 codecOK=false;allowPage(true);const auto nextError=snapshot().sequence+1;assert(request(Command::Record));
 waitFor([&]{return snapshot().sequence==nextError && snapshot().phase==Phase::Error && !busy();});
 assert(snapshot().error==Error::MicStart && owner==0);codecOK=true;
 assert(request(Command::Record));waitFor([]{return snapshot().phase==Phase::Recording;});
 const auto stopAt=millis();assert(request(Command::Clear));waitFor([]{return !busy();});assert(millis()-stopAt<100);
 assert(owner==0&&!snapshot().ready);allowPage(false);waitFor([]{return !busy();});
 auto diagnosticsNow=diagnostics();assert(!diagnosticsNow.allowed&&!diagnosticsNow.cancelPending&&!diagnosticsNow.working&&diagnosticsNow.pending==Command::None);
 // Once busy() reports completion, a closed idle page must not become busy
 // again as the same cancellation is claimed. Keep the original check above.
 for(int i=0;i<200;++i){
  allowPage(false);waitFor([]{return !busy();});const auto d=diagnostics();
  assert(!d.allowed&&!d.cancelPending&&!d.working&&d.pending==Command::None);
  assert(owner==0&&!snapshot().ready);
 }
 audioTestEnd=true;audioTestThread.join();heap_caps_free(audioTestMemory);
 std::printf("PASS actual LocalAudio.cpp worker: quiet/allocation/task failure,one-owner,real scheduling250chunks,explicitplay,stop/erase,retry,codecACKfailure,250ms start isolation(%u safety polls),late-start cancellation,200 repeated idle-cancel completion checks; fake codecs/RTOS only\n",safetyTicks);
}
