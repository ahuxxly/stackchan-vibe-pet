#pragma once

// Release builds supply the single version value in platformio.ini.
// Host-only tests may use dev; release packaging rejects it.
#ifndef VIBESTACKCHAN_FIRMWARE_VERSION
#define VIBESTACKCHAN_FIRMWARE_VERSION "dev"
#endif
namespace firmware_version {
// The runtime pointer refers into this record, keeping the actual compiled
// identity inspectable in firmware.bin before any hardware write.
inline constexpr char kRecord[]="STACKCHAN_BUILD=" VIBESTACKCHAN_FIRMWARE_VERSION;
inline constexpr const char* kBuild=kRecord+sizeof("STACKCHAN_BUILD=")-1;
inline constexpr char kHid[]="v" VIBESTACKCHAN_FIRMWARE_VERSION;
template<class Document> void stampTelemetry(Document& doc) {
  doc["version"]=kBuild;
  doc["build"]=kBuild;
}
}
