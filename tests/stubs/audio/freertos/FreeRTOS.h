#pragma once
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
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
