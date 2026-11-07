#pragma once
#include <cstdlib>
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
inline bool failAudioMemory=false;
inline void* audioTestMemory=nullptr;
inline size_t audioTestBytes=0;
inline void* heap_caps_calloc(size_t n,size_t size,int caps){assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));audioTestBytes=n*size;return audioTestMemory=failAudioMemory?nullptr:std::calloc(n,size);}
inline void heap_caps_free(void* p){std::free(p);if(p==audioTestMemory)audioTestMemory=nullptr;}
