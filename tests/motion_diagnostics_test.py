"""Real command parser/diagnostic builder/loop and HWCDC; fake hardware only."""
from pathlib import Path
import subprocess
import serial_fairness_test as harness

ROOT=Path(__file__).resolve().parents[1]
harness.OUT=ROOT/'.test-bin/motion-diagnostics'
harness.SCENARIOS=r'''
void enqueue(const std::string& s){for(char c:s)usb_model::rx.push_back(c);}
JsonDocument query(){
 const auto before=statusWriter.sent();enqueue("{\"op\":\"motion_diag\"}\n");
 assert(serialCommands());modelIsr();assert(statusWriter.sent()==before+1);
 JsonDocument d;assert(!deserializeJson(d,usb_model::lines.back()));
 assert(usb_model::lines.back().size()<1024&&d["kind"].as<std::string>()=="motion_diag");return d;
}
int main(){
 setup();while(!imuGuard.ready(appNow()))loop();
 JsonDocument empty=query();assert(empty["attempt"]==0&&!empty["pose_sampled"].as<bool>());
 assert(empty["raw"].isNull()&&!empty["uart_sampled"].as<bool>());
 assert(empty["fw"].as<std::string>()==firmware_version::kBuild&&empty["outputs"]==0&&empty["inputs"]==0);
 assert(empty["imu_ready"].as<bool>()&&empty["imu_gaps"]==0&&empty["loop"].as<unsigned>()>0);
 for(auto v:empty["power"].as<JsonObject>())assert(v.value().as<int>()==-1);
 // Simulate a historical cache, not a hardware movement or real servo response.
 motionEnableAttempts=9;motionAttemptAt=appNow()-2000;
 motionPowerConfig={1,1,1,0,1,0};motionPowerAt={10,50,90,130,170,210};
 motionUartSampled=true;motionUartStage="ready";motionUartError=0;
 motionPoseCaptured=true;motionPoseAt=appNow()-700;motionDiagReads=2;
 motionPoseYaw=0;motionPosePitch=450;motionRaw={0,714};motionReplyError={0,0};motionReplyStatus={0,4};
 motionGateReason="user";
 JsonDocument cached=query();assert(cached["attempt"]==9&&cached["attempt_age_ms"]==2000);
 assert(cached["pose_age_ms"]==700&&cached["raw"][0]==0&&cached["reply_status"][1]==4);
 assert(cached["power"]["input"]==1&&!cached["requested_power"].as<bool>());
 assert(cached["power_at_ms"][5]==210&&cached["targets"]==0);
 usb_model::advance(40);loop();auto live=query();
 assert(live["loop"].as<unsigned>()>cached["loop"].as<unsigned>());
 assert(live["imu_samples"].as<unsigned>()>cached["imu_samples"].as<unsigned>());
 const auto bytes=usb_model::lines.back().size();
 // Worst-width counters, negative sentinels and the longest current failure tag
 // stay below 1 KiB; old state remains historical, never silently LIVE.
 motionEnableAttempts=UINT32_MAX;motionAttemptAt=appNow()+1;motionPowerAt.fill(UINT32_MAX);
 motionDiagReads=UINT32_MAX;motionCommands=UINT32_MAX;motionPoseAt=appNow()+1;
 motionPoseYaw=motionPosePitch=-32768;motionRaw.fill(-32768);motionReplyError.fill(-1);motionReplyStatus.fill(-1);
 motionUartStage="already_open";motionUartError=-2147483647;motionGateReason="invalid_feedback";
 auto widest=query();assert(widest["pose_age_ms"].as<uint32_t>()==UINT32_MAX);
 const auto maxBytes=usb_model::lines.back().size();assert(maxBytes<1024);motionCommands=0;
 // Exercise actual loop sharing with an overdue periodic status: one response.
 const auto sent=statusWriter.sent();const auto frames=usb_model::telemetryCalls;
 enqueue("{\"op\":\"motion_diag\"}\n{\"op\":\"motion_diag\"}\n");lastTelemetry=appNow()-5000;
 loop();assert(statusWriter.sent()==sent+1&&!usb_model::rx.empty());
 assert(usb_model::telemetryCalls==frames); // no second, periodic full snapshot
 loop();assert(statusWriter.sent()==sent+2&&usb_model::rx.empty());
 // Partial commands survive a loop, malformed input has no side effects.
 enqueue("{\"op\":\"motion_");assert(!serialCommands());
 enqueue("diag\"}\n{bad}\n");assert(serialCommands());assert(!serialCommands());
 const auto pressure=statusWriter.backpressure();const auto sentAt=statusWriter.sent();
 usb_model::hostReading=false;usb_model::tx.assign(8000,'x');
 enqueue("{\"op\":\"motion_diag\"}\n");assert(serialCommands());
 assert(statusWriter.backpressure()==pressure+1&&statusWriter.sent()==sentAt);
 assert(M5StackChan.onCalls==0&&M5StackChan.uartCalls==0&&motionCommands==0);
 assert(statusWriter.partial()==0&&statusWriter.oversize()==0);
 printf("PASS actual cached motion_diag/parser/loop: %zu normal/%zu worst-width bytes, unknown vs zero, attempt/time/wrap, GPIO cache not live voltage, one response/loop, partial command, backpressure; zero actuator calls, fake USB/IO\n",bytes,maxBytes);
}
'''
binary,_=harness.build('current',ROOT)
subprocess.run([str(binary)],check=True,timeout=10)
