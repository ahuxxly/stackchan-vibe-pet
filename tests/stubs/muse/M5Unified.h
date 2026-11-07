#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
constexpr int TFT_WHITE=65535,middle_center=1,middle_right=2;
namespace fonts {inline int Font0;}
inline uint32_t micros(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline void delay(unsigned n){std::this_thread::sleep_for(std::chrono::milliseconds(n));}
struct SerialFake {template<class... A> void printf(const char*,A...) {}};inline SerialFake Serial;
struct DisplayFake {
 unsigned pushes=0;uint16_t color565(int r,int g,int b){return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
};struct M5Fake {DisplayFake Display;};inline M5Fake M5;
inline std::atomic<bool> failJpeg{false},decoding{false};
inline std::atomic<unsigned> decodeDelayMs{0};
inline bool failAllocation=false;inline unsigned presentDelayMs=0;
inline void* ps_malloc(size_t n){return failAllocation?nullptr:malloc(n);}
class M5Canvas {
 public:
 M5Canvas(DisplayFake* =nullptr){}
 void setPsram(bool){}void setColorDepth(int){}
 void* createSprite(int,int){return failAllocation?nullptr:this;}
 void fillScreen(int){}void setTextDatum(int){}void setTextColor(int){}
 void setFont(int*){}void drawString(const char*,int,int){}
 bool drawJpg(const uint8_t* bytes,size_t size,int,int){decoding=true;delay(decodeDelayMs);decoding=false;return size>2 && bytes[0]==255 && bytes[1]==216 && !failJpeg;}
 void pushSprite(M5Canvas*,int,int){}
 void pushSprite(int,int){delay(presentDelayMs);++M5.Display.pushes;}
};
