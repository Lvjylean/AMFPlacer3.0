#pragma once
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <vector>
namespace amf_sll_plan {
// Rebuilt at the serial lifecycle boundary; immutable during timing readers.
class TimingIndex {
public:
    struct Location {int plan=-1,sink=-1;};
private:
    std::vector<Location> pins;
    std::unordered_map<uint64_t,int> cellPairs;
    static uint64_t key(int a,int b){return (uint64_t(uint32_t(a))<<32)|uint32_t(b);}
public:
    void reset(size_t pinCount,size_t sinkCount){
        pins.assign(pinCount,{});cellPairs.clear();cellPairs.reserve(sinkCount);
    }
    void add(int pin,int plan,int sink,int sourceCell,int sinkCell){
        pins.at(pin)={plan,sink};
        auto inserted=cellPairs.emplace(key(sourceCell,sinkCell),pin);
        if(!inserted.second)inserted.first->second=-1; // Multiple pins remain ambiguous.
    }
    const Location* findPin(int pin)const{
        if(pin<0||size_t(pin)>=pins.size()||pins[pin].plan<0)return nullptr;
        return &pins[pin];
    }
    int uniqueSink(int sourceCell,int sinkCell)const{
        if(sourceCell<0||sinkCell<0)return -1;
        auto it=cellPairs.find(key(sourceCell,sinkCell));return it==cellPairs.end()?-1:it->second;
    }
};
}
