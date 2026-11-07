#pragma once
#include "FreeRTOS.h"
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,unsigned,void* arg,int,TaskHandle_t*,int) {
 if(failTask)return 0;std::thread([=]{fn(arg);}).detach();return pdPASS;
}
inline void vTaskDelay(unsigned n){std::this_thread::sleep_for(std::chrono::milliseconds(n));}
inline void vTaskDelete(void*){}
