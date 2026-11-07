#pragma once
#include <cstdint>

// Real firmware and host fake renderer share this coordinator. It has no HID
// or motor access. Shake uses upstream's ordinary shake animation, not the
// guruguru-only API whose precondition was never enabled in 0.1.2.
namespace pet_presentation {
class Coordinator {
 public:
  template<class Face> void update(Face& face,bool visible,bool petting,bool shaking,
                                   bool longPetting,int8_t gaze,uint32_t now) {
    face.setEnabled(visible);
    const bool nextShake=visible && shaking;
    const bool nextPet=visible && petting && !nextShake;
    const int8_t nextGaze=visible && !nextPet && !nextShake ? gaze : -1;
    face.setGazeDirection(nextGaze);
    if(pet_ && !nextPet)face.setPetFaceMode(false,now,visible && !nextShake,longPetting);
    if(shake_!=nextShake)face.setShakeFaceMode(nextShake);
    if(nextPet && !pet_)face.setPetFaceMode(true,now,true);
    pet_=nextPet;shake_=nextShake;
  }
 private:
  bool pet_=false,shake_=false;
};
} // namespace pet_presentation
