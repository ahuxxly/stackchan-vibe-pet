#pragma once
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdlib>
#include <cstdio>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
// This is a wall-clock RTOS simulation. macOS background timer coalescing
// can turn each 20ms capture into ~40ms and falsely trip firmware deadlines.
// Give both participating threads latency-sensitive scheduling, without
// changing the simulated sample rate, clock, or production timeout policy.
inline void audioTestLatencySensitiveThread(){
#ifdef __APPLE__
 if(pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0)!=0){
  std::fputs("Unable to set audio host-test thread QoS\n",stderr);std::abort();
 }
#endif
}
using TaskHandle_t=void*;
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()
#define pdMS_TO_TICKS(v) (v)
constexpr int pdPASS=1;
inline std::thread audioTestThread;
inline std::atomic<bool> audioTestEnd{false};
inline bool failWorker=false;
struct AudioTestExit{};
inline const auto audioTestEpoch=std::chrono::steady_clock::now();
inline uint32_t millis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-audioTestEpoch).count();}
