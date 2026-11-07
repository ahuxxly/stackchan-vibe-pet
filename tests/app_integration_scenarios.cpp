void step(unsigned ms=40){
 for(unsigned elapsed=0;elapsed<ms;elapsed+=40){
  appClock+=40;updateImu(appClock);
  updateMotion(appClock);
 }
}
void down(int x,int y){M5.Touch.detail={true,x,y};updateInput(appClock);}
void up(){M5.Touch.detail.down=false;updateInput(appClock);}
void tap(int x,int y,unsigned ms=80){down(x,y);step(ms);up();}
void audioIdle(){audioWait([]{return !local_audio::busy();});assert(owner==0);}
void petPage(){switchPage(Page::Pet);audioIdle();step(640);}
void enable(){step(640);tap(40,20,1040);step(880);assert(movement.enabled()&&servoPowered);}
int main(){
 localAudioReady=local_audio::begin();assert(localAudioReady);audioIdle();
 assert(!servoPowered&&micCalls==0&&speakerCalls==0);
 // Disabled IMU readiness is a wait message, never a motor command.
 down(40,20);up();assert(motionEnableAttempts==0&&!servoPowered);
 auto hud=capturePetHud(appClock);assert(!strcmp(hud.moveLabel,"WAIT"));
 assert(strstr(hud.feedback.data(),"disabled"));step(800);
 tap(280,20);assert(page==Page::Control);tap(160,20);assert(page==Page::Audio);audioIdle();
 const auto initialHid=codex_hid::actions.size();
 // Full manual record/play uses the actual asynchronous worker and 250 chunks.
 tap(240,150,1040);audioWait([]{return local_audio::snapshot().ready&&!local_audio::busy();});
 assert(local_audio::snapshot().samples==80000&&recordCalls==250&&owner==0);
 tap(50,200);audioWait([]{return local_audio::snapshot().phase==local_audio::Phase::Playing;});
 const auto stopped=stopExecutions;down(120,20);
 assert(stopExecutions==stopped+1);up();assert(stopExecutions==stopped+1);audioIdle();
 assert(!local_audio::snapshot().ready);for(int i=0;i<80000;++i)assert(static_cast<int16_t*>(audioTestMemory)[i]==0);
 // CLEAR must cancel a busy recording, not wait for the five-second job.
 tap(240,150,1040);audioWait([]{return local_audio::snapshot().phase==local_audio::Phase::Recording;});
 tap(240,200);audioIdle();assert(!local_audio::snapshot().ready&&local_audio::snapshot().samples==0);
 // Exit while recording, then traverse the actual AUDIO -> CONTROL -> PET path.
 tap(240,150,1040);audioWait([]{return local_audio::snapshot().phase==local_audio::Phase::Recording;});
 tap(35,20);assert(page==Page::Control);tap(30,20);assert(page==Page::Pet);audioIdle();
 assert(!local_audio::diagnostics().allowed&&!local_audio::snapshot().ready);
 // Every failure class cuts power and leaves a visible fault beyond 2.5 seconds.
 for(int fault=0;fault<5;++fault){
  petPage();M5StackChan.gateOK=fault!=0;M5StackChan.uartOK=fault!=1;M5StackChan.taskOK=fault!=2;
  M5StackChan.Motion.pose=fault==3?BoardStub::Pair{-32768,-32768}:fault==4?BoardStub::Pair{0,0}:BoardStub::Pair{0,450};
  const auto commands=motionCommands;tap(40,20,1040);step(1800);
  assert(!servoPowered&&!movement.enabled()&&motionCommands==commands&&*motionFaultDetail);
  step(3000);hud=capturePetHud(appClock);assert(strstr(hud.moveLabel,"ERR")&&*hud.feedback.data());
 }
 M5StackChan.gateOK=M5StackChan.uartOK=M5StackChan.taskOK=true;M5StackChan.Motion.pose={0,450};petPage();
 // Failed GPIO read is distinct from real LOW; diagnostics cannot reuse a prior pose.
 M5StackChan.gateReadOK=false;tap(40,20,1040);step(800);
 assert(!servoPowered&&!motionPoseCaptured&&!motionUartSampled&&motionDiagReads==0);
 assert(!strcmp(motionGateReason,"power_read")&&motionPowerConfig[0]==-1);
 M5StackChan.gateReadOK=true;petPage();
 // Optional register sampling is one read/tick, stops at first NACK, and cannot
 // itself send a target. It does not replace the existing HIGH + stable pose gate.
 M5StackChan.failDiagnosticField=3;const int beforeDiag=M5StackChan.diagnosticReads;
 tap(40,20,1040);
 for(int i=0;i<6;++i){const int reads=M5StackChan.diagnosticReads;step(40);assert(M5StackChan.diagnosticReads-reads<=1);}
 assert(M5StackChan.diagnosticReads==beforeDiag+3&&motionPowerConfig[3]==-1&&motionPowerConfig[4]==-1);
 const auto duringWarmup=motionCommands;assert(servoWarmup.pending()&&!movement.enabled());
 down(200,20);up();step(800);assert(!servoPowered&&motionCommands==duringWarmup);
 assert(M5StackChan.diagnosticReads==beforeDiag+3);M5StackChan.failDiagnosticField=-1;petPage();
 // Status refresh/connection revisions cannot cancel a local MOVE hold.
 down(40,20);step(520);codex_hid::data.connect(true);
 codex_hid::data.receive(0,0xff9600,1,1,0,appClock);step(520);up();step(880);
 assert(movement.enabled()&&!*motionFaultDetail);
 assert(motionPoseCaptured&&motionDiagReads==2&&motionUartSampled&&!strcmp(motionUartStage,"ready"));
 assert(motionPowerConfig[0]==1&&motionPowerConfig[4]==1);
 // Awaiting entering during DANCE remains a reason to cancel, unlike MOVE.
 codex_hid::data.receive(0,0x123456,1,1,0,appClock);down(120,20);step(320);
 codex_hid::data.receive(0,0xff9600,1,1,0,appClock);step(320);up();assert(!movement.active());
 codex_hid::data.receive(0,0x123456,1,1,0,appClock);
 tap(120,20,640);assert(movement.activity()==pet_motion::Activity::Dance);
 step(1000);assert(motionCommands>0);const auto commandCount=motionCommands;
 down(200,20);assert(!servoPowered&&!movement.enabled());up();step(500);assert(motionCommands==commandCount);
 // Returning to AUDIO after movement releases all actuator state; rapid page
 // re-entry cannot resurrect a queued recording or turn a late start into sound.
 down(280,20);step(40);codex_hid::data.receive(0,0xff9600,1,1,0,appClock);step(40);up();
 assert(page==Page::Control);tap(160,20);audioIdle();tap(240,150,1040);
 audioWait([]{return local_audio::snapshot().phase==local_audio::Phase::Preparing;});
 const auto micStarts=micCalls.load();switchPage(Page::Control);switchPage(Page::Audio);audioIdle();
 std::this_thread::sleep_for(std::chrono::milliseconds(1100));assert(micCalls==micStarts&&!local_audio::busy());
 // Sleep during recording cancels; wake with a held REC finger cannot restart.
 tap(240,150,1040);audioWait([]{return local_audio::snapshot().phase==local_audio::Phase::Recording;});
 setScreen(false);audioIdle();assert(!servoPowered&&!local_audio::snapshot().ready);
 setScreen(true);down(240,150);step(1600);up();audioIdle();assert(!local_audio::snapshot().ready&&!local_audio::busy());
 // Short press and drag cancellation remain silent.
 const auto captures=recordCalls.load();tap(240,150,960);audioIdle();assert(recordCalls==captures);
 down(240,150);step(1040);M5.Touch.detail.x=160;updateInput(appClock);up();audioIdle();assert(recordCalls==captures);
 assert(codex_hid::actions.size()==initialHid); // local pages never send host input
 // Real approval context still cancels on a changed status and on disconnect.
 switchPage(Page::Control);audioIdle();
 for(bool disconnect:{false,true}){
  codex_hid::data.connect(true);codex_hid::data.receive(0,0xff9600,1,1,0,appClock);
  down(50,150);step(400);
  if(disconnect)codex_hid::data.connect(false);else codex_hid::data.receive(0,0xff9600,1,2,0,appClock);
  step(400);up();assert(codex_hid::actions.size()==initialHid);
 }
 // A reconnect can make old epoch+revision (1+3) equal new (3+1).
 // Separate captured identities must cancel even without an intermediate poll.
 codex_hid::data=companion::State{};codex_hid::data.connect(true);
 for(int effect:{1,2,3})codex_hid::data.receive(0,0xff9600,1,effect,0,appClock);
 down(50,150);step(320);codex_hid::data.connect(false);codex_hid::data.connect(true);
 codex_hid::data.receive(0,0xff9600,1,1,0,appClock);step(320);up();
 assert(codex_hid::actions.size()==initialHid);
 petPage();
 // The actual updateImu callback must retain the reason after quiet recovery.
 for(unsigned gap:{119U,120U,121U,154U,249U,250U}){
  enable();appClock+=gap;updateImu(appClock);
  if(gap<=120){assert(servoPowered&&movement.enabled());motionOff();}
  else {
   assert(!servoPowered&&!movement.enabled()&&!strcmp(capturePetHud(appClock).moveLabel,"IMU STOP"));
   assert(strstr(motionFaultDetail,gap==250?"stale":"gap"));
   step(800);assert(imuGuard.ready(appClock));assert(!strcmp(capturePetHud(appClock).moveLabel,"IMU STOP"));
  }
 }
 for(int hazard=0;hazard<4;++hazard){
  enable();M5.Imu.data.accel=hazard==0?FakeImu::V{0,0,2.2f}:
    hazard==1?FakeImu::V{0,0,0.2f}:hazard==2?FakeImu::V{.6f,0,.8f}:FakeImu::V{NAN,0,1};
  step(80);assert(!servoPowered&&!movement.enabled()&&!strcmp(capturePetHud(appClock).moveLabel,"IMU STOP"));
  const std::string reason=motionFaultDetail;M5.Imu.data.accel={0,0,1};step(800);
  assert(imuGuard.ready(appClock)&&reason==motionFaultDetail);
 }
 enable();setScreen(false);assert(!servoPowered&&!movement.enabled());
 audioIdle();audioTestEnd=true;audioTestThread.join();heap_caps_free(audioTestMemory);
 puts("PASS actual main page/input/motion/HUD/updateImu callback + actual audio worker: full PET-AUDIO/STOP/exit chain, power/init/pose faults, awaiting changes during MOVE/navigation/DANCE, approval cancellation, sleep/wake, 119/120/121/154/249/250ms continuity and strong/low-gravity/tilt/invalid cutoff with persistent reason; fake physical IO only");
}
