"""Compile actual BSP checked write bodies; fake transactions, never hardware."""
from pathlib import Path
import subprocess
from boot_runtime_test import body
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'.pio/libdeps/stackchan-vibe-pet/StackChan-BSP/src/M5StackChan.cpp').read_text()
prefix=r'''
#include <memory>
#include <cstdint>
#include <cassert>
#include <cstdio>
struct Io {
 bool ok=true;uint8_t value=0xa4;unsigned calls=0;
 bool bitOn(uint8_t reg,uint8_t mask){assert(reg==5&&mask==1);++calls;if(ok)value|=mask;return ok;}
 bool bitOff(uint8_t reg,uint8_t mask){assert(reg==5&&mask==1);++calls;if(ok)value&=~mask;return ok;}
};
std::unique_ptr<Io> _io_expander;
struct M5StackChan_Class {
 bool _motion_prepared=true, _servo_power_configured=true;
 struct {bool enabled=true;void setOutputEnabled(bool b){enabled=b;}} Motion;
 int setServoPowerEnabledChecked(bool);void setServoPowerEnabled(bool);
};
'''
checks=r'''
int main(){
 M5StackChan_Class b;assert(b.setServoPowerEnabledChecked(true)==-1&&!b.Motion.enabled);
 b.Motion.enabled=true;assert(b.setServoPowerEnabledChecked(false)==-1&&!b.Motion.enabled);
 _io_expander=std::make_unique<Io>();
 for(uint8_t initial:{uint8_t(0xa4),uint8_t(0xa5)})for(bool ok:{false,true})for(bool on:{false,true}) {
  _io_expander->ok=ok;_io_expander->value=initial;unsigned before=_io_expander->calls;
  b.Motion.enabled=true;assert(b.setServoPowerEnabledChecked(on)==int(ok));
  assert(_io_expander->calls==before+1&&(_io_expander->value&0xfe)==0xa4);
  assert(b.Motion.enabled==(ok&&on));
  assert((_io_expander->value&1)==(ok?int(on):int(initial&1)));
 }
 _io_expander->ok=false;b.setServoPowerEnabled(true);assert(!b.Motion.enabled);
 _io_expander->ok=true;b._servo_power_configured=false;
 unsigned before=_io_expander->calls;assert(b.setServoPowerEnabledChecked(true)==0&&_io_expander->calls==before&&!b.Motion.enabled);
 assert(b.setServoPowerEnabledChecked(false)==1&&_io_expander->calls==before+1&&!b.Motion.enabled);
 puts("PASS actual BSP writes: same register/mask, returned NACK/unavailable, failed ON disables interpolation, OFF always disables, no unrelated bits changed (fake I2C)");
}
'''
p=ROOT/'.test-bin/power_write_test.cpp'
p.write_text(prefix+body(s,'int M5StackChan_Class::setServoPowerEnabledChecked(')+body(s,'void M5StackChan_Class::setServoPowerEnabled(')+checks)
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True)
subprocess.run([str(p.with_suffix(''))],check=True)
