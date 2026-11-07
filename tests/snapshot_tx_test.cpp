#include "SnapshotTx.h"
#include <cassert>
#include <string>
#include <cstdio>
struct Stream {
 int available=8192;size_t calls=0,services=0;bool shortWrite=false;std::string output;
 bool isConnected(){++services;return true;}
 int availableForWrite(){return available;}
 size_t write(const uint8_t* bytes,size_t n){++calls;output.assign(reinterpret_cast<const char*>(bytes),n);return shortWrite?n-1:n;}
};
int main(){
 snapshot_tx::Writer writer;Stream stream;JsonDocument d;d["kind"]="status";d["text"]=std::string(4000,'x');
 assert(writer.send(d,stream));assert(stream.calls==1 && stream.output.back()=='\n');
 JsonDocument parsed;assert(!deserializeJson(parsed,stream.output));assert(parsed["text"].as<std::string>().size()==4000);
 for(int available:{0,1,4000}){stream.available=available;assert(!writer.send(d,stream));}
 assert(stream.calls==1 && writer.backpressure()==3);
 assert(stream.services==4); // Connection progress is independent of enqueue room.
 stream.available=8192;stream.shortWrite=true;assert(!writer.send(d,stream));assert(writer.partial()==1);
 stream.shortWrite=false;assert(writer.send(d,stream));assert(writer.sent()==2);
 d["text"]=std::string(9000,'x');assert(!writer.send(d,stream) && writer.oversize()==1);
 puts("PASS production telemetry: one JSON line/one bulk write, no write under backpressure, oversized and partial-write accounting, recovery (fake USB)");
}
