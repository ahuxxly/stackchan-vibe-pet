#pragma once
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
using String=std::string;
using std::min;using std::max;
template<class A,class B,class C> A constrain(A v,B a,C b){return std::max<A>(a,std::min<A>(b,v));}
inline uint32_t tickMs=0;
inline uint32_t millis(){return tickMs;}
struct SerialFake {template<class... A> void printf(const char*,A...){} void println(const char*){}};
inline SerialFake Serial;
