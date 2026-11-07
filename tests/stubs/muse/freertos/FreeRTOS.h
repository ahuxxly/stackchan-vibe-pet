#pragma once
#include <thread>
#include <chrono>
#include <atomic>
using TaskHandle_t=void*;
constexpr int pdPASS=1;
inline bool failTask=false;
