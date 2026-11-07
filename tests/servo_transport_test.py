"""Compile real SCSerial/SCS/SCSCL against fake ESP UART, never a device.

Tests UART ownership on failed init, each init stage, and actual position packet
parsing (zero vs no reply, ID, length, checksum). Virtual time is not HW timing.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / '.pio/libdeps/stackchan-vibe-pet/StackChan-BSP/src/drivers/FTServo_Arduino/src'
OUT = ROOT / '.test-bin/servo-transport'
OUT.mkdir(parents=True, exist_ok=True)
headers = {
    'driver/gpio.h': '#pragma once\n',
    'driver/uart.h': r'''
#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t=int;using uart_port_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=259;
constexpr int UART_NUM_1=1,UART_NUM_MAX=3,UART_DATA_8_BITS=8,UART_PARITY_DISABLE=0;
constexpr int UART_STOP_BITS_1=1,UART_HW_FLOWCTRL_DISABLE=0,UART_SCLK_DEFAULT=0,UART_PIN_NO_CHANGE=-1;
struct uart_config_t{int baud_rate,data_bits,parity,stop_bits,flow_ctrl,rx_flow_ctrl_thresh,source_clk;};
esp_err_t uart_driver_install(int,int,int,int,void*,int);
esp_err_t uart_driver_delete(int);
esp_err_t uart_param_config(int,const uart_config_t*);
esp_err_t uart_set_pin(int,int,int,int,int);
esp_err_t uart_get_buffered_data_len(int,size_t*);
int uart_read_bytes(int,void*,int,int);
int uart_write_bytes(int,const char*,size_t);
esp_err_t uart_flush_input(int);
esp_err_t uart_wait_tx_done(int,int);
const char* esp_err_to_name(int);
''',
    'freertos/FreeRTOS.h': '#pragma once\n#define pdMS_TO_TICKS(x) (x)\n',
    'freertos/task.h': '#pragma once\nvoid vTaskDelay(int);\n',
    'esp_timer.h': '#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n',
    'esp_log.h': '#pragma once\n#define ESP_LOGE(tag,...) (void)(tag)\n',
}
for name, content in headers.items():
    target = OUT / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content)
source = r'''
#include "SCSCL.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>
static int installError=0,configError=0,pinError=0,deleteError=0;
static int installs=0,deletes=0,ioCalls=0;
static bool driverOwned=false;
static int64_t clockUs=0;
static std::deque<unsigned char> receive;
static std::vector<unsigned char> reply,transmit;
esp_err_t uart_driver_install(int p,int,int,int,void*,int){assert(p==1);++installs;if(!installError)driverOwned=true;return installError;}
esp_err_t uart_driver_delete(int p){assert(p==1);++deletes;if(!deleteError)driverOwned=false;return deleteError;}
esp_err_t uart_param_config(int p,const uart_config_t* c){assert(p==1&&c->baud_rate==1000000);return configError;}
esp_err_t uart_set_pin(int p,int tx,int rx,int,int){assert(p==1&&tx==6&&rx==7);return pinError;}
esp_err_t uart_get_buffered_data_len(int,size_t* n){++ioCalls;*n=receive.size();return ESP_OK;}
int uart_read_bytes(int,void* out,int n,int){++ioCalls;int count=0;auto* p=(unsigned char*)out;
 while(count<n&&!receive.empty()){p[count++]=receive.front();receive.pop_front();}return count;}
int uart_write_bytes(int,const char* bytes,size_t n){++ioCalls;transmit.insert(transmit.end(),bytes,bytes+n);return int(n);}
esp_err_t uart_flush_input(int){++ioCalls;receive.clear();transmit.clear();return ESP_OK;}
esp_err_t uart_wait_tx_done(int,int){++ioCalls;receive.assign(reply.begin(),reply.end());return ESP_OK;}
const char* esp_err_to_name(int){return "fake_error";}
int64_t esp_timer_get_time(){return clockUs;}
void vTaskDelay(int ms){clockUs+=ms*1000;}
struct Probe:SCSCL {using SCSerial::readSCS;using SCSerial::writeSCS;using SCSerial::rFlushSCS;using SCSerial::wFlushSCS;};
static void noAccess(Probe& p){unsigned char b=0;p.readSCS(&b,1);p.readSCS(&b,1,5);p.writeSCS(b);p.writeSCS(&b,1);p.rFlushSCS();p.wFlushSCS();p.end();}
static void packet(int id,int raw,int status=0){reply={255,255,(unsigned char)id,4,(unsigned char)status,(unsigned char)(raw>>8),(unsigned char)raw};
 unsigned sum=0;for(size_t i=2;i<reply.size();++i)sum+=reply[i];reply.push_back((unsigned char)~sum);}
int main(){
 // Another owner exists. A failed install must never grant this object access.
 installError=ESP_ERR_INVALID_STATE;driverOwned=true;Probe failed;
 assert(!failed.begin(1,1000000,6,7));noAccess(failed);
 if(ioCalls||deletes||!driverOwned||failed.uart_num!=UART_NUM_MAX){
   fprintf(stderr,"FAIL failed UART install retained unowned port: io=%d delete=%d resident=%d port=%d\n",ioCalls,deletes,driverOwned,failed.uart_num);return 1;
 }
#ifdef DIAGNOSTIC_API
 assert(failed.initError()==ESP_ERR_INVALID_STATE&&!strcmp(failed.initStage(),"install"));
#endif
 driverOwned=false;installError=0;
 for(int stage=0;stage<2;++stage){
   configError=stage==0?ESP_FAIL:0;pinError=stage==1?ESP_FAIL:0;Probe p;int before=deletes;
   assert(!p.begin(1,1000000,6,7));assert(deletes==before+1&&!driverOwned&&p.uart_num==UART_NUM_MAX);
#ifdef DIAGNOSTIC_API
   assert(p.initError()==ESP_FAIL&&!strcmp(p.initStage(),stage==0?"configure":"pins"));
#endif
   const int io=ioCalls;noAccess(p);assert(ioCalls==io&&deletes==before+1);
 }
 configError=pinError=0;Probe p;assert(p.begin(1,1000000,6,7));
#ifdef DIAGNOSTIC_API
 assert(p.initError()==0&&!strcmp(p.initStage(),"ready"));
 int before=installs;assert(!p.begin(1,1000000,6,7));assert(installs==before&&p.uart_num==1&&driverOwned);
 assert(!strcmp(p.initStage(),"already_open"));
#endif
 for(int raw:{0,489,570,1000}){packet(1,raw);assert(p.ReadPos(1)==raw&&p.getLastError()==0);}
 packet(2,570,4);assert(p.ReadPos(2)==570&&p.getState()==4&&p.getLastError()==0);
 reply.clear();const auto at=clockUs;assert(p.ReadPos(1)==-1&&p.getLastError()==ERR_NO_REPLY);assert(clockUs-at<50000);
 packet(1,570);reply.resize(6);assert(p.ReadPos(1)==-1&&p.getLastError()==ERR_NO_REPLY);
 packet(2,570);assert(p.ReadPos(1)==-1&&p.getLastError()==ERR_SLAVE_ID);
 packet(1,570);reply[3]=3;assert(p.ReadPos(1)==-1&&p.getLastError()==ERR_BUFF_LEN);
 packet(1,570);reply.back()^=1;assert(p.ReadPos(1)==-1&&p.getLastError()==ERR_CRC_CMP);
 assert(transmit.size()==8&&transmit[4]==INST_READ&&transmit[5]==SCSCL_PRESENT_POSITION_L);
#ifdef DIAGNOSTIC_API
 deleteError=ESP_FAIL;p.end();assert(p.uart_num==1&&driverOwned);
 assert(!strcmp(p.initStage(),"cleanup")&&p.initError()==ESP_FAIL);
 deleteError=0;
#endif
 p.end();assert(!driverOwned&&p.uart_num==UART_NUM_MAX);
 puts("PASS actual UART init/ownership + SCS position packets: no reply, truncated, checksum, ID, length, raw zero, status; fake ESP driver, no device");
}
'''
(OUT / 'test.cpp').write_text(source)
flags = ['-DDIAGNOSTIC_API'] if 'initStage()' in (SRC / 'SCSerial.h').read_text() else []
subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-Wno-sign-compare', '-Wno-unused-parameter', '-Wno-vla-cxx-extension',
                '-fsanitize=address,undefined',
                '-include', 'cstdlib', '-I'+str(OUT), '-I'+str(SRC), *flags,
                str(OUT/'test.cpp'), *(str(SRC/name) for name in ('SCSerial.cpp','SCS.cpp','SCSCL.cpp')),
                '-o', str(OUT/'test')], check=True)
subprocess.run([str(OUT/'test')], check=True, timeout=10)
