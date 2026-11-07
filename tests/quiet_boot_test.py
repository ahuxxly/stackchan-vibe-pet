"""Actual old/new BSP begin/init/power bodies; exhaustive single-transaction faults.

Fake I2C and topology do not prove physical VM voltage or target-board timing.
"""
from pathlib import Path
import argparse
import json
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from prepare_safe_bsp import prepare,REL
from boot_runtime_test import body

SIGNATURES=('void M5StackChan_Class::begin()', 'void M5StackChan_Class::io_expander_init()',
 'void M5StackChan_Class::setServoPowerEnabled(bool enabled)',
 'int M5StackChan_Class::servoPowerGateLevel()', 'int M5StackChan_Class::setServoPowerEnabledChecked(',
 'bool M5StackChan_Class::readServoPowerRegister(', 'int M5StackChan_Class::servoPowerDiagnosticField(',
 'bool M5StackChan_Class::prepareMotion()', 'bool M5StackChan_Class::configureServoPowerBit(',
 'bool M5StackChan_Class::verifyServoPowerLow(', 'bool M5StackChan_Class::initializeServoPowerBias()')

OLD=r'''
int main(){resetFixture();M5StackChan_Class board;board.begin();
 assert(!board._motion_prepared&&board.servoInits==0&&!_io_expander->modeledEnable());
 assert((_io_expander->regs[19]&1)==1&&(_io_expander->regs[9]&1)==0&&(_io_expander->regs[11]&1)==1);
 assert(board.setServoPowerEnabledChecked(true)==1);
 assert((_io_expander->regs[5]&1)==1&&!_io_expander->modeledEnable());
 board.setServoPowerEnabled(false);
 puts("PASS negative control: actual usb1 startup leaves OD+PD/noPU; acknowledged ON cannot enable topology model");
}
'''
NEW=r'''
int main(){unsigned cases=0;
 resetFixture();M5StackChan_Class boot;boot.begin();
 assert(M5.actual.internal_mic&&M5.actual.internal_spk);
 assert(boot.servoInits==0&&!boot._motion_prepared&&boot._servo_power_configured);
 assert(!strcmp(boot._servo_power_stage,"ready_off")&&highWrites==0&&!_io_expander->everEnabled);
 assert((_io_expander->regs[3]&1)&&(_io_expander->regs[19]&1)&&(_io_expander->regs[9]&1));
 assert(!(_io_expander->regs[5]&1)&&!(_io_expander->regs[11]&1)&&boot.servoPowerGateLevel()==0);
 assert(boot.setServoPowerEnabledChecked(true)==1&&_io_expander->modeledEnable());
 assert(boot.prepareMotion()&&boot.prepareMotion()&&boot.servoInits==1);
 assert(boot.setServoPowerEnabledChecked(false)==1&&!_io_expander->modeledEnable()&&!boot.Motion.enabled);
 ++cases;
 // Begin is safe even when a previous firmware left latch HIGH: latch LOW is first.
 for(unsigned other:{0u,0x5au,0xfeu})for(bool latch:{false,true})for(bool direction:{false,true}){
  resetFixture();for(unsigned reg:{3u,5u,7u,9u,11u,19u})initial[reg]=other;
  initial[5]|=latch;initial[3]|=direction;initial[19]|=1;
  _io_expander=std::make_unique<m5::PY32IOExpander_Class>();M5StackChan_Class b;
  assert(b.initializeServoPowerBias()&&highWrites==0);
  assert(transactions==17&&!_io_expander->modeledEnable());
  for(unsigned reg:{3u,5u,9u,11u,19u})assert((_io_expander->regs[reg]&0xfe)==other);
  assert(transfers[0].reg==5&&!transfers[0].write&&transfers[1].reg==5&&transfers[1].write&&!(transfers[1].value&1));
  assert(!_io_expander->everEnabled);++cases;
 }
 // NACK each actual read/write in the initialization chain. No later bias write
 // is allowed. io_expander_init executes its one independent OFF request.
 for(unsigned failure=1;failure<=17;++failure){
  resetFixture();failAt=failure;M5StackChan_Class b;b.begin();
  assert(!b._servo_power_configured&&!b._motion_prepared&&b.servoInits==0&&highWrites==0);
  assert(strcmp(b._servo_power_error,"none")&&!_io_expander->everEnabled&&transactions==failure+2);
  const auto before=transactions;assert(b.setServoPowerEnabledChecked(true)==0&&transactions==before);
  assert(!b.prepareMotion()&&b.servoInits==0);
  assert(b.setServoPowerEnabledChecked(false)==1&&!_io_expander->modeledEnable());++cases;
 }
 // A failed write may nevertheless have reached the register. Still reject ON.
 for(unsigned failure:{2u,5u,8u,12u,15u}){
  resetFixture();failAt=failure;applyFailedWrite=true;M5StackChan_Class b;b.begin();
  assert(!b._servo_power_configured&&!_io_expander->everEnabled&&highWrites==0);
  assert(b.setServoPowerEnabledChecked(true)==0&&!strcmp(b._servo_power_error,"write_failed"));++cases;
 }
 // Success status with wrong readback is a failure, not a successful setup.
 for(unsigned wrong:{3u,6u,9u,13u,16u,10u,17u}){
  resetFixture();corruptAt=wrong;M5StackChan_Class b;b.begin();
  assert(!b._servo_power_configured&&highWrites==0&&!_io_expander->everEnabled);
  assert(!strcmp(b._servo_power_error,wrong==10||wrong==17?"input_not_low":"readback_mismatch"));
  assert(b.setServoPowerEnabledChecked(true)==0);++cases;
 }
 resetFixture();available=false;M5StackChan_Class absent;absent.begin();
 assert(!_io_expander&&!absent._servo_power_configured&&absent.servoInits==0);
 assert(!strcmp(absent._servo_power_stage,"io_timeout")&&!strcmp(absent._servo_power_error,"unavailable"));
 assert(absent.setServoPowerEnabledChecked(true)==-1&&absent.servoPowerGateLevel()==-1);++cases;
 // Loading a transistor base can differ from digital HIGH. This model proves
 // no equivalence; actual main.cpp retains its separate HIGH gate in power_app.
 resetFixture();M5StackChan_Class clamp;clamp.begin();clampInput=true;
 assert(clamp.setServoPowerEnabledChecked(true)==1&&_io_expander->modeledEnable());
 assert(clamp.servoPowerGateLevel()==0);clamp.setServoPowerEnabled(false);++cases;
 // Runtime ON and OFF NACK propagate and disable interpolation; OFF remains
 // callable with configuration invalid, and error provenance is not overwritten.
 for(bool on:{false,true})for(unsigned failedOperation:{1u,2u}){
  resetFixture();M5StackChan_Class b;b.begin();b._motion_prepared=true;b.Motion.enabled=true;
  failAt=transactions+failedOperation;assert(b.setServoPowerEnabledChecked(on)==0&&!b.Motion.enabled);++cases;
 }
 printf("PASS %u actual BSP startup/ON/OFF and failure cases (fake I2C/topology; no hardware)\n",cases);
}
'''

def run(label,source,scenario):
    code='#include <cstring>\n#include "power_bias_fixture.h"\n'
    code+='\n'.join(body(source,s) for s in SIGNATURES if s in source)
    # body() is shared with runtime fixtures and normalizes millis to appNow.
    code=code.replace('appNow()','millis()')
    p=ROOT/'.test-bin'/('quiet-boot-'+label+'.cpp');p.parent.mkdir(exist_ok=True)
    p.write_text(code+scenario)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-Itests',str(p),'-o',str(p.with_suffix(''))],cwd=ROOT,check=True)
    x=subprocess.run([str(p.with_suffix(''))],capture_output=True,text=True,check=True,timeout=15)
    print(x.stdout.strip());return {'case':label,'status':'PASS','output':x.stdout.strip()}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output');args=parser.parse_args()
    prepare(check=True)
    current=(ROOT/REL/'M5StackChan.cpp').read_text()
    old=(ROOT/'tests/fixtures/usb1-bsp.cpp').read_text()
    rows=[run('usb1-negative',old,OLD),run('power1',current,NEW)]
    unified=ROOT/'.pio/libdeps/stackchan-vibe-pet/M5Unified/src'
    for name in ('M5Unified.cpp','M5Unified.hpp'):
        assert not re.search(r'(Mic|Speaker)\s*\.\s*(begin|record|playRaw|tone)\s*\(', (unified/name).read_text()),name
    if args.output:Path(args.output).write_text(json.dumps({'status':'PASS','device_calls':0,'cases':rows},indent=2)+'\n')

if __name__=='__main__':main()
