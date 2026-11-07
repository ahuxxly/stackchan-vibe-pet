#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace fonts {inline int Font0=0,Font2=2;}
constexpr int top_left=0,middle_center=1,bottom_center=2,middle_right=3;
constexpr uint16_t TFT_WHITE=0xffff,TFT_BLACK=0,TFT_RED=0xf800;
namespace display_model {
struct Op {std::string kind,text;int x=0,y=0,w=0,h=0,font=0,datum=0;uint16_t color=0;};
struct Frame {int x,y,w,h;std::vector<Op> ops;};
inline std::vector<Frame> frames;
inline bool failBootAllocation=false;
inline void(*onPush)(int,int)=nullptr;
inline void(*onText)(const char*)=nullptr;
}
struct M5Canvas {
 template<class T>explicit M5Canvas(T*){}
 int w=0,h=0,font=0,datum=0;uint16_t color=0;bool allocated=false;
 std::vector<display_model::Op> ops;
 void setPsram(bool){}void setColorDepth(int){}
 void* createSprite(int width,int height){w=width;h=height;allocated=!(height==8&&display_model::failBootAllocation);return allocated?this:nullptr;}
 void deleteSprite(){allocated=false;}
 void setFont(const int* f){font=*f;}void setTextSize(int n){(void)n;}
 void setTextDatum(int d){datum=d;}void setTextColor(uint16_t c,uint16_t=0){color=c;}
 void fillScreen(uint16_t c){ops.clear();fillRect(0,0,w,h,c);}
 void fillRect(int x,int y,int width,int height,uint16_t c){ops.push_back({"rect","",x,y,width,height,0,0,c});}
 void fillRoundRect(int x,int y,int width,int height,int r,uint16_t c){ops.push_back({"round","",x,y,width,height,r,0,c});}
 void drawRoundRect(int x,int y,int width,int height,int r,uint16_t c){ops.push_back({"border","",x,y,width,height,r,0,c});}
 void fillCircle(int x,int y,int r,uint16_t c){ops.push_back({"circle","",x-r,y-r,2*r,2*r,0,0,c});}
 void drawString(const char* s,int x,int y){ops.push_back({"text",s,x,y,0,0,font,datum,color});if(display_model::onText)display_model::onText(s);}
 void pushSprite(int x,int y){display_model::frames.push_back({x,y,w,h,ops});if(display_model::onPush)display_model::onPush(w,h);}
};
