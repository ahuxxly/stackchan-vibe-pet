// Test-only byte queue and USB connection/ISR boundary. Driver methods appended
// by boot_runtime_test.py are extracted from the pinned HWCDC.cpp unchanged.
#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
namespace usb_model {
inline uint32_t now=0,queueCalls=0,linkServices=0,hostNext=0;
inline bool plugged=true,hostReading=true,interruptEnabled=true,locked=false;
inline size_t capacity=8192;
inline std::deque<uint8_t> tx,rx;
inline std::string pendingLine;
inline std::vector<std::string> lines;
inline void inputStatus(){for(auto c:std::string("{\"op\":\"status\"}\n"))rx.push_back(c);}
inline void advance(unsigned ms){now+=ms;while(hostNext&&now>=hostNext){inputStatus();hostNext+=1000;}}
}
inline bool connected=true;
constexpr int pdTRUE=1,pdPASS=1,portTICK_PERIOD_MS=1,USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY=1;
inline void* tx_ring_buf=reinterpret_cast<void*>(1);
inline void* tx_lock=reinterpret_cast<void*>(1);
inline uint32_t tx_timeout_ms=2;
inline void modelIsr(){
 using namespace usb_model;
 if(!plugged||!hostReading||!interruptEnabled)return;
 connected=true;
 while(!tx.empty()){
  const char c=tx.front();tx.pop_front();pendingLine+=c;
  if(c=='\n'){lines.push_back(pendingLine);pendingLine.clear();}
 }
 // Model the pinned ISR disabling TX-empty interrupts after it drains the queue.
 interruptEnabled=false;
}
inline int xSemaphoreTake(void*,unsigned){return usb_model::locked?0:pdPASS;}
inline void xSemaphoreGive(void*){}
using UBaseType_t=unsigned;
inline void vRingbufferGetInfo(void*,void*,void*,void*,void*,UBaseType_t* n){*n=usb_model::tx.size();}
inline uint32_t usb_serial_jtag_ll_get_intr_ena_status(){return usb_model::interruptEnabled?USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY:0;}
inline size_t xRingbufferGetCurFreeSize(void*){++usb_model::queueCalls;return usb_model::capacity-usb_model::tx.size();}
inline int xRingbufferSend(void*,void* buffer,size_t n,unsigned wait){
 auto& q=usb_model::tx;
 if(n>usb_model::capacity-q.size()){usb_model::advance(wait);return 0;}
 auto p=static_cast<uint8_t*>(buffer);for(size_t i=0;i<n;++i)q.push_back(p[i]);return pdTRUE;
}
inline void usb_serial_jtag_ll_txfifo_flush(){modelIsr();}
inline void usb_serial_jtag_ll_ena_intr_mask(int){usb_model::interruptEnabled=true;modelIsr();}
inline bool usb_serial_jtag_ll_txfifo_writable(){return usb_model::plugged&&usb_model::hostReading;}
inline void flushTXBuffer(const uint8_t* buffer,size_t n){
 auto& q=usb_model::tx;
 if(n>usb_model::capacity){buffer+=n-usb_model::capacity;n=usb_model::capacity;}
 while(q.size()+n>usb_model::capacity)q.pop_front();
 for(size_t i=0;i<n;++i)q.push_back(buffer[i]);
}
#define log_w(...) (void)0
inline uint32_t micros(){return usb_model::now*1000;}
inline void delay(unsigned ms){usb_model::advance(ms);}
struct HWCDC {
 static bool isPlugged(){return usb_model::plugged;}
 static bool isCDC_Connected();
 static bool isConnected(){++usb_model::linkServices;return isCDC_Connected();}
 int availableForWrite();size_t write(const uint8_t*,size_t);
 void begin(unsigned){assert(tx_timeout_ms==2);}
 void setTxBufferSize(size_t n){usb_model::capacity=n;}
 void setTxTimeoutMs(uint32_t n){tx_timeout_ms=n;}
 int available(){return static_cast<int>(usb_model::rx.size());}
 int read(){assert(!usb_model::rx.empty());int c=usb_model::rx.front();usb_model::rx.pop_front();return c;}
 template<class... Args>void printf(const char*,Args...){}
 void println(const char*){}
};
