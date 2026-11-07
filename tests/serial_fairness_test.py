"""Offline actual setup/loop/serialCommands/full status/input/STOP regression.

Hardware/RTOS are explicit fakes. The 18 ms status cost is a conservative
rounded model of the saved 147.699 ms / eight-response observation, not an MCU
benchmark. Generated production function bodies are not changed by this test.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from boot_runtime_test import body, CDC, driver_source

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'evidence/serial-fairness-20261102'


def replace_once(text, before, after):
    assert text.count(before) == 1, before
    return text.replace(before, after)


def support(directory):
    usb = (ROOT / 'tests/boot_usb_model.h').read_text()
    usb = replace_once(usb, 'inline size_t capacity=8192;', '''inline size_t capacity=8192;
inline unsigned telemetryCostMs=18,telemetryCalls=0,readCostMs=0,inputCalls=0;
inline unsigned renderCostMs=50,stopAt=0,audioStops=0,audioStopAt=0;
inline bool touchDown=false;''')
    usb = replace_once(usb, 'int read(){assert(', 'int read(){usb_model::advance(usb_model::readCostMs);assert(')
    (directory / 'boot_usb_model.h').write_text(usb)
    h = (ROOT / 'tests/boot_runtime_support.h').read_text()
    h = replace_once(h, 'unsigned getFreeHeap(){return 133732;}',
                     'unsigned getFreeHeap(){++usb_model::telemetryCalls;usb_model::advance(usb_model::telemetryCostMs);return 133732;}')
    h = replace_once(h, 'void setBrightness(int){}', 'void setBrightness(int){}void wakeup(){}void sleep(){}')
    h = replace_once(h, 'struct M5Fake{', '''struct TouchFake {
 struct Detail{int x=200,y=20;bool isPressed()const{return usb_model::touchDown ||
     (usb_model::stopAt && appNow()>=usb_model::stopAt);}} detail;
 const Detail& getDetail(){++usb_model::inputCalls;return detail;}
};
struct M5Fake{TouchFake Touch;''')
    h = replace_once(h, 'struct FaceFake{', 'struct FaceFake{void setEnabled(bool){}')
    h = replace_once(h, 'inline bool begin(){return true;}inline void update(uint32_t){}', '''inline bool begin(){return true;}inline void update(uint32_t){}
inline void advertise(){assert(false);}inline bool tapAgent(int){assert(false);return false;}
inline bool tapAction(int){assert(false);return false;}inline bool microphone(bool){assert(false);return false;}''')
    h = replace_once(h, 'inline Snapshot snapshot(){return {};}', '''inline Snapshot snapshot(){return {};}
inline void allowPage(bool){}inline void stop(){++usb_model::audioStops;usb_model::audioStopAt=appNow();}
inline bool request(Command){assert(false);return false;}''')
    h = replace_once(h, 'unsigned onCalls=0,uartCalls=0;', 'unsigned onCalls=0,uartCalls=0;bool powered=false;')
    h = replace_once(h, 'void setServoPowerEnabled(bool on){if(on){++onCalls;assert(false);}}',
                     'void setServoPowerEnabled(bool on){powered=on;if(on){++onCalls;assert(false);}}')
    h = replace_once(h, 'int servoPowerGateLevel(){return 0;}', 'int servoPowerGateLevel(){return powered?1:0;}')
    (directory / 'serial_fairness_support.h').write_text(h)


SCENARIOS = r'''
void enqueue(const std::string& s){for(char c:s)usb_model::rx.push_back(c);}
void callSerial(){serialCommands();}
void validateLines(bool modelWarmup){
 for(const auto& line:usb_model::lines){
  JsonDocument d;assert(!deserializeJson(d,line));
  assert(d["kind"].as<std::string>()=="vibe_pet_status");
  assert(d["agents"].as<JsonArray>().size()==6);
  assert(d["servo_power_gate"].as<int>()==0 || modelWarmup);
  assert(d["affection"].as<int>()==481);
 }
 assert(statusWriter.partial()==0&&statusWriter.oversize()==0);
}
int main(int argc,char** argv){
 assert(argc==4);const bool fixed=std::string(argv[2])=="fixed";
 const std::string scenario=argv[3];
 if(scenario=="startup")usb_model::hostNext=1000;
 setup();
 const unsigned setupCalls=usb_model::telemetryCalls;
 unsigned maximumCalls=0,maximumSerialMs=0,stopLatency=0,loops=0;
 bool parserOK=true;
 if(scenario=="startup") {
  const auto end=appNow()+1500;
  while(appNow()<end){
   const unsigned before=usb_model::telemetryCalls;loop();++loops;
   maximumCalls=std::max(maximumCalls,usb_model::telemetryCalls-before);
   maximumSerialMs=std::max(maximumSerialMs,maxSerialUs/1000);
  }
  if(fixed)assert(maximumCalls<=1 && imuGuard.gapCount()==0 && imuGuard.ready(appNow()));
  else assert(maximumCalls>=8 && imuGuard.gapCount()>0);
 } else {
  // Startup queues are empty here. Warm up the actual IMU/router via real loop.
  while(!imuGuard.ready(appNow()) || loopCount<15)loop();
  imuGuard.resetTimingStats();maxSerialUs=0;lastTelemetry=appNow();
  if(scenario=="burst"||scenario=="stop"||scenario=="flood"||scenario=="slow"||scenario=="too_slow"||scenario=="backpressure") {
   if(scenario=="slow")usb_model::telemetryCostMs=60;
   if(scenario=="too_slow")usb_model::telemetryCostMs=130;
   if(scenario=="backpressure") {
    usb_model::hostReading=false;usb_model::tx.assign(8000,'x');
    // This case observes foreground fairness, not output validity of seed bytes.
    usb_model::lines.clear();
   }
   for(int i=0;i<8;++i)usb_model::inputStatus();
   if(scenario=="stop")usb_model::stopAt=appNow()+2;
   if(scenario=="stop" || scenario=="too_slow") {
    // Model an already powered warm-up; no GPIO/servo call is issued.
    assert(imuGuard.arm(appNow()));servoPowered=true;M5StackChan.powered=true;
    servoWarmup.start(appNow());
   }
   // A requested status and periodic status become due in the SAME first loop.
   lastTelemetry=appNow()-5000;
   unsigned firstRemaining=0;
   for(unsigned i=0;i<(scenario=="flood"?120U:20U);++i){
    if(scenario=="flood")usb_model::inputStatus();
    const unsigned before=usb_model::telemetryCalls;loop();++loops;
    maximumCalls=std::max(maximumCalls,usb_model::telemetryCalls-before);
    maximumSerialMs=std::max(maximumSerialMs,maxSerialUs/1000);
    if(i==0)firstRemaining=usb_model::rx.size();
    if(usb_model::audioStops && !stopLatency)stopLatency=usb_model::audioStopAt-usb_model::stopAt;
   }
   if(fixed){
    assert(maximumCalls<=1 && firstRemaining>=7*16);
    if(scenario!="too_slow")assert(imuGuard.gapCount()==0);
    else {
     assert(imuGuard.gapCount()>0 && imuGuard.trips()==1 &&
            imuGuard.lastTrip()==pet_imu::Trip::Continuity);
     assert(!servoPowered && !M5StackChan.powered && !servoWarmup.pending());
    } // One 130ms handler still trips the unchanged production IMU interlock.
    if(scenario=="stop") {
     assert(usb_model::audioStops==1 && stopLatency<=80);
     assert(!servoPowered && !M5StackChan.powered && !servoWarmup.pending());
    }
   } else if(scenario=="burst" || scenario=="flood") {
    assert(maximumCalls>=8 && imuGuard.gapCount()>0);
   }
   if(scenario=="backpressure")assert(statusWriter.backpressure()>0);
   else assert(usb_model::rx.empty() || scenario=="flood");
  } else if(scenario=="parser") {
   usb_model::telemetryCostMs=0;const auto initial=usb_model::telemetryCalls;
   enqueue("{\"op\":\"sta");callSerial();assert(usb_model::telemetryCalls==initial);
   enqueue("tus\"}\r\n{bad}\n"+std::string(300,'x')+"\n{\"op\":\"status\"}\n");
   for(int i=0;i<32 && !usb_model::rx.empty();++i)callSerial();
   assert(usb_model::rx.empty()&&serialLine.empty()&&!discardSerialLine);
   assert(usb_model::telemetryCalls==initial+2); // No extra reply for invalid/oversized line.
   const auto calls=usb_model::telemetryCalls;
   usb_model::readCostMs=1;enqueue("{\"op\":\"status\"}\n");
   for(int i=0;i<32 && !usb_model::rx.empty();++i){
    const auto start=appNow();callSerial();
    maximumSerialMs=std::max(maximumSerialMs,uint32_t(appNow()-start));++loops;
   }
   assert(usb_model::telemetryCalls==calls+1);
   if(fixed)assert(maximumSerialMs<=2 && loops>=8);
   // micros wraps while processing a partial line; state survives into next loop.
   usb_model::now=4294966;enqueue("{\"op\":\"status\"}\n");
   for(int i=0;i<32 && !usb_model::rx.empty();++i){
    const auto start=appNow();callSerial();
    if(fixed)assert(uint32_t(appNow()-start)<=2);
   }
   assert(usb_model::rx.empty()&&usb_model::telemetryCalls==calls+2);
  } else {assert(false);}
 }
 if(scenario!="backpressure")validateLines(scenario=="stop" || scenario=="too_slow");
 assert(M5StackChan.onCalls==0 && M5StackChan.uartCalls==0 && motionCommands==0);
 JsonDocument result;result["label"]=argv[1];result["scenario"]=scenario;
 result["loops"]=loops;result["modeled_status_cost_ms"]=usb_model::telemetryCostMs;
 result["max_responses_per_loop"]=maximumCalls;result["max_serial_ms"]=maximumSerialMs;
 result["imu_max_gap_ms"]=imuGuard.maxGapMs();result["imu_gap_count"]=imuGuard.gapCount();
 result["imu_samples"]=imuGuard.samples();result["imu_ready"]=imuGuard.ready(appNow());
 result["imu_trips"]=imuGuard.trips();result["final_powered"]=servoPowered;
 result["input_calls"]=usb_model::inputCalls;result["stop_executions"]=usb_model::audioStops;
 result["modeled_stop_latency_ms"]=stopLatency;result["parser_ok"]=parserOK;
 result["status_sent"]=statusWriter.sent();result["backpressure"]=statusWriter.backpressure();
 result["partial"]=statusWriter.partial();result["usb_rx_remaining"]=usb_model::rx.size();
 result["setup_status_calls"]=setupCalls;result["payload_fields_preserved"]=true;
 std::string output;serializeJson(result,output);puts(output.c_str());
}
'''


def build(label, root):
    directory = OUT / 'generated' / label
    directory.mkdir(parents=True, exist_ok=True)
    support(directory)
    main = (root / 'src/main.cpp').read_text()
    code = ('#define BOOT_HAS_DIAGNOSTICS 1\n' if 'Diagnostics diagnostics()' in (root / 'src/LocalAudio.h').read_text() else '')
    code += '#include "serial_fairness_support.h"\n'
    cdc = driver_source(root)
    for sig in ['bool HWCDC::isCDC_Connected()', 'int HWCDC::availableForWrite(void)',
                'size_t HWCDC::write(const uint8_t *buffer, size_t size)']:
        code += body(cdc, sig) + '\n'
    code += main[main.index('Page page='):main.index('void applyAffection(')]
    code += r'''
void applyAffection(const AffectionApplyResult&,uint32_t){}
void drawPetHud(M5Canvas&){}
void drawPet(uint32_t){++lumi.frames;usb_model::advance(usb_model::renderCostMs);}
void drawGallery(uint32_t){assert(false);}void drawAudio(uint32_t){assert(false);}
void drawControl(uint32_t){assert(false);}void dumpFrame(){assert(false);}
void updatePet(uint32_t){} // No rear contact in this status/input scheduling test.
'''
    signatures = ['bool selectedFresh(', 'bool selectedWaiting(', 'bool selectedApproval(', 'uint32_t hostColor(',
                  'int requestServoPower(', 'bool powerOffContext()', 'void updatePowerDiagnostics()', 'void motionOff(', 'void failMotion(', 'void failWarmup(', 'void failImu(',
                  'void switchPage(', 'void setScreen(', 'void execute(', 'void updateInput(',
                  'void showBootHealth(', 'void updateImu(uint32_t now)', 'void updateHostCue(', 'void updateMotion(',
                  'void updateLights(', 'void motionDiagnostics()', 'void powerDiagnostics()', 'void telemetry()',
                  'bool serialCommands()' if 'bool serialCommands()' in main else 'void serialCommands()',
                  'void setup()', 'void loop()']
    for sig in signatures:
        if sig in main:
            code += body(main, sig) + '\n'
    source = directory / 'test.cpp'
    source.write_text(code + SCENARIOS)
    binary = directory / 'test'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(root / 'src'), '-Itests', '-I.pio/libdeps/stackchan-vibe-pet/ArduinoJson/src',
                    '-D', 'VIBESTACKCHAN_FIRMWARE_VERSION="offline-fairness-test"', str(source), '-o', str(binary)],
                   cwd=ROOT, check=True, timeout=45)
    return binary, hashlib.sha256(source.read_bytes()).hexdigest()


def main():
    global OUT
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixed', action='store_true')
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    if a.output:
        OUT=a.output
        OUT.mkdir(parents=True,exist_ok=True)
    cases = [('audio4', ROOT / 'evidence/audio-volume-20261102/release-audio4'),
             ('integration3', ROOT / 'evidence/boot-regression-20261102/release-integration3')]
    if a.fixed:
        cases.append(('current', ROOT))
    records = []
    for label, root in cases:
        binary, sha = build(label, root)
        scenarios = ['startup', 'burst', 'stop']
        if label == 'current':
            scenarios += ['flood', 'parser', 'backpressure', 'slow', 'too_slow']
        for scenario in scenarios:
            run = subprocess.run([str(binary), label, 'fixed' if label == 'current' else 'old', scenario],
                                 cwd=ROOT, capture_output=True, text=True, timeout=10)
            if run.returncode:
                raise RuntimeError({'label': label, 'scenario': scenario, 'returncode': run.returncode,
                                    'stdout': run.stdout, 'stderr': run.stderr})
            record = json.loads(run.stdout)
            record['generated_source_sha256'] = sha
            records.append(record)
            print(json.dumps(record), flush=True)
    name = 'fairness.json' if a.fixed else 'baseline.json'
    (OUT / name).write_text(json.dumps({'scope': 'offline actual foreground functions; fake peripherals/RTOS and injected execution costs, not hardware timing',
                                      'cases': records}, indent=2) + '\n')


if __name__ == '__main__':
    main()
