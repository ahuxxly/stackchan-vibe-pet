#include "PetMotion.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace pet_motion;
struct Event {uint32_t at;Output out;int8_t gaze;};
static unsigned runs=0,commands=0;

static std::vector<Event> run(Activity activity,int yaw,int pitch,uint32_t start,uint32_t seed) {
  Controller controller;
  assert(controller.enable(yaw,pitch,start,seed));
  assert(controller.request(activity,start));
  std::vector<Event> events;
  uint32_t lastMove=0;bool haveMove=false;
  int finalYaw=yaw,finalPitch=pitch;
  for(uint32_t elapsed=0;elapsed<=Controller::kBurstMs;elapsed+=20) {
    uint32_t now=start+elapsed;
    bool busy=haveMove && uint32_t(now-lastMove)<400;
    auto out=controller.tick(now,busy,{});
    assert(!out.powerOff);
    if(out.move) {
      assert(!busy); // Fake motor completion is independent of the planner.
      assert(controller.posePermits(out,finalYaw,finalPitch));
      assert(out.speed==350);
      if(out.axis==Axis::Yaw) {
        assert(out.target>=-1000 && out.target<=1000);
        assert(out.target-yaw>=-300 && out.target-yaw<=300);finalYaw=out.target;
      } else {
        assert(out.target>=100 && out.target<=800);
        assert(out.target-pitch>=0 && out.target-pitch<=150);finalPitch=out.target;
      }
      if(haveMove)assert(uint32_t(now-lastMove)>=650);
      controller.commandSent(out);
      lastMove=now;haveMove=true;++commands;
      events.push_back({elapsed,out,controller.gaze()});
    }
    if(!controller.active()) {
      assert(elapsed<Controller::kBurstMs);
      assert(finalYaw==yaw && finalPitch==pitch && controller.gaze()==-1);
      assert(events.size()==(activity==Activity::Dance?4:activity==Activity::Look?3:5));
      ++runs;return events;
    }
  }
  assert(false && "Choreography must finish within its limit");return {};
}
int main() {
  Controller off;
  assert(!off.enabled());
  for(uint32_t now=0;now<1000000;now+=1000) {
    assert(!off.request(Activity::Dance,now));
    auto out=off.tick(now,false,{});assert(!out.move && !out.powerOff);
  }
  for(auto pose:std::vector<std::pair<int,int>>{{-651,400},{651,400},{0,199},{0,651},{-1280,0}})
    assert(!off.enable(pose.first,pose.second,0,1) && !off.enabled());
  for(int yaw:{-650,0,650})for(int pitch:{200,400,650})for(uint32_t seed=1;seed<=20;++seed)
    for(auto activity:{Activity::Look,Activity::Pet,Activity::Attention,Activity::Dance}) {
      run(activity,yaw,pitch,100,seed);
      run(activity,yaw,pitch,0xfffff000U,seed);
    }
  // Automatic look is sparse, independent of Codex, and suppressed during waiting/touch/petting.
  for(int scenario=0;scenario<4;++scenario) {
    Controller idle;assert(idle.enable(0,400,0,1));
    Guard guard;guard.awaiting=scenario==1;guard.petting=scenario==2;guard.touching=scenario==3;
    bool sawMove=false;
    for(uint32_t now=0;now<=30020;now+=20) {
      auto out=idle.tick(now,false,guard);
      if(out.move){assert(now>=15000);sawMove=true;break;}
    }
    assert(sawMove==(scenario==0));
  }
  // Immediate cancellation on page/screen/mic/shake; no return movement after stop.
  for(int scenario=0;scenario<5;++scenario) {
    Controller c;assert(c.enable(0,400,0,1));assert(c.request(Activity::Dance,0));
    assert(c.tick(0,false,{}).move);
    Guard g;
    if(scenario==0)g.petPage=false;
    if(scenario==1)g.screenOn=false;
    if(scenario==2)g.microphone=true;
    if(scenario==3)g.shaking=true;
    if(scenario==4)c.disable(10);
    auto out=c.tick(10,true,g);assert(!out.move && !c.enabled());
    assert(out.powerOff==(scenario!=4));
    for(uint32_t now=20;now<13000;now+=20)assert(!c.tick(now,false,{}).move);
  }
  // A stuck motor and a missing loop deadline cannot create catch-up bursts.
  Controller stuck;assert(stuck.enable(0,400,0,1));assert(stuck.request(Activity::Dance,0));
  assert(stuck.tick(0,false,{}).move);
  assert(!stuck.tick(2999,true,{}).powerOff);
  assert(stuck.tick(3000,true,{}).powerOff && !stuck.enabled());
  Controller late;assert(late.enable(0,400,0,1));assert(late.request(Activity::Dance,0));
  assert(late.tick(12000,false,{}).powerOff);
  Controller session;assert(session.enable(0,400,0,1));
  Guard waiting;waiting.awaiting=true;
  assert(!session.tick(599999,false,waiting).powerOff);
  assert(session.tick(600000,false,waiting).powerOff);
  // Petting preempts a dance, waits for the current axis and returns both axes.
  Controller pet;assert(pet.enable(0,400,0,1));assert(pet.request(Activity::Dance,0));
  assert(pet.tick(0,false,{}).move);
  assert(!pet.request(Activity::Dance,10));
  assert(pet.request(Activity::Pet,100));
  assert(!pet.tick(100,true,{}).move);
  auto home=pet.tick(400,false,{});assert(home.move && home.axis==Axis::Yaw && home.target==0);
  assert(!pet.request(Activity::Dance,500));
  assert(!pet.request(Activity::Pet,500));
  Controller stopped;assert(stopped.enable(0,400,0,1));assert(stopped.request(Activity::Dance,0));
  stopped.tick(0,false,{});stopped.disable(10);
  assert(stopped.enable(100,500,10000,2));
  assert(!stopped.active() && !stopped.tick(10000,false,{}).move); // no saved dance replay
  Controller displaced;assert(displaced.enable(0,400,0,1));assert(displaced.request(Activity::Dance,0));
  auto proposed=displaced.tick(0,false,{});
  assert(displaced.posePermits(proposed,0,400));
  assert(!displaced.posePermits(proposed,51,400));
  assert(!displaced.posePermits(proposed,0,431));
  assert(!displaced.posePermits(proposed,-1280,0));
  assert(!displaced.posePermits(proposed,900,400));
  proposed.target=-1000;assert(!displaced.posePermits(proposed,0,400));
  // A large planned move is accepted only after actual arrival, not just
  // because an absolute/home envelope would still permit the next step.
  Controller arrival;assert(arrival.enable(0,400,0,1));assert(arrival.request(Activity::Dance,0));
  auto first=arrival.tick(0,false,{});arrival.commandSent(first);
  assert(!arrival.poseAtRestPermits(0,400)); // Stalled at old home.
  assert(arrival.poseAtRestPermits(first.target,400));
  assert(!arrival.poseAtRestPermits(first.target,435)); // Other axis displaced.
  auto trace=run(Activity::Dance,0,400,0,1);
  std::puts("DANCE_TRACE_BEGIN");
  for(auto event:trace)std::printf("{\"at_ms\":%u,\"axis\":\"%s\",\"target\":%d,\"speed\":%d,\"gaze\":%d}\n",
      event.at,event.out.axis==Axis::Yaw?"yaw":"pitch",event.out.target,event.out.speed,event.gaze);
  std::puts("DANCE_TRACE_END");
  std::printf("PASS: %u choreography runs, %u bounded single-axis commands; limits, wrap, stop, preemption, no replay, motor/loop/session timeouts; fake motor only\n",runs,commands);
}
