"""Execute actual patched lifecycle and mic sample-loop bodies against fake RTOS/I2S."""
from pathlib import Path
import sys,subprocess
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
from prepare_local_audio import prepare,REL
prepare(check=True)
mic=(ROOT/REL/'Mic_Class.cpp').read_text()
spk=(ROOT/REL/'Speaker_Class.cpp').read_text()
def function(source,signature):
 start=source.index(signature);brace=source.index('{',start);end=brace+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
begin_mic=function(mic,'bool Mic_Class::begin(void)')
end_mic=function(mic,'void Mic_Class::end(void)')
begin_spk=function(spk,'bool Speaker_Class::begin(void)')
sample_loop=mic[mic.index('      const size_t completed_length = dst_remain;'):mic.index('    self->_is_recording = false;\n    _i2s_stop')]
# Strip the closing brace of the outer task while; preserve the real inner loop.
sample_loop=sample_loop.rstrip();assert sample_loop.endswith('}');sample_loop=sample_loop[:-1]
prefix=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <climits>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#define portNUM_PROCESSORS 2
#define pdMS_TO_TICKS(x) (x)
constexpr int ESP_OK=0;
using TaskHandle_t=void*;
bool failTask=false,failSemaphore=false,failSetup=false;
int codecOn=0,codecOff=0,uninstalls=0;void** activeHandle=nullptr;
void* xSemaphoreCreateBinary(){return failSemaphore?nullptr:(void*)1;}
void vTaskDelay(int){if(activeHandle)*activeHandle=nullptr;}
void xTaskNotifyGive(void*){}
void xTaskCreatePinnedToCore(void(*)(void*),const char*,size_t,void*,int,void** h,int){*h=failTask?nullptr:(void*)1;}
void xTaskCreate(void(*f)(void*),const char*n,size_t z,void*a,int p,void**h){xTaskCreatePinnedToCore(f,n,z,a,p,h,0);}
void _i2s_driver_uninstall(int){++uninstalls;}
bool codec(void*,bool on){on?++codecOn:++codecOff;return true;}
struct Config {int task_pinned_core=0,task_priority=2,i2s_port=1;size_t dma_buf_len=128;};
struct Info {void* data=nullptr;size_t length=0,index=0;bool is_stereo=false,is_16bit=true;};
class Mic_Class {public:
 using recording_info_t=Info;
 bool _task_running=false,_is_recording=false;void* _task_handle=nullptr;void* _task_semaphore=nullptr;
 Config _cfg;unsigned _rec_sample_rate=0;Info _rec_info[2];std::atomic<size_t> _pending_samples{0};std::atomic<bool> _capture_failed{false};
 bool(*_cb_set_enabled)(void*,bool)=codec;void* _cb_set_enabled_args=nullptr;
 unsigned _calc_rec_rate(){return 16000;}int _setup_i2s(){return failSetup?1:ESP_OK;}int isRecording(){return 0;}
 static void mic_task(void*){}bool begin();void end();
};
class Speaker_Class {public:
 bool _task_running=false;void* _task_handle=nullptr;void* _task_semaphore=nullptr;Config _cfg;
 bool(*_cb_set_enabled)(void*,bool)=codec;void* _cb_set_enabled_args=nullptr;
 int _setup_i2s(){return failSetup?1:ESP_OK;}static void spk_task(void*){}bool begin();
};
'''
loop_prefix=r'''
struct LoopConfig {int i2s_port=1;bool use_adc=false;int noise_filter_level=0;};
struct LoopMic {bool _task_running=true;std::atomic<bool> _capture_failed{false};std::atomic<size_t> _pending_samples{320};LoopConfig _cfg;int32_t _offset=0;};
int readError=0,pattern=0;size_t sourceFrame=0;size_t readBytes=128;bool stopInRead=false;LoopMic* loopOwner=nullptr;
int _i2s_read(int,void* dest,size_t len,size_t* result,unsigned timeout){
 assert(timeout==20);*result=readBytes;
 auto p=static_cast<int16_t*>(dest);for(size_t i=0;i<len/2;i+=2){
  int wave=(sourceFrame++%16<8)?2000:0;
  p[i]=pattern?wave:1000;
  p[i+1]=pattern==1?wave:pattern==2?0:pattern==3?2000-wave:-1000;
 }
 if(stopInRead)loopOwner->_task_running=false;return readError;
}
void runSampleLoop(LoopMic& instance,int16_t* pcm){
 auto self=&instance;loopOwner=self;Info info;info.data=pcm;info.length=320;Info* current_rec=&info;
 size_t dst_remain=320,src_idx=~size_t(0),src_len=0;const size_t dma_buf_len=128;
 int16_t src_buf[128]{};int oversampling=1,os_remain=1;bool in_stereo=true;float f_gain=1;
 int32_t sum_value[4]={},prev_value[2]={};
'''
suffix=r'''
}
int main(){
 for(int mode=0;mode<4;++mode){
  failSemaphore=mode==1;failSetup=mode==2;failTask=mode==3;
  Mic_Class mic;Speaker_Class speaker;const bool ok=mode==0;
  assert(mic.begin()==ok);assert(mic._task_running==ok);assert(speaker.begin()==ok);assert(speaker._task_running==ok);
  if(ok)assert(mic._rec_sample_rate==16000);
  mic._rec_info[0].length=320;mic._rec_info[1].length=320;mic._pending_samples.store(640);
  activeHandle=&mic._task_handle;mic.end();assert(!mic._task_running&&!mic._pending_samples.load());
  assert(!mic._rec_info[0].length&&!mic._rec_info[1].length);
 }
 assert(codecOff>=6&&uninstalls>=6);
 int16_t pcm[320]{};LoopMic good;runSampleLoop(good,pcm);
 assert(good._pending_samples.load()==0&&!good._capture_failed.load());
 // Execute the real stereo-to-mono/DC/gain loop with known16kHz1kHz input.
 int16_t same[320]{},single[320]{},opposite[320]{};
 pattern=1;sourceFrame=0;LoopMic stereo;runSampleLoop(stereo,same);
 pattern=2;sourceFrame=0;LoopMic one;runSampleLoop(one,single);
 pattern=3;sourceFrame=0;LoopMic anti;runSampleLoop(anti,opposite);
 int crossings=0;long long a=0,b=0;
 for(int i=80;i<320;++i){a+=int(same[i])*same[i];b+=int(single[i])*single[i];
  assert(std::abs(int(opposite[i]))<90);
  if(i>80 && (same[i]>=0)!=(same[i-1]>=0))++crossings;
 }
 assert(crossings>=28&&crossings<=31); // 240frames at1kHz/16kHz
 assert(b>0 && a>3*b && a<5*b); // physical stereo averaging, no16/8bit reinterpretation
 pattern=0;sourceFrame=0;
 for(size_t bytes:{size_t(0),size_t(1),size_t(2),size_t(3),size_t(6),size_t(132)}){
  readBytes=bytes;LoopMic bad;runSampleLoop(bad,pcm);assert(bad._capture_failed.load()&&!bad._task_running&&bad._pending_samples.load()==320);
 }
 readBytes=128;readError=1;LoopMic timeout;runSampleLoop(timeout,pcm);assert(timeout._capture_failed.load());
 readError=0;stopInRead=true;LoopMic cancelled;runSampleLoop(cancelled,pcm);
 assert(!cancelled._task_running && cancelled._pending_samples.load()==320); // cancelled is never complete
 puts("PASS actual patched audio driver bodies: semaphore/I2S/task failure,cleanup/cancelled descriptors,rate stability; actual mic loop stereo/downmix/DC/1kHz/missingchannel/cancellation,good/empty/short/oversize/error DMA and stop; fake hardware");
}
'''
target=ROOT/'.test-bin/audio_driver_test.cpp';target.write_text(prefix+begin_mic+end_mic+begin_spk+loop_prefix+sample_loop+suffix)
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(target),'-o',str(target.with_suffix(''))],check=True)
subprocess.run([str(target.with_suffix(''))],check=True)
