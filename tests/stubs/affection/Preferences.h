#pragma once
#include "Arduino.h"
#include <map>
struct Preferences {
 inline static std::map<std::string,int> numbers;
 inline static std::map<std::string,String> strings;
 inline static bool failOpen=false,failAff=false,failSeq=false;
 bool begin(const char*,bool){return !failOpen;}void end(){}
 int16_t getShort(const char* k,int d){return numbers.count(k)?numbers[k]:d;}
 uint32_t getUInt(const char* k,unsigned d){return numbers.count(k)?numbers[k]:d;}
 String getString(const char* k,const char* d){return strings.count(k)?strings[k]:d;}
 size_t putShort(const char* k,int16_t v){if(failAff)return 0;numbers[k]=v;return 2;}
 size_t putUInt(const char* k,uint32_t v){if(failSeq)return 0;numbers[k]=v;return 4;}
 size_t putString(const char* k,const String& v){strings[k]=v;return v.size();}
};
