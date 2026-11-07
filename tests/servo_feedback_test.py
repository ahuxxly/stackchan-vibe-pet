"""Execute actual pinned ScsServo read mapping and Motion.stop with fake bus/servos."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from prepare_safe_bsp import prepare,REL
prepare(check=True)
def body(source,signature):
    at=source.index(signature);start=source.index('{',at);end=start+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
feedback=body((ROOT/REL/'M5StackChan.cpp').read_text(),'int getCurrentAngle() override')
stop=body((ROOT/REL/'utils/motion/motion.cpp').read_text(),'void Motion::stop()')
code=r'''
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <mutex>
#include <atomic>
#include "PetMotion.h"
#include "ServoWarmup.h"
namespace uitk_intl {template<class T>T clamp(T v,T lo,T hi){return std::max(lo,std::min(hi,v));}}
struct Pair{int x,y;};
std::atomic<int> _servo_raw[2]{{-32768},{-32768}},_servo_error[2]{{0},{0}},_servo_status[2]{{0},{0}};
struct Bus{int value=620,reads=0;int ReadPos(int){++reads;return value;}int getLastError(){return value<0?1:0;}int getState(){return 0;}};
struct Scs {Bus _scs_bus;struct {int id=2;Pair rawPosLimit{0,1000};} _config;int _zero_pos=620;
 Pair getAngleLimit(){return {0,900};}
 int getCurrentAngle() FEEDBACK
};
struct Servo {int cancelled=0;void cancelAnimation(){++cancelled;}};
struct Motion {std::mutex _mutex;Servo* _yaw_servo=nullptr;Servo* _pitch_servo=nullptr;void stop() STOP};
int main(){
 Scs s;
 for(int raw:{-1,-32768,1001}){s._scs_bus.value=raw;assert(s.getCurrentAngle()==-32768);assert(!pet_motion::Controller::validHome(0,s.getCurrentAngle()));}
 s._scs_bus.value=620;assert(s.getCurrentAngle()==0);assert(_servo_raw[1]==620&&_servo_error[1]==0); // valid logical zero differs from missing feedback
 s._scs_bus.value=774;assert(s.getCurrentAngle()==481);
 s._scs_bus.value=0;assert(s.getCurrentAngle()==0); // legitimate raw lower limit (still fails safe home range)
 s._scs_bus.value=1000;assert(s.getCurrentAngle()==900);
 pet_motion::ServoWarmup w;w.start(0);assert(w.poll(600)==pet_motion::ServoWarmup::Poll::ReadPose);
 assert(!w.accept(0,-32768,600));assert(!w.accept(0,0,700));
 Motion m;m.stop();Servo yaw,pitch;m._yaw_servo=&yaw;m._pitch_servo=&pitch;m.stop();
 assert(yaw.cancelled==1 && pitch.cancelled==1);
 puts("PASS actual BSP feedback and stop bodies: missing/timeout=-1 sentinel, invalid range, genuine zero, historical481 mapping, warm-up rejection, cancel without UART/move (fake bus)");
}
'''.replace('FEEDBACK',feedback).replace('STOP',stop)
out=ROOT/'.test-bin/servo_feedback_test.cpp';out.write_text(code)
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Isrc',str(out),'-o',str(out.with_suffix(''))],check=True,cwd=ROOT)
subprocess.run([str(out.with_suffix(''))],check=True)
