#pragma once
// Fake I2C/electrical topology only. Production BSP bodies are compiled separately.
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
static uint32_t tick=0;
static bool available=true,initOK=true,clampInput=false;
static unsigned failAt=0,corruptAt=0,transactions=0,highWrites=0;
static bool applyFailedWrite=false;
struct Transfer {bool write;uint8_t reg,value;bool ok;};
static std::vector<Transfer> transfers;
static std::array<uint8_t,256> initial{};
static std::vector<std::string> events;
uint32_t millis(){return tick;}
void delay(uint32_t ms){tick+=ms;}
#define ESP_LOGE(...) (void)0
struct Config {bool internal_mic=true,internal_spk=true;};
struct Core {Config actual;Config config(){return {};}
 void begin(Config c){actual=c;events.push_back("core");}} M5;
namespace m5 { struct PY32IOExpander_Class {
 std::array<uint8_t,256> regs=initial;
 bool everEnabled=false;
 bool begin(){return available;}
 // Topology-only model: OD release needs pull-up, not pull-down. No measured VM.
 bool modeledEnable() const {
  if((regs[3]&1) && !(regs[5]&1))return false;
  if((regs[3]&1) && !(regs[19]&1) && (regs[5]&1))return true;
  return (regs[9]&1) && !(regs[11]&1);
 }
 bool readRegister(uint8_t reg,uint8_t* out,size_t n){
  assert(n==1);++transactions;
  uint8_t value=reg==7 ? uint8_t((regs[7]&0xfe)|(modeledEnable()&&!clampInput)) : regs[reg];
  if(transactions==corruptAt)value^=1;
  bool ok=transactions!=failAt;transfers.push_back({false,reg,value,ok});
  if(ok)*out=value;
  return ok;
 }
 bool writeRegister8(uint8_t reg,uint8_t value){
  ++transactions;bool ok=transactions!=failAt;
  transfers.push_back({true,reg,value,ok});
  if(reg==5&&(value&1))++highWrites;
  if(ok||applyFailedWrite)regs[reg]=value;
  everEnabled|=modeledEnable();return ok;
 }
 bool bitOn(uint8_t reg,uint8_t mask){uint8_t v=0;return readRegister(reg,&v,1)&&writeRegister8(reg,v|mask);}
 bool bitOff(uint8_t reg,uint8_t mask){uint8_t v=0;return readRegister(reg,&v,1)&&writeRegister8(reg,v&~mask);}
 void digitalWrite(int pin,bool value){uint8_t reg=5+pin/8,mask=1<<(pin%8);if(value)bitOn(reg,mask);else bitOff(reg,mask);}
 bool digitalRead(int pin){uint8_t v=0;return readRegister(7+pin/8,&v,1)&&(v&(1<<(pin%8)));}
 void setDirection(int pin,bool value){uint8_t reg=3+pin/8,mask=1<<(pin%8);if(value)bitOn(reg,mask);else bitOff(reg,mask);}
 void setPullMode(int pin,bool up){uint8_t mask=1<<(pin%8);bitOff((up?11:9)+pin/8,mask);bitOn((up?9:11)+pin/8,mask);}
 void setDriveMode(int pin,bool od){uint8_t reg=19+pin/8,mask=1<<(pin%8);if(od)bitOn(reg,mask);else bitOff(reg,mask);}
 void setLedCount(int){}
}; }
std::unique_ptr<m5::PY32IOExpander_Class> _io_expander;
struct Touch {void begin(){events.push_back("touch");}};
class M5StackChan_Class {public:
 struct {bool enabled=false;void setOutputEnabled(bool v){enabled=v;}} Motion;
 Touch TouchSensor;
 bool _motion_prepared=false,_servo_power_configured=false;
 const char* _servo_power_stage="not_started";
 const char* _servo_power_error="none";
 int _servo_power_reg=-1,_servo_power_actual=-1,servoInits=0;
 void begin();void io_expander_init();void setServoPowerEnabled(bool);bool prepareMotion();
 int servoPowerGateLevel();int servoPowerDiagnosticField(unsigned);
 int setServoPowerEnabledChecked(bool);bool readServoPowerRegister(uint8_t,uint8_t*);
 bool initializeServoPowerBias();bool configureServoPowerBit(uint8_t,bool,const char*);
 bool verifyServoPowerLow(const char*);
 bool servo_init(){++servoInits;events.push_back("servo-init");return initOK;}
 void ina226_init(){}void showRgbColor(int,int,int){}
};
static void resetFixture(){
 tick=0;available=initOK=true;clampInput=false;failAt=corruptAt=transactions=highWrites=0;
 applyFailedWrite=false;transfers.clear();events.clear();initial.fill(0);initial[19]=255;initial[2]=0x41;
 _io_expander.reset();
}
