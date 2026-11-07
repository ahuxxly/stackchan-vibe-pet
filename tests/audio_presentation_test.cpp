#include "AudioPresentation.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace local_audio;
int main(){
 Snapshot s;s.available=true;auto p=presentation(s);assert(!std::strcmp(p.title,"MIC OFF"));
 s.phase=Phase::Preparing;s.prepareRemainingMs=1000;p=presentation(s);
 assert(!std::strcmp(p.title,"GET READY 1")&&!p.recording&&std::strstr(p.detail,"MIC OFF"));
 s.prepareRemainingMs=1;p=presentation(s);assert(!std::strcmp(p.title,"GET READY 1"));
 s.phase=Phase::Recording;s.mic=true;p=presentation(s);
 assert(!std::strcmp(p.title,"SPEAK NOW")&&p.recording&&!std::strcmp(p.detail,"RECORDING 5.0s LEFT"));
 s.samples=16000;p=presentation(s);assert(!std::strcmp(p.detail,"RECORDING 4.0s LEFT"));
 s.samples=79680;p=presentation(s);assert(!std::strcmp(p.detail,"RECORDING 0.0s LEFT"));
 s.samples=90000;p=presentation(s);assert(!std::strchr(p.detail,'-'));
 s.phase=Phase::Ready;s.mic=false;s.samples=80000;s.ready=true;s.peak=7603;s.rms=865.33;p=presentation(s);
 assert(!std::strcmp(p.title,"READY TO PLAY")&&std::strstr(p.detail,"5.0s")&&std::strstr(p.hint,"MIC OFF"));
 s.phase=Phase::Playing;p=presentation(s);assert(std::strstr(p.detail,"80/255")&&!p.recording);
 s.phase=Phase::Error;s.error=Error::Timeout;p=presentation(s);assert(std::strstr(p.title,"timeout"));
 for(auto phase:{Phase::Off,Phase::Starting,Phase::Preparing,Phase::Tone,Phase::Recording,Phase::Ready,Phase::Playing,Phase::Stopping,Phase::Error}){
  s.phase=phase;s.peak=32768;s.rms=32768;s.clipped=80000;p=presentation(s);
  assert(std::strlen(p.title)<=28&&std::strlen(p.detail)<=50&&std::strlen(p.hint)<=50);
 }
 puts("PASS actual audio presentation: preparation keeps MIC OFF, SPEAK NOW with5to0 countdown, ready/play distinct, bounded320px text, error visibility");
}
