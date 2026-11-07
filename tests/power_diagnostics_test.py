"""Actual OFF diagnostic parser/loop/full packet; modeled USB and GPIO only."""
from pathlib import Path
import subprocess
import serial_fairness_test as h
ROOT=Path(__file__).resolve().parents[1]
h.OUT=ROOT/'.test-bin/power-diagnostics'
h.SCENARIOS=r'''
void enqueue(const std::string& s){for(char c:s)usb_model::rx.push_back(c);}
JsonDocument query(bool start=false){
 const auto reads=M5StackChan.registerReads;
 enqueue(start?"{\"op\":\"power_diag_off\",\"start\":true}\n":"{\"op\":\"power_diag_off\"}\n");
 assert(serialCommands());modelIsr();assert(M5StackChan.registerReads==reads);
 JsonDocument d;assert(!deserializeJson(d,usb_model::lines.back()));
 assert(d["kind"].as<std::string>()=="power_diag_off"&&usb_model::lines.back().size()<2048);return d;
}
int main(){
 setup();while(!imuGuard.ready(appNow()))loop();const auto initialReads=M5StackChan.registerReads;
 auto empty=query();assert(empty["state"].as<std::string>()=="idle"&&initialReads==0);
 assert(!empty["writes"]["on"]["sampled"].as<bool>()&&empty["writes"]["on"]["result"]==-1);
 assert(empty["writes"]["off"]["sampled"].as<bool>()&&empty["writes"]["off"]["result"]==1);
 auto accepted=query(true);assert(accepted["accepted"].as<bool>()&&accepted["state"].as<std::string>()=="pending");
 while(powerSnapshot.state==power_probe::State::Pending){const auto n=M5StackChan.registerReads;loop();assert(M5StackChan.registerReads<=n+1);}
 auto complete=query();assert(complete["state"].as<std::string>()=="complete"&&M5StackChan.registerReads==8);
 assert(complete["fields"].as<JsonArray>().size()==8&&complete["off_context"].as<bool>());
 for(auto f:complete["fields"].as<JsonArray>())assert(f["valid"].as<bool>()&&f["value"]==0&&f["finished_ms"].as<uint32_t>()>f["started_ms"].as<uint32_t>());
 assert(imuGuard.gapCount()==0&&imuGuard.ready(appNow()));
 assert(!query(true)["accepted"].as<bool>()&&powerSnapshot.state==power_probe::State::Complete);
 usb_model::advance(5000);query(true);const auto n=M5StackChan.registerReads;
 micOn=true;updatePowerDiagnostics();assert(powerSnapshot.state==power_probe::State::Cancelled&&M5StackChan.registerReads==n);micOn=false;
 usb_model::advance(5000);servoPowered=true;assert(!query(true)["accepted"].as<bool>());servoPowered=false;
 query(true);++motionEnableAttempts;updatePowerDiagnostics();assert(powerSnapshot.state==power_probe::State::Cancelled);
 usb_model::advance(5000);query(true);M5StackChan.registerOK=false;updatePowerDiagnostics();
 auto failed=query();assert(failed["state"].as<std::string>()=="read_failed"&&!failed["fields"][0]["valid"].as<bool>()&&failed["fields"][0]["value"]==-1);
 M5StackChan.registerOK=true;usb_model::advance(5000);query(true);M5StackChan.registerValue=1;updatePowerDiagnostics();
 assert(query()["state"].as<std::string>()=="input_high");M5StackChan.registerValue=0;
 // Worst-width cached fields remain one bounded packet; polling never reads IO.
 powerSnapshot.started=UINT32_MAX;powerSnapshot.attempt=UINT32_MAX;powerSnapshotRequests=UINT32_MAX;
 for(auto& s:powerSnapshot.samples){s={255,true,UINT32_MAX,UINT32_MAX};}
 powerWrites.record(true,0,UINT32_MAX,UINT32_MAX,UINT32_MAX);powerWrites.record(false,-1,UINT32_MAX,UINT32_MAX,UINT32_MAX);
 query();const auto widest=usb_model::lines.back().size();
 const auto sent=statusWriter.sent();lastTelemetry=appNow()-5000;
 enqueue("{\"op\":\"power_diag_off\"}\n{\"op\":\"power_diag_off\"}\n");loop();assert(statusWriter.sent()==sent+1&&!usb_model::rx.empty());
 loop();assert(statusWriter.sent()==sent+2&&usb_model::rx.empty());
 const auto pressure=statusWriter.backpressure();usb_model::hostReading=false;usb_model::tx.assign(8000,'x');
 enqueue("{\"op\":\"power_diag_off\"}\n");assert(serialCommands());assert(statusWriter.backpressure()==pressure+1);
 assert(M5StackChan.onCalls==0&&M5StackChan.uartCalls==0&&motionCommands==0&&statusWriter.partial()==0);
 printf("PASS actual OFF parser/loop: explicit start, cached polls, <=1 read/loop, OFF cancellation/epoch/NACK/HIGH/cooldown, %zu B widest packet, shared output budget/backpressure; modeled IO only\n",widest);
}
'''
binary,_=h.build('current',ROOT)
subprocess.run([str(binary)],check=True,timeout=15)
