#include "PetPresentation.h"
#include <cassert>
#include <cstdio>
struct Face {
  bool visible=true,pet=false,shake=false,longStroke=false,animatedEnd=false;
  int8_t gaze=-1;int petStarts=0,shakeStarts=0;
  void setEnabled(bool value){visible=value;}
  void setGazeDirection(int8_t value){gaze=value;}
  void setPetFaceMode(bool value,uint32_t,bool animate,bool longPetting=false) {
    pet=value;longStroke=longPetting;animatedEnd=animate;if(value)++petStarts;
  }
  void setShakeFaceMode(bool value){shake=value;if(value)++shakeStarts;}
};
int main() {
  pet_presentation::Coordinator coordinator;Face face;
  coordinator.update(face,true,false,false,false,8,0);assert(face.gaze==8 && !face.pet && !face.shake);
  coordinator.update(face,true,true,false,false,8,100);assert(face.pet && face.gaze==-1 && face.petStarts==1);
  coordinator.update(face,true,true,false,false,8,200);assert(face.petStarts==1);
  coordinator.update(face,true,false,false,true,-1,3500);assert(!face.pet && face.longStroke && face.animatedEnd);
  // Regression: the ordinary shake path must activate without guruguru mode.
  // Fake deliberately has no startGuruguruDizzyAnimation or guruguru mode API.
  coordinator.update(face,true,false,true,false,0,4000);assert(face.shake && face.gaze==-1 && face.shakeStarts==1);
  coordinator.update(face,true,false,true,false,0,4100);assert(face.shakeStarts==1);
  coordinator.update(face,true,false,false,false,-1,6200);assert(!face.shake);
  coordinator.update(face,true,true,false,false,-1,7000);assert(face.pet);
  coordinator.update(face,false,true,false,false,0,7100);assert(!face.visible && !face.pet && !face.shake && face.gaze==-1 && !face.animatedEnd);
  coordinator.update(face,true,true,false,false,0,7200);assert(face.visible && face.pet);
  coordinator.update(face,true,true,true,false,0,7300);assert(!face.pet && face.shake && !face.animatedEnd);
  coordinator.update(face,true,false,false,false,16,9500);assert(!face.pet && !face.shake && face.gaze==16);
  std::puts("PASS: production face coordinator; gaze/pet/shake precedence, long-stroke end, hidden page reset, no guruguru prerequisite; fake renderer only");
}
