std::string footer(const display_model::Frame& f) {
 std::string value;unsigned count=0;
 for(const auto& op:f.ops)if(op.kind=="text" && op.text.size()>1 && op.text[0]=='L' &&
     (op.text[1]=='-' || (op.text[1]>='0'&&op.text[1]<='9'))) {
   assert(op.x==1&&op.y==232&&op.font==0&&op.datum==top_left);
   assert(op.text.size()*6+1<=320);value=op.text;++count;
 }
 assert(count==1);return value;
}
void frameJson(JsonArray list,const char* name,const display_model::Frame& f) {
 auto entry=list.add<JsonObject>();entry["name"]=name;auto ops=entry["ops"].to<JsonArray>();
 for(const auto& op:f.ops){auto d=ops.add<JsonObject>();d["kind"]=op.kind;d["text"]=op.text;
  d["x"]=op.x;d["y"]=op.y;d["w"]=op.w;d["h"]=op.h;d["font"]=op.font;d["datum"]=op.datum;d["color"]=op.color;}
}
int main(int argc,char** argv) {
 assert(argc==2);const std::string mode=argv[1];
 display_model::failBootAllocation=mode=="boot-alloc-fail";
 health_probe::failLumi=mode=="legacy-fallback";
 display_model::onText=[](const char* s){if(s[0]=='L' && (s[1]=='-' || (s[1]>='0'&&s[1]<='9')))usb_model::advance(1);};
 display_model::onPush=[](int w,int h){assert(w==320);usb_model::advance(h==8?1:50);};
 if(mode=="setup-block")display_model::onPush=[](int,int h){
   if(h==8 && display_model::frames.back().ops.back().text=="BOOT 8 USB STATE")health_probe::blockTelemetry=true;
 };
 bool caught=false;
 try{setup();}catch(int e){assert(e==42);caught=true;}
 if(mode=="setup-block") {
   assert(caught && runtimeHealth.completed()==0 && loopCount==0);
   assert(display_model::frames.back().ops.back().text=="BOOT 8 USB STATE");
   assert(statusWriter.sent()==0 && M5StackChan.onCalls==0);
   puts("{\"case\":\"setup-block\",\"pass\":true,\"last_visible_stage\":\"BOOT 8 USB STATE\",\"completed_loops\":0,\"usb_status_sent\":0}");return 0;
 }
 assert(!caught && !bootHealth.allocated && runtimeHealth.completed()==0);
 assert(bootHealthAvailable!=display_model::failBootAllocation);
 if(bootHealthAvailable){
   assert(display_model::frames.size()==10);
   for(unsigned i=0;i<10;++i){const auto& f=display_model::frames[i];
     assert(f.x==0&&f.y==232&&f.w==320&&f.h==8);
     assert(f.ops.back().text==std::string("BOOT ")+std::to_string(i)+" "+runtime_health::name(static_cast<runtime_health::Boot>(i)));
   }
 }else assert(display_model::frames.empty());
 display_model::frames.clear();
 while(!imuGuard.ready(appNow()) || runtimeHealth.completed()<20)loop();
 assert(runtimeHealth.completed()==loopCount && !display_model::frames.empty());
 if(mode=="loop-block") {
   const auto before=runtimeHealth.completed();const std::string label=runtimeHealth.text();
   usb_model::inputStatus();health_probe::blockTelemetry=true;caught=false;
   try{loop();}catch(int e){assert(e==42);caught=true;}
   assert(caught && loopCount==before+1 && runtimeHealth.completed()==before);
   usb_model::advance(5000);lumi.requestFrame();drawPet(appNow());
   assert(footer(display_model::frames.back())==label && runtimeHealth.completed()==before);
   puts("{\"case\":\"loop-block\",\"pass\":true,\"wall_time_and_redraw_cannot_advance_counter\":true}");return 0;
 }
 const auto begin=appNow(),before=runtimeHealth.completed(),imuBefore=imuGuard.samples();
 const auto outputBefore=usb_model::lines.size();
 usb_model::hostReading=false;usb_model::hostNext=appNow()+1000;
 unsigned peakSubmissions=0;
 while(appNow()-begin<35000) {
   const auto count=display_model::frames.size();loop();
   peakSubmissions=std::max(peakSubmissions,unsigned(display_model::frames.size()-count));
   assert(display_model::frames.size()-count<=1);
   assert(runtimeHealth.completed()==loopCount);
 }
 assert(usb_model::lines.size()==outputBefore && statusWriter.backpressure()>0);
 assert(runtimeHealth.completed()>before+400 && imuGuard.samples()>imuBefore+400);
 assert(imuGuard.ready(appNow()) && imuGuard.gapCount()==0 && imuGuard.maxGapMs()<=120);
 assert(M5StackChan.onCalls==0 && M5StackChan.uartCalls==0 && motionCommands==0 && codex_hid::sentActions()==0);
 std::vector<std::string> labels;
 for(const auto& f:display_model::frames){auto s=footer(f);if(labels.empty() || s!=labels.back())labels.push_back(s);}
 assert(labels.size()>=30 && labels.size()<=38);
 const auto count=display_model::frames.size();setScreen(false);
 for(int i=0;i<30;++i)loop();assert(display_model::frames.size()==count);
 setScreen(true);loop();assert(display_model::frames.size()>count);
 JsonDocument report;
 report["case"]=mode;report["pass"]=true;report["boot_canvas_released"]=!bootHealth.allocated;
 report["loop_delta"]=runtimeHealth.completed()-before;report["imu_delta"]=imuGuard.samples()-imuBefore;
 report["imu_max_gap_ms_model"]=imuGuard.maxGapMs();report["imu_gap_count"]=imuGuard.gapCount();
 report["lcd_submissions_per_loop_max"]=peakSubmissions;report["distinct_1Hz_labels"]=labels.size();
 report["usb_output_frames_during_outage"]=usb_model::lines.size()-outputBefore;
 report["footer_cost_us_injected"]=maxHealthComposeUs;
 if(mode=="outage" || mode=="legacy-fallback") {
   auto layouts=report["layouts"].to<JsonArray>();
   for(int p=0;p<4;++p){
     const auto n=display_model::frames.size();
     if(p==0){lumi.requestFrame();drawPet(appNow());}
     else if(p==1)drawControl(appNow());else if(p==2)drawAudio(appNow());else drawGallery(appNow());
     assert(display_model::frames.size()==n+1);const auto& frame=display_model::frames.back();footer(frame);
     frameJson(layouts,p==0?"PET":p==1?"CODEX":p==2?"AUDIO":"GALLERY",frame);
     for(const auto& op:frame.ops)if(op.kind=="text" && (op.text=="STOP"||op.text=="STOP / CLEAR"))assert(op.y<232);
     if(mode=="legacy-fallback")break;
   }
 }
 std::string output;serializeJson(report,output);puts(output.c_str());
}
