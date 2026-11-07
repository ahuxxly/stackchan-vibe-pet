"""Actual BSP UART/task init and update bodies, with failing fake bus/RTOS."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from prepare_safe_bsp import prepare,REL
prepare(check=True)
def body(source,signature):
    at=source.index(signature);end=source.index('{',at)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[at:end]
motion=(ROOT/REL/'utils/motion/motion.cpp').read_text()
board=(ROOT/REL/'M5StackChan.cpp').read_text()
functions=[body(motion,sig) for sig in ('bool Motion::init(', 'void Motion::update()')]
functions.append(body(board,'bool M5StackChan_Class::servo_init()'))
prefix=r'''
#include <cassert>
#include <atomic>
#include <mutex>
#include <memory>
#include <cstdio>
#include <string>
using uint32_t=unsigned;using TaskHandle_t=void*;
constexpr int UART_NUM_1=1,pdPASS=1;
static bool taskOK=true,taskNull=false,busOK=true;static int taskCalls=0,servoUpdates=0,servoInitCalls=0;
uint32_t millis(){return 1000;}void delay(uint32_t){}
#define ESP_LOGI(...) (void)0
int xTaskCreate(void(*)(void*),const char*,int,void*,int,TaskHandle_t* handle){++taskCalls;*handle=taskOK&&!taskNull?reinterpret_cast<void*>(1):nullptr;return taskOK?1:0;}
namespace uitk_intl{struct Vector2i{int x=0,y=0;Vector2i()=default;Vector2i(int a,int b):x(a),y(b){}};
namespace ui_hal {template<class T>void on_delay(T){}template<class T>void on_get_tick(T){}}}
struct Servo{virtual ~Servo()=default;virtual void init(){++servoInitCalls;}void update(){++servoUpdates;}};
struct Motion{
 std::mutex _mutex;std::unique_ptr<Servo> _yaw_servo,_pitch_servo;
 std::atomic<bool> _running{false},_output_enabled{false};TaskHandle_t _task_handle=nullptr;
 static void update_task(void*){}bool init(std::unique_ptr<Servo>,std::unique_ptr<Servo>);void update();
};
struct ServoConfig_t{int id=-1,defaultZeroPos=0;uitk_intl::Vector2i angleLimit,rawPosLimit;std::string settingNs,settingZeroPositionKey;bool enablePwmMode=false;};
struct ScsServo:Servo{explicit ScsServo(const ServoConfig_t&) {}};
struct Bus{int calls=0;bool begin(int uart,int baud,int tx,int rx){++calls;assert(uart==1&&baud==1000000&&tx==6&&rx==7);return busOK;}} _scs_bus;
struct M5StackChan_Class{bool _motion_uart_ready=false;::Motion Motion;bool servo_init();};
'''
suffix=r'''
int main(){
 busOK=false;M5StackChan_Class failedBus;assert(!failedBus.servo_init());
 assert(!failedBus._motion_uart_ready&&taskCalls==0&&servoInitCalls==0);
 busOK=true;taskOK=false;M5StackChan_Class failedTask;assert(!failedTask.servo_init());
 assert(failedTask._motion_uart_ready&&!failedTask.Motion._running&&taskCalls==1);
 const int uartCalls=_scs_bus.calls;taskOK=true;taskNull=true;assert(!failedTask.servo_init());
 assert(!failedTask.Motion._running&&_scs_bus.calls==uartCalls); // no double UART install
 taskNull=false;assert(failedTask.servo_init());assert(failedTask.Motion._running);
 const int before=servoUpdates;failedTask.Motion.update();assert(servoUpdates==before);
 failedTask.Motion._output_enabled=true;failedTask.Motion.update();assert(servoUpdates==before+2);
 failedTask.Motion._output_enabled=false;
 for(int n=0;n<1000;++n)failedTask.Motion.update();assert(servoUpdates==before+2);
 puts("PASS actual BSP init/update: UART failure never creates task, task/null-handle failure is false, retry reuses owned UART, no background servo polling while rail off; fake UART/RTOS");
}
'''
out=ROOT/'.test-bin/motion_init_test.cpp';out.write_text(prefix+'\n'.join(functions)+suffix)
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(out),'-o',str(out.with_suffix(''))],check=True)
subprocess.run([str(out.with_suffix(''))],check=True)
