#include "MuseRenderer.h"
#include "MuseAssets.h"
#include <LittleFS.h>

bool MuseRenderer::begin() {
  canvas_.setPsram(true);canvas_.setColorDepth(16);
  ready_=canvas_.createSprite(320,240)!=nullptr;
  lastTile_.setPsram(true);lastTile_.setColorDepth(16);
  ready_=lastTile_.createSprite(168,168)!=nullptr && ready_;
  for(auto& slot:slots_) {
    slot.pixels.setPsram(true);slot.pixels.setColorDepth(16);
    ready_=slot.pixels.createSprite(168,168)!=nullptr && ready_;
  }
  decode_.setPsram(false);decode_.setColorDepth(16);
  ready_=decode_.createSprite(168,168)!=nullptr && ready_;
  uint32_t total=0;
  for(int id=0;id<kCount;++id){offsets_[id]=total;total+=muse_assets::files[id/8][id%8].bytes;}
  encoded_=static_cast<uint8_t*>(ps_malloc(total));ready_=encoded_ && ready_;
  if(!ready_)return false;
  // Startup only, while the servo rail is LOW and before the interactive loop.
  // Runtime worker uses validated RAM bytes and never opens flash files.
  for(int id=0;id<kCount;++id) {
    char path[48];snprintf(path,sizeof(path),"/lumi/%s_%u.jpg",muse::name(static_cast<muse::Clip>(id/8)),id%8);
    const auto expected=muse_assets::files[id/8][id%8];const uint32_t at=micros();
    auto file=LittleFS.open(path,"r");
    bool ok=file && file.size()==expected.bytes && file.read(encoded_+offsets_[id],expected.bytes)==expected.bytes;
    if(ok)ok=(muse::crc32(encoded_+offsets_[id],expected.bytes)^0xffffffffU)==expected.crc;
    file.close();maxReadUs_=std::max(maxReadUs_,uint32_t(micros()-at));
    if(!ok){failed_[id].store(true);++failures_;}
    delay(1);
  }
  running_.store(true);
  if(xTaskCreatePinnedToCore(workerEntry,"lumi-jpeg",8192,this,1,&task_,0)!=pdPASS) {
    running_.store(false);ready_=false;return false;
  }
  Serial.printf("[lumi] async frames=%u encoded=%u cache=%u failures=%u\n",unsigned(kCount),total,unsigned(kSlots*168*168*2),failures_.load());
  return true;
}
void MuseRenderer::end() {
  stopping_.store(true);
  while(running_.load())delay(1);
  if(encoded_){free(encoded_);encoded_=nullptr;}
  ready_=false;
}
void MuseRenderer::pauseForLegacy(bool paused) {
  if(paused_.exchange(paused)==paused)return;
  if(paused)while(running_.load() && !parked_.load())delay(1);
}
void MuseRenderer::workerEntry(void* self) {
  auto* renderer=static_cast<MuseRenderer*>(self);renderer->worker();
  renderer->running_.store(false);vTaskDelete(nullptr);
}
bool MuseRenderer::cached(int id)const {
  for(const auto& slot:slots_) {const int value=slot.id.load();if(value==id || value==kReading+id)return true;}
  return false;
}
int MuseRenderer::claimSlot(int wanted) {
  for(int pass=0;pass<2;++pass)for(int i=0;i<kSlots;++i) {
    int value=slots_[i].id.load();
    if(value==-2 || value>=kReading)continue;
    if(pass==0 && value>=0)continue;
    if(value>=0 && value/8==wanted/8)continue; // Keep the current clip's useful frames.
    if(slots_[i].id.compare_exchange_strong(value,-2))return i;
  }
  return -1;
}
void MuseRenderer::worker() {
  while(!stopping_.load()) {
    parked_.store(false);
    if(paused_.load()){parked_.store(true);vTaskDelay(1);continue;}
    const int wanted=requested_.load();int next=-1;
    if(wanted>=0 && wanted<kCount) {
      for(int offset=0;offset<8;++offset) {
        const int id=(wanted/8)*8+(wanted%8+offset)%8;
        if(!failed_[id].load() && !cached(id)){next=id;break;}
      }
    }
    if(next>=0) {
      const int slot=claimSlot(next);
      if(slot>=0) {
        const uint32_t at=micros();decode_.fillScreen(0x0883);
        bool ok=decode_.drawJpg(encoded_+offsets_[next],muse_assets::files[next/8][next%8].bytes,0,0);
        if(ok)decode_.pushSprite(&slots_[slot].pixels,0,0);
        const auto elapsed=uint32_t(micros()-at);
        if(elapsed>maxDecodeUs_.load())maxDecodeUs_.store(elapsed);
        if(ok){++loads_;slots_[slot].id.store(next);}
        else{failed_[next].store(true);++failures_;slots_[slot].id.store(-1);}
      }
    }
    vTaskDelay(1);
  }
}
void MuseRenderer::draw(muse::Clip clip,uint8_t frame,Overlay overlay) {
  if(!ready_ || static_cast<uint8_t>(clip)>=muse::kStates || frame>=muse::kFrames)return;
  const uint32_t drawAt=micros();const int id=static_cast<int>(clip)*8+frame;
  requested_.store(id);
  if(lastId_!=id) {
    bool found=false;
    for(auto& slot:slots_) {
      int expected=id;
      if(slot.id.compare_exchange_strong(expected,kReading+id)) {
        slot.pixels.pushSprite(&lastTile_,0,0);slot.id.store(id);
        lastId_=id;dirty_=true;found=true;break;
      }
    }
    if(!found)++misses_; // No lock, wait, filesystem access or decoder in this path.
  }
  if(seenFailures_!=failures_.load()){seenFailures_=failures_.load();dirty_=true;}
  if(dirty_) {
    canvas_.fillScreen(M5.Display.color565(7,16,28));
    if(lastId_>=0)lastTile_.pushSprite(&canvas_,76,50);
    else {
      canvas_.setTextDatum(middle_center);canvas_.setTextColor(TFT_WHITE);
      canvas_.drawString(failures_.load()?"Lumi asset error":"Lumi is waking up",160,128);
    }
    if(overlay)overlay(canvas_);
    if(failures_.load()) {
      canvas_.setTextDatum(middle_right);canvas_.setTextColor(0xfd20);
      canvas_.setFont(&fonts::Font0);canvas_.drawString("ASSET!",317,210);
    }
    if(safetyTick_)safetyTick_(); // Between RAM composition and synchronous LCD transfer.
    const uint32_t at=micros();canvas_.pushSprite(0,0);
    maxPresentUs_=std::max(maxPresentUs_,uint32_t(micros()-at));++presented_;dirty_=false;
  }
  maxDrawUs_=std::max(maxDrawUs_,uint32_t(micros()-drawAt));
}
