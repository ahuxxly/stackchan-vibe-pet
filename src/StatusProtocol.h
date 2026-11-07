#pragma once
#include <ArduinoJson.h>
#include "CompanionCore.h"

namespace companion {
// Only the observed host status request counts as liveness. USB telemetry,
// GATT battery notifications, arbitrary RPCs and malformed queries do not.
inline bool applyHostPoll(State& state,JsonVariantConst doc,uint32_t receivedAt) {
  if(!doc.is<JsonObjectConst>())return false;
  const char* method=doc["method"] | doc["m"] | "";
  if(std::strcmp(method,"device.status"))return false;
  if(!doc["result"].isUnbound() || !doc["error"].isUnbound())return false;
  const auto id=doc["id"].isUnbound()?doc["i"]:doc["id"];
  const auto params=doc["params"].isUnbound()?doc["p"]:doc["params"];
  if(!id.is<int>() || id.as<int>()<0 ||
     (!params.isUnbound() && !params.isNull() &&
      (!params.is<JsonObjectConst>() || params.size()!=0)) || !state.connected)return false;
  state.receiveHostPoll(receivedAt);return true;
}
// Actual firmware decoder, also exercised on the host with the pinned ArduinoJson library.
inline unsigned applyThreadStatus(State& state,JsonVariantConst params,uint32_t receivedAt) {
  if(!params.is<JsonArrayConst>())return 1;
  unsigned rejected=0;
  for(JsonObjectConst item:params.as<JsonArrayConst>()) {
    const uint8_t fields=(!item["c"].isUnbound()?1:0)|(!item["b"].isUnbound()?2:0)|
                         (!item["e"].isUnbound()?4:0)|(!item["s"].isUnbound()?8:0);
    if(!item["id"].is<int>() || ((fields&1) && !item["c"].is<uint32_t>()) ||
       ((fields&2) && !item["b"].is<float>()) || ((fields&4) && !item["e"].is<int>()) ||
       ((fields&8) && !item["s"].is<float>())){++rejected;continue;}
    if(!state.receivePatch(item["id"],fields,item["c"]|0U,item["b"]|0.0f,
                          item["e"]|0,item["s"]|0.0f,receivedAt))++rejected;
  }
  return rejected;
}
}
