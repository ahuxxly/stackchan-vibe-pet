"""Actual setup/loop/serialCommands/full telemetry and pinned HWCDC, offline.

Queues/ISR, sensors, graphics and codecs are modeled; this tests the confirmed
USB recovery gap, not the unobserved physical cause of the recorded failure.
"""
from pathlib import Path
import argparse
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'evidence/boot-regression-20261102'
CDC = ROOT / '.toolchain/packages/framework-arduinoespressif32/cores/esp32/HWCDC.cpp'
sys.path.insert(0, str(ROOT / 'scripts'))
from version_source import build_version


def driver_source(root=ROOT):
    # Historical firmware keeps its original core; never silently test an old
    # application against the new patched dependency.
    if root.resolve() != ROOT.resolve():
        frozen = root / CDC.relative_to(ROOT)
        if frozen.is_file():
            return frozen.read_text()
        original = ROOT / 'vendor/arduino-hwcdc-original/HWCDC.cpp'
        if original.is_file():
            return original.read_text()
    return CDC.read_text()


def body(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    level = 1
    while level:
        level += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end].replace('millis()', 'appNow()')


def build_case(label, root, rx_stall=False):
    main = (root / 'src/main.cpp').read_text()
    diag = 'Diagnostics diagnostics()' in (root / 'src/LocalAudio.h').read_text()
    code = ('#define BOOT_HAS_DIAGNOSTICS 1\n' if diag else '')
    code += '#include "boot_runtime_support.h"\n'
    cdc = driver_source(root)
    for signature in ['bool HWCDC::isCDC_Connected()', 'int HWCDC::availableForWrite(void)',
                      'size_t HWCDC::write(const uint8_t *buffer, size_t size)']:
        code += body(cdc, signature) + '\n'
    code += main[main.index('Page page='):main.index('void applyAffection(')]
    code += r'''
void updateImu(uint32_t);
void applyAffection(const AffectionApplyResult&,uint32_t){}
void drawPetHud(M5Canvas&){}
void drawPet(uint32_t){++lumi.frames;usb_model::advance(50);}
void drawGallery(uint32_t){assert(false);}
void drawAudio(uint32_t){assert(false);}
void drawControl(uint32_t){assert(false);}
void dumpFrame(){assert(false);}
void switchPage(Page){assert(false);}
void setScreen(bool){assert(false);}
void updateInput(uint32_t){} // No physical touch in this quiet-boot fixture.
void updatePet(uint32_t){} // No rear contact or reward updates.
bool selectedFresh(uint32_t now){return codex_hid::state().fresh(selected,now);}
bool selectedWaiting(uint32_t now){return codex_hid::state().awaiting(selected,now);}
uint32_t hostColor(int,uint32_t){return 0x40444f;}
'''
    for signature in ['int requestServoPower(', 'bool powerOffContext()', 'void updatePowerDiagnostics()', 'void motionOff(', 'void failMotion(', 'void failWarmup(', 'void failImu(',
                      'void showBootHealth(', 'void updateImu(', 'void updateHostCue(', 'void updateMotion(', 'void updateLights(',
                      'void motionDiagnostics()', 'void powerDiagnostics()', 'void telemetry()',
                      'bool serialCommands()' if 'bool serialCommands()' in main else 'void serialCommands()',
                      'void setup()', 'void loop()']:
        if signature == 'void updateImu(':
            signature = 'void updateImu(uint32_t now)'
        if signature in main:
            code += body(main, signature) + '\n'
    code += r'''
int main(int argc,char** argv){
 assert(argc==2);
 usb_model::hostNext=12000; // Polling starts before slow asset loading completes.
 setup();
 for(int i=0;i<4;++i)loop();
 assert(!usb_model::lines.empty());
 JsonDocument full;
 assert(!deserializeJson(full,usb_model::lines.back()));
 const std::string fullLine=usb_model::lines.back();
 const auto payloadSize=fullLine.size();
 assert(full["kind"].as<std::string>()=="vibe_pet_status");
 assert(full["servo_power_gate"].as<int>()==0&&!full["motion_enabled"].as<bool>());
 // A valid packet is queued, host transmission needs a new TX-empty interrupt.
 // No new payload may bypass the whole-line backpressure gate to restart it.
 usb_model::tx.clear();usb_model::pendingLine.clear();
 for(auto c:fullLine)usb_model::tx.push_back(c);
 connected=false;usb_model::interruptEnabled=false;
 usb_model::lines.clear();
 const auto beforeLoop=loopCount,beforeImu=imuGuard.samples(),beforeFrames=lumi.frames;
 const auto begin=appNow();
 unsigned maximumGap=0;uint32_t previous=appNow();bool stalled=false;
 while(uint32_t(appNow()-begin)<35000){
  // Stop reading again mid-run, then resume, without giving the application
  // a new outbound packet or a simulated RX interrupt as a shortcut.
  const auto age=uint32_t(appNow()-begin);
  if(age>=10000&&!stalled){stalled=true;usb_model::interruptEnabled=false;connected=false;}
  usb_model::hostReading=!(age>=10000&&age<13000);
  // The stalled link is injected once; an armed ISR must remain armed while
  // the host is paused. Repeatedly disabling it would invent another actor.
  modelIsr();
  loop();maximumGap=std::max(maximumGap,uint32_t(appNow()-previous));previous=appNow();
 }
 unsigned complete=0;uint32_t lastUptime=0;
 for(const auto& line:usb_model::lines){
  JsonDocument status;assert(!deserializeJson(status,line));
  assert(status["kind"].as<std::string>()=="vibe_pet_status");
  lastUptime=std::max(lastUptime,status["uptime_ms"].as<uint32_t>());++complete;
 }
 const bool live=loopCount>beforeLoop+500 && imuGuard.samples()>beforeImu+500 && lumi.frames>beforeFrames+100;
 const bool recovered=complete>=20&&uint32_t(appNow()-lastUptime)<2000;
 assert(live&&M5StackChan.onCalls==0&&M5StackChan.uartCalls==0&&motionCommands==0);
 assert(imuGuard.ready(appNow())&&imuGuard.gapCount()==0&&imuGuard.maxGapMs()<=120);
 printf("{\"label\":\"%s\",\"full_payload_bytes\":%zu,\"complete_responses\":%u,\"loop_delta\":%u,\"imu_delta\":%u,\"max_loop_gap_ms\":%u,\"imu_gap_count\":%u,\"imu_max_gap_ms\":%u,\"loop_live\":%s,\"usb_recovered\":%s,\"connection_service_calls\":%u,\"queued_bytes\":%zu}\n",
 argv[1],payloadSize,complete,loopCount-beforeLoop,imuGuard.samples()-beforeImu,maximumGap,imuGuard.gapCount(),imuGuard.maxGapMs(),live?"true":"false",recovered?"true":"false",usb_model::linkServices,usb_model::tx.size());
 return 0;
}
'''
    path = ROOT / '.test-bin' / ('boot-runtime-' + label + '.cpp')
    if rx_stall:
        # Actual RX ISR behavior is covered by usb_tx_service_test; this full
        # foreground model seeds its connected=true / TX disabled result.
        code = code.replace('connected=false;usb_model::interruptEnabled=false;',
                            'connected=true;usb_model::interruptEnabled=false;')
        code = code.replace('usb_model::interruptEnabled=false;connected=false;',
                            'usb_model::interruptEnabled=false;connected=true;')
    path.write_text(code)
    binary = path.with_suffix('')
    version = build_version(root)
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(root / 'src'),
                    '-Itests', '-I.pio/libdeps/stackchan-vibe-pet/ArduinoJson/src',
                    '-D', f'VIBESTACKCHAN_FIRMWARE_VERSION="{version}"',
                    str(path), '-o', str(binary)], cwd=ROOT, check=True, timeout=30)
    run = subprocess.run([str(binary), label], capture_output=True, text=True, timeout=15)
    if run.returncode:
        raise RuntimeError({'label': label, 'returncode': run.returncode,
                            'stdout': run.stdout, 'stderr': run.stderr})
    return json.loads(run.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expect-fixed', action='store_true')
    parser.add_argument('--rx-stall', action='store_true', help='Powerdiag1/current with RX-connected stranded TX')
    parser.add_argument('--output', type=Path, help='New evidence directory; do not overwrite historical runs')
    args = parser.parse_args()
    cases = [('audio4', ROOT / 'evidence/audio-volume-20261102/release-audio4'),
             ('integration2', ROOT / 'evidence/integration-20261102/release-integration2'),
             ('current', ROOT)]
    if args.rx_stall:
        cases = [('powerdiag1', ROOT / 'evidence/power-integration-20261105/release-powerdiag1'), ('current', ROOT)]
    records = [build_case(label, root, args.rx_stall) for label, root in cases]
    # Both old implementations can strand a full queue on stop/resume; the
    # >4KiB case already strands a *single* complete packet from the outset.
    name = 'runtime-fixed.json' if args.expect_fixed else 'runtime-baseline.json'
    output = args.output or OUT
    output.mkdir(parents=True, exist_ok=True)
    (output / name).write_text(json.dumps({'scope': '35 virtual seconds; actual foreground functions/full JSON/HWCDC methods; modeled peripherals/RTOS',
        'proven': 'constructible USB liveness defect, not proof of the recorded hardware root cause',
        'cases': records}, indent=2) + '\n')
    print(json.dumps(records, indent=2), flush=True)
    baseline = 0 if args.rx_stall else 1
    assert records[baseline]['loop_live'] and not records[baseline]['usb_recovered']
    assert records[-1]['usb_recovered'] == args.expect_fixed


if __name__ == '__main__':
    main()
