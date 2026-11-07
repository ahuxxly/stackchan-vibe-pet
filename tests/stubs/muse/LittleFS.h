#pragma once
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#include <cstring>
inline std::string missingFile,corruptFile;
inline unsigned readDelayMs=0;
class FileFake {
 public:
  std::vector<uint8_t> bytes;size_t at=0;bool exists=false;
  explicit operator bool()const{return exists;}
  size_t size()const{return bytes.size();}
  size_t available()const{return bytes.size()-at;}
  size_t read(uint8_t* out,size_t n){delay(readDelayMs);n=std::min(n,available());std::memcpy(out,bytes.data()+at,n);at+=n;return n;}
  void seek(size_t n){at=n;}void close(){}
};
struct FilesystemFake {
  unsigned opens=0;
  FileFake open(const char* path,const char*) {
    ++opens;FileFake f;if(missingFile==path)return f;
    std::ifstream file(std::string("data")+path,std::ios::binary);
    f.exists=bool(file);f.bytes={std::istreambuf_iterator<char>(file),{}};
    if(corruptFile==path && !f.bytes.empty())f.bytes.back()^=1;
    return f;
  }
};inline FilesystemFake LittleFS;
