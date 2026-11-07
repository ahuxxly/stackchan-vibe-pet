#pragma once
#include "FreeRTOS.h"
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,unsigned,void* arg,int,TaskHandle_t* task,int){
 if(failWorker)return 0;
 *task=reinterpret_cast<void*>(1);
 audioTestThread=std::thread([=]{try{fn(arg);}catch(const AudioTestExit&){}});return pdPASS;
}
inline void vTaskDelay(unsigned ms){if(audioTestEnd.load())throw AudioTestExit{};std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
