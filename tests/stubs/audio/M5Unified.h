#pragma once
#include "freertos/FreeRTOS.h"
#include <cassert>
constexpr int I2S_NUM_1=1;
namespace m5 {enum class board_t{board_M5StackChan};enum class input_channel_t{input_only_right,input_only_left,input_stereo};}
struct AudioConfig {m5::input_channel_t input_channel=m5::input_channel_t::input_stereo;unsigned magnification=2,noise_filter_level=0;int pin_data_in=14,pin_data_out=13,i2s_port=1;unsigned sample_rate=16000,over_sampling=1,dma_buf_len=128,dma_buf_count=4,task_pinned_core=0;};
inline std::atomic<unsigned> micDelay{0},speakerDelay{0};
inline std::atomic<int> owner{0},micCalls{0},speakerCalls{0},recordCalls{0},toneCalls{0},playCalls{0};
inline std::atomic<bool> codecOK{true};
inline std::atomic<int> badCodecReg{-1};
struct FakeMic {
 AudioConfig cfg;std::atomic<bool> running{false};size_t pending=0;int16_t* buffer=nullptr;unsigned completeAt=0;
 AudioConfig config(){return cfg;}void config(AudioConfig c){cfg=c;}
 bool isRunning(){return running.load();}
 bool begin(){++micCalls;assert(owner.exchange(1)==0);std::this_thread::sleep_for(std::chrono::milliseconds(micDelay.load()));completeAt=millis();running=true;return true;}
 void end(){assert(owner.load()==1);owner=0;running=false;pending=0;completeAt=0;}
 bool record(int16_t* p,size_t n,uint32_t rate,bool stereo){assert(running && !pending && rate==16000&&!stereo);++recordCalls;pending=n;buffer=p;// DMA samples arrive continuously, independently of host polling latency.
 // Keep the sample-clock deadline cumulative so delayed worker polls can
 // consume data already captured instead of restarting a 20ms timer.
 assert(n==320);completeAt+=static_cast<unsigned>(n*1000/rate);return true;}
 size_t pendingSamples(){if(pending && int32_t(millis()-completeAt)>=0){for(size_t i=0;i<pending;++i)buffer[i]=(i%2)?1000:-1000;pending=0;}return pending;}
 bool captureFailed(){return false;}
};
struct FakeSpeaker {
 AudioConfig cfg;std::atomic<bool> running{false};unsigned until=0;uint8_t volume=0;
 AudioConfig config(){return cfg;}void config(AudioConfig c){cfg=c;}
 bool isRunning(){return running.load();}
 bool begin(){++speakerCalls;assert(owner.exchange(2)==0);std::this_thread::sleep_for(std::chrono::milliseconds(speakerDelay.load()));running=true;return true;}
 void end(){assert(owner.load()==2);owner=0;running=false;until=0;}
 void setVolume(uint8_t v){assert(v==24 || v==80);volume=v;}void setAllChannelVolume(uint8_t v){assert(v==255);}
 bool tone(uint32_t f,uint32_t t,int,bool){assert(f==660&&t==150&&volume==24);++toneCalls;until=millis()+t;return true;}
 bool playRaw(const int16_t*,size_t n,uint32_t rate,bool stereo,unsigned repeat,int,bool){assert(n==80000&&rate==16000&&!stereo&&repeat==1&&volume==80);++playCalls;until=millis()+5000;return true;}
 bool isPlaying(){return until && int32_t(until-millis())>0;}
};
struct FakeBus {
 bool readRegister(uint8_t addr,uint8_t reg,uint8_t* bytes,size_t n,int){
  if(!codecOK)return false;
  if(addr==0x36){assert(owner==2&&reg==4&&n==2);bytes[0]=bytes[1]=0x40;}
  else {assert(owner==1&&addr==0x40&&n==1);assert(reg==0 || reg==0x11 || reg==0x43 || reg==0x44);bytes[0]=reg==0?0x41:reg==0x11?0x60:0x1b;if(badCodecReg==reg)bytes[0]^=1;}return true;
 }
};
struct FakeDisplay{void wakeup(){}void sleep(){}void setBrightness(int){}};
struct FakeTouch{struct Detail{bool down=false;int x=0,y=0;bool isPressed() const{return down;}} detail;const Detail& getDetail(){return detail;}};
struct FakeImu {struct V{float x=0,y=0,z=0;};struct Data{V accel{0,0,1},gyro{};} data;int update(){return 3;}const Data& getImuData(){return data;}};
struct FakeBoard {FakeDisplay Display;FakeTouch Touch;FakeImu Imu;FakeMic Mic;FakeSpeaker Speaker;FakeBus In_I2C;m5::board_t getBoard(){return m5::board_t::board_M5StackChan;}};
inline FakeBoard M5;
