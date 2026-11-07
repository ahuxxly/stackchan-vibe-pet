#pragma once
#include "LocalAudioPolicy.h"
#include <cstdio>

namespace local_audio {
struct Presentation {
  char title[64]{},detail[64]{},hint[64]{};
  bool recording=false;
};
inline Presentation presentation(const Snapshot& s) {
  Presentation p;
  if(!s.available) {
    std::snprintf(p.title,sizeof(p.title),"AUDIO UNAVAILABLE");
    std::snprintf(p.hint,sizeof(p.hint),"MIC OFF | go BACK");return p;
  }
  switch(s.phase) {
    case Phase::Preparing:
      std::snprintf(p.title,sizeof(p.title),"GET READY %u",unsigned((s.prepareRemainingMs+999)/1000));
      std::snprintf(p.detail,sizeof(p.detail),"MIC OFF | wait for SPEAK NOW");
      std::snprintf(p.hint,sizeof(p.hint),"5 seconds to speak | STOP cancels");break;
    case Phase::Recording:
      p.recording=true;
      std::snprintf(p.title,sizeof(p.title),"SPEAK NOW");
      std::snprintf(p.detail,sizeof(p.detail),"RECORDING %.1fs LEFT",double(kSamples-std::min(s.samples,kSamples))/kRate);
      std::snprintf(p.hint,sizeof(p.hint),"MIC ON | STOP cancels and erases");break;
    case Phase::Ready:
      std::snprintf(p.title,sizeof(p.title),"READY TO PLAY");
      std::snprintf(p.detail,sizeof(p.detail),"%.1fs | peak %u RMS %.0f clip %u",double(s.samples)/kRate,unsigned(s.peak),double(s.rms),unsigned(s.clipped));
      std::snprintf(p.hint,sizeof(p.hint),"MIC OFF | PLAY ONCE to listen");break;
    case Phase::Playing:
      std::snprintf(p.title,sizeof(p.title),"PLAYING");
      std::snprintf(p.detail,sizeof(p.detail),"Playback volume %u/255",unsigned(kPlaybackVolume));
      std::snprintf(p.hint,sizeof(p.hint),"MIC OFF | STOP stops and erases");break;
    case Phase::Error:
      std::snprintf(p.title,sizeof(p.title),"ERROR: %s",name(s.error));
      std::snprintf(p.hint,sizeof(p.hint),"MIC OFF | STOP clears error");break;
    case Phase::Tone:
      std::snprintf(p.title,sizeof(p.title),"SHORT TONE");
      std::snprintf(p.hint,sizeof(p.hint),"MIC OFF | quiet 150ms tone");break;
    case Phase::Starting:case Phase::Stopping:
      std::snprintf(p.title,sizeof(p.title),"%s",name(s.phase));
      std::snprintf(p.hint,sizeof(p.hint),"Please wait | STOP cancels");break;
    default:
      std::snprintf(p.title,sizeof(p.title),"MIC OFF");
      std::snprintf(p.detail,sizeof(p.detail),"5s local recording | no upload");
      std::snprintf(p.hint,sizeof(p.hint),"Hold REC, release; wait for SPEAK NOW");break;
  }
  return p;
}
} // namespace local_audio
