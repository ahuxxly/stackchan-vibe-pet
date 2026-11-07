"""Real input/main power gates + audio worker, fake IO; no device access."""
from pathlib import Path
import subprocess
from app_integration_test import render
ROOT=Path(__file__).resolve().parents[1]
scenario=r'''
void step(unsigned ms=40){for(unsigned n=0;n<ms;n+=40){appClock+=40;updateImu(appClock);updateMotion(appClock);}}
void down(int x,int y){M5.Touch.detail={true,x,y};updateInput(appClock);}
void up(){M5.Touch.detail.down=false;updateInput(appClock);}
void attempt(){step(800);assert(imuGuard.ready(appClock));down(56,31);step(1440);up();}
int main(){
 localAudioReady=local_audio::begin();assert(localAudioReady);audioWait([]{return !local_audio::busy();});
 // Configuration failure blocks ON before IMU arming; visible actual HUD reason.
 M5StackChan.powerConfigReady=false;attempt();
 assert(!strcmp(motionGateReason,"power_config")&&M5StackChan.onCalls==0);
 assert(!imuGuard.armed()&&!M5StackChan.prepared&&motionCommands==0);
 assert(!strcmp(capturePetHud(appClock).moveLabel,"POWER ERR"));
 assert(strstr(capturePetHud(appClock).feedback.data(),"configuration"));
 M5StackChan.powerConfigReady=true;
 M5StackChan.writeOnOK=false;attempt();
 assert(!strcmp(motionGateReason,"power_write"));
 assert(powerWrites.enable.sampled&&powerWrites.enable.result==power_probe::WriteResult::Failed);
 assert(powerWrites.disable.sampled&&powerWrites.disable.result==power_probe::WriteResult::Succeeded);
 assert(powerWrites.enable.attempt==motionEnableAttempts&&motionStartPowerLevel==-1);
 assert(!powerCutFault&&!servoPowered&&!movement.enabled()&&!servoWarmup.pending()&&!M5StackChan.prepared);
 assert(!motionPoseCaptured&&!motionUartSampled&&motionCommands==0);
 assert(!strcmp(capturePetHud(appClock).moveLabel,"POWER ERR"));
 // An acknowledged ON still needs actual HIGH and valid poses. LOW is no fallback.
 M5StackChan.writeOnOK=true;M5StackChan.gateOK=false;attempt();
 assert(!strcmp(motionGateReason,"power_gate")&&motionStartPowerLevel==0);
 assert(powerWrites.enable.result==power_probe::WriteResult::Succeeded&&!servoPowered);
 // Failed cutoff stays visible and blocks new ON/audio/probe, not reported physical OFF.
 M5StackChan.writeOffOK=false;execute(Intent::Stop,appClock);up();
 audioWait([]{return !local_audio::busy();});
 assert(powerCutFault&&!powerOffContext()&&!strcmp(capturePetHud(appClock).moveLabel,"POWER ERR"));
 assert(strstr(capturePetHud(appClock).feedback.data(),"unconfirmed"));
 const auto writes=M5StackChan.writes;attempt();assert(M5StackChan.writes==writes);
 assert(!strcmp(motionGateReason,"power_off_failed"));
 execute(Intent::OpenAudio,appClock);audioWait([]{return !local_audio::busy();});
 execute(Intent::AudioRecord,appClock);assert(micCalls==0&&speakerCalls==0);
 assert(!local_audio::diagnostics().allowed);
 M5StackChan.writeOffOK=true;execute(Intent::Stop,appClock);
 audioWait([]{return !local_audio::busy();});assert(!powerCutFault);
 assert(powerWrites.enable.result==power_probe::WriteResult::Succeeded); // cutoff did not erase ON record
 assert(codex_hid::actions.empty()&&motionCommands==0);
 audioTestEnd=true;audioTestThread.join();heap_caps_free(audioTestMemory);
 puts("PASS actual touch/execute/HUD/audio gates: failed ON, acknowledged-but-LOW, failed OFF persistent warning and interlocks, separate histories; fake IO, zero targets");
}
'''
p=ROOT/'.test-bin/power_app_test.cpp';p.write_text(render(ROOT,scenario))
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Itests','-Itests/stubs/audio','-Isrc',str(p),'src/LocalAudio.cpp','-o',str(p.with_suffix(''))],cwd=ROOT,check=True)
subprocess.run([str(p.with_suffix(''))],check=True,timeout=15)
