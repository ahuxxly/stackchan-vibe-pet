"""Actual BSP checked GPIO functions, fake I2C only; no power writes."""
from pathlib import Path
import subprocess
from boot_runtime_test import body

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'.pio/libdeps/stackchan-vibe-pet/StackChan-BSP/src/M5StackChan.cpp').read_text()
legacy=(ROOT/'tests/fixtures/motion-baseline-bsp.cpp').read_text()
prefix=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <array>
static bool ok=true;static uint8_t returned=0;static unsigned reads=0,lastReg=0;
struct Io {
 bool digitalRead(unsigned){++reads;return ok?(returned&1):false;} // pinned readRegister8 value_or(0)
 bool readRegister(uint8_t reg,uint8_t* out,size_t n){assert(n==1);++reads;lastReg=reg;*out=returned;return ok;}
};
std::unique_ptr<Io> _io_expander;
struct M5StackChan_Class {int servoPowerGateLevel();int servoPowerDiagnosticField(unsigned);bool readServoPowerRegister(uint8_t,uint8_t*);};
struct Legacy {int servoPowerGateLevel();};
'''
tests=r'''
int main(){
 M5StackChan_Class board;assert(board.servoPowerGateLevel()==-1&&reads==0);
 _io_expander=std::make_unique<Io>();
 const std::array<unsigned,6> regs{7,3,5,9,11,19};
 for(unsigned i=0;i<6;++i){
  for(uint8_t value:{uint8_t(0),uint8_t(1),uint8_t(0xfe),uint8_t(0xff)}){
   ok=true;returned=value;const auto before=reads;
   assert(board.servoPowerDiagnosticField(i)==int(value&1)&&reads==before+1&&lastReg==regs[i]);
   ok=false;assert(board.servoPowerDiagnosticField(i)==-1); // never trust a failed transfer's bytes
  }
 }
 const auto before=reads;assert(board.servoPowerDiagnosticField(6)==-1&&reads==before);
 uint8_t value=0;ok=true;returned=0xa4;
 const auto checkedBefore=reads;
 assert(board.readServoPowerRegister(2,&value)&&value==0xa4&&reads==checkedBefore+1&&lastReg==2);
 assert(!board.readServoPowerRegister(4,&value)&&!board.readServoPowerRegister(7,nullptr)&&reads==checkedBefore+1);
 Legacy old;ok=false;assert(old.servoPowerGateLevel()==0);assert(board.servoPowerGateLevel()==-1);
 ok=true;returned=0;assert(board.servoPowerGateLevel()==0);returned=1;assert(board.servoPowerGateLevel()==1);
 puts("PASS actual checked GPIO readback: legacy NACK looked LOW, new NACK unknown; valid LOW/HIGH, exact six allowlisted registers, one transaction/call, no writes; fake I2C, not VM voltage");
}
'''
out=ROOT/'.test-bin/servo_power_test.cpp'
out.write_text(prefix+body(legacy,'int M5StackChan_Class::servoPowerGateLevel()').replace('M5StackChan_Class::','Legacy::')+'\n'+
               body(source,'int M5StackChan_Class::servoPowerGateLevel()')+'\n'+
               body(source,'int M5StackChan_Class::servoPowerDiagnosticField(')+'\n'+
               body(source,'bool M5StackChan_Class::readServoPowerRegister(')+tests)
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(out),'-o',str(out.with_suffix(''))],check=True)
subprocess.run([str(out.with_suffix(''))],check=True)
