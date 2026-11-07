// HID identity/report map and wire protocol adapted from Corvelis/VibeStackChan (MIT).
// Rendering, input ownership and status expiry are intentionally separate here.
#include "CodexHid.h"
#include "HidProtocol.h"
#include "HidReassembly.h"
#include "StatusProtocol.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>

namespace codex_hid {
namespace {
NimBLEServer* server=nullptr;
NimBLEHIDDevice* hid=nullptr;
NimBLECharacteristic* input=nullptr;
QueueHandle_t queue=nullptr;
companion::State current;
std::atomic<bool> subscribed{false}, authenticated{false};
// User intent to reconnect is separate from the controller's current advertising state.
std::atomic<bool> pairing{false}, pairingFailed{false};
std::atomic<uint32_t> epoch{0}, invalid{0};
std::atomic<uint16_t> handle{BLE_HS_CONN_HANDLE_NONE};
uint32_t appliedEpoch=0, rxCount=0, statusCount=0, txCount=0, lastBattery=0;
bool ready=false, micHeld=false;
companion::HidReassembly assembly;
struct Message { uint32_t epoch; uint32_t receivedAt; char* body; };

bool sendFrame(const uint8_t* bytes) {
  if (!subscribed.load() || !authenticated.load() || !input) return false;
  input->setValue(bytes,63); return input->notify();
}
bool key(const char* name,bool pressed) {
  uint8_t bytes[63];
  if (!companion::keyFrame(name,pressed,bytes)) return false;
  return sendFrame(bytes);
}
bool tap(const char* name) {
  if (!key(name,true)) return false;
  delay(20);
  const bool released=key(name,false);
  if (released) ++txCount;
  // A failed release must terminate the input session, never leave a latched key.
  if (!released && server && handle.load()!=BLE_HS_CONN_HANDLE_NONE)
    server->disconnect(handle.load());
  return released;
}
void sendJson(const String& text) {
  const String payload=text+"\r\n";
  for (size_t offset=0;offset<payload.length();) {
    uint8_t bytes[63]={2,0};
    const size_t n=std::min<size_t>(61,payload.length()-offset);
    bytes[1]=n; memcpy(bytes+2,payload.c_str()+offset,n);
    if (!sendFrame(bytes)) return;
    offset+=n; if (offset<payload.length()) delay(8);
  }
}
void response(const char* method,int id) {
  JsonDocument doc;
  doc["id"]=id; doc["method"]=method;
  if (!strcmp(method,"device.status")) {
    auto value=doc["result"].to<JsonObject>();
    value["version"]=hid_protocol::kFirmwareVersion;
    value["profile_index"]=0; value["layer_index"]=1;
    value["battery"]=M5.Power.getBatteryLevel();
    value["is_charging"]=M5.Power.isCharging()==m5::Power_Class::is_charging;
  } else if (!strcmp(method,"sys.version")) doc["result"]["version"]=hid_protocol::kFirmwareVersion;
  else doc["result"]["ok"]=1;
  String json; serializeJson(doc,json); sendJson(json);
}
void process(const char* body,uint32_t now) {
  JsonDocument doc;
  if (deserializeJson(doc,body) || !doc.is<JsonObject>()) {++invalid; return;}
  const char* method=doc["method"] | doc["m"] | "";
  JsonVariantConst params=doc["params"];
  if (params.isNull()) params=doc["p"];
  if (!strcmp(method,"v.oai.thstatus")) {
    invalid.fetch_add(companion::applyThreadStatus(current,params,now));
    ++statusCount;
  }
  companion::applyHostPoll(current,doc.as<JsonVariantConst>(),now);
  // Ignore focused app text and unrelated data; do not log incoming JSON/chat content.
  ++rxCount;
  const int id=doc["id"] | doc["i"] | -1;
  if (id>=0 && method[0]) response(method,id);
}
class OutputCallbacks final:public NimBLECharacteristicCallbacks {
 void onWrite(NimBLECharacteristic* characteristic,NimBLEConnInfo& info) override {
  if (!info.isEncrypted() || !authenticated.load() || info.getConnHandle()!=handle.load()) return;
  const auto value=characteristic->getValue();
  const uint8_t* data=value.data(); const size_t size=value.size();
  if(!assembly.append(data,size,millis(),epoch.load())) {++invalid;return;}
  JsonDocument probe; const auto error=deserializeJson(probe,assembly.data());
  if (error==DeserializationError::IncompleteInput) return;
  if (error) {assembly.clear();++invalid;return;}
  Message message{epoch.load(),millis(),static_cast<char*>(malloc(assembly.size()+1))};
  if(!message.body){assembly.clear();++invalid;return;}
  memcpy(message.body,assembly.data(),assembly.size()+1);
  if(xQueueSend(queue,&message,0)!=pdTRUE){free(message.body);++invalid;}
  assembly.clear();
 }
};
class ServerCallbacks final:public NimBLEServerCallbacks {
 void onConnect(NimBLEServer* s,NimBLEConnInfo& info) override {
  // One host owns the HID session; never mix hosts' status/commands.
  if (handle.load()!=BLE_HS_CONN_HANDLE_NONE) {s->disconnect(info.getConnHandle());return;}
  handle=info.getConnHandle(); authenticated=false; subscribed=false; pairingFailed=false; ++epoch;
  assembly.clear();
  s->updateConnParams(info.getConnHandle(),12,24,0,180);
 }
 void onDisconnect(NimBLEServer*,NimBLEConnInfo& info,int) override {
  if (handle.load()!=info.getConnHandle()) return;
  subscribed=false; authenticated=false; handle=BLE_HS_CONN_HANDLE_NONE; ++epoch; assembly.clear();
  // Re-advertise only after the user has physically enabled pairing once this boot.
  if (pairing.load()) pairingFailed=!NimBLEDevice::startAdvertising();
 }
 void onAuthenticationComplete(NimBLEConnInfo& info) override {
  if (!info.isEncrypted()) {server->disconnect(info.getConnHandle());return;}
  if (handle.load()==info.getConnHandle()) authenticated=true;
 }
};
class InputCallbacks final:public NimBLECharacteristicCallbacks {
 void onSubscribe(NimBLECharacteristic*,NimBLEConnInfo& info,uint16_t value) override {
  if (info.getConnHandle()==handle.load()) {
    subscribed=value!=0;
    if (info.isEncrypted()) authenticated=true;
    if (!value) ++epoch;
  }
 }
};
OutputCallbacks outputCallbacks;
ServerCallbacks serverCallbacks;
InputCallbacks inputCallbacks;

bool configureIdentity() {
  // Upstream's separate stable HID identity avoids reusing DANCE's cached GATT identity.
  const uint64_t chipMac=ESP.getEfuseMac();
  char text[18];
  snprintf(text,sizeof(text),"C2:%02X:%02X:%02X:%02X:%02X",
    unsigned((chipMac>>8)&255),unsigned((chipMac>>16)&255),unsigned((chipMac>>24)&255),
    unsigned((chipMac>>32)&255),unsigned((chipMac>>40)&255));
  const NimBLEAddress address(text,BLE_ADDR_RANDOM);
  return address.isStatic() && NimBLEDevice::setOwnAddr(address) &&
         NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
}
}

bool begin() {
  if (ready) return true;
  queue=xQueueCreate(4,sizeof(Message));
  if (!queue || !NimBLEDevice::init("VibeStackChan Pet")) return false;
  if (!configureIdentity()) {NimBLEDevice::deinit(true);return false;}
  // Preserve upstream vendor-first HID collection for macOS Codex discovery.
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(true,false,true);
  server=NimBLEDevice::createServer(); server->setCallbacks(&serverCallbacks,false);
  hid=new NimBLEHIDDevice(server);
  hid->setManufacturer(hid_protocol::kManufacturer);
  hid->setPnp(0x01,hid_protocol::kVendorId,hid_protocol::kProductId,hid_protocol::kProductVersion);
  hid->setHidInfo(0,1); hid->setReportMap(hid_protocol::kReportMap,sizeof(hid_protocol::kReportMap));
  const uint8_t idle[8]={};
  hid->getInputReport(1)->setValue(idle,8);
  hid->getInputReport(2)->setValue(idle,2);
  // Pointer descriptor is five bytes (buttons, X, Y, wheel, horizontal wheel).
  hid->getInputReport(3)->setValue(idle,5);
  input=hid->getInputReport(6); input->setCallbacks(&inputCallbacks);
  const uint8_t vendorIdle[hid_protocol::kBleReportLength]={};
  input->setValue(vendorIdle,sizeof(vendorIdle));
  auto* output=hid->getOutputReport(6); output->setCallbacks(&outputCallbacks);
  hid->getFeatureReport(6);
  auto* info=hid->getDeviceInfoService();
  info->createCharacteristic(uint16_t(0x2a24),NIMBLE_PROPERTY::READ)->setValue(hid_protocol::kModelNumber);
  info->createCharacteristic(uint16_t(0x2a26),NIMBLE_PROPERTY::READ)->setValue(hid_protocol::kFirmwareVersion);
  if (!server->start()) return false;
  auto* adv=NimBLEDevice::getAdvertising();
  adv->setName("VibeStackChan Pet"); adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(hid->getHidService()->getUUID()); adv->enableScanResponse(true);
  ready=true; Serial.println("[hid] ready; physical PAIR button required; no advertising at boot");
  return true;
}
bool advertise() {
  if (!ready) {pairingFailed=true;return false;}
  // Do not start accepting another host while this one is negotiating HID.
  if (handle.load()!=BLE_HS_CONN_HANDLE_NONE) return true;
  const bool started=NimBLEDevice::getAdvertising()->start();
  pairingFailed=!started;
  if (started) pairing=true;
  return started;
}
bool advertising() {return ready && NimBLEDevice::getAdvertising()->isAdvertising();}
ConnectionInfo connectionInfo() {
  return {ready,advertising(),handle.load()!=BLE_HS_CONN_HANDLE_NONE,
          authenticated.load(),subscribed.load(),pairing.load(),pairingFailed.load(),epoch.load()};
}
void update(uint32_t now) {
  const uint32_t latestEpoch=epoch.load();
  if (latestEpoch!=appliedEpoch) {current.connect(false);appliedEpoch=latestEpoch;micHeld=false;}
  current.connect(subscribed.load() && authenticated.load());
  Message message;
  while(queue && xQueueReceive(queue,&message,0)==pdTRUE) {
    if(current.connected && message.epoch==appliedEpoch)process(message.body,message.receivedAt);
    free(message.body);
  }
  if (hid && uint32_t(now-lastBattery)>=30000) {
    hid->setBatteryLevel(constrain(M5.Power.getBatteryLevel(),0,100),current.connected);lastBattery=now;
  }
}
const companion::State& state() {return current;}
bool tapAgent(int i) {if(i<0||i>5)return false;char name[5];snprintf(name,sizeof(name),"AG%02d",i);return tap(name);}
bool tapAction(int i) {if(i<6||i>12)return false;char name[6];snprintf(name,sizeof(name),"ACT%02d",i);return tap(name);}
bool microphone(bool pressed) {
  const bool a=key("ACT10",pressed);delay(12);const bool b=key("ACT11",pressed);
  if(a&&b){micHeld=pressed;return true;}
  key("ACT10",false);delay(12);key("ACT11",false);micHeld=false;
  if(server && handle.load()!=BLE_HS_CONN_HANDLE_NONE) server->disconnect(handle.load());
  return false;
}
void releaseInputs() {
  if(micHeld){
    const bool a=key("ACT10",false);delay(12);const bool b=key("ACT11",false);micHeld=false;
    if((!a || !b) && server && handle.load()!=BLE_HS_CONN_HANDLE_NONE) server->disconnect(handle.load());
  }
}
uint32_t receivedMessages(){return rxCount;}
uint32_t receivedStatusMessages(){return statusCount;}
uint32_t sentActions(){return txCount;}
uint32_t invalidMessages(){return invalid.load();}
}
