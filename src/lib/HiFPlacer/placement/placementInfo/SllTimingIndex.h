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
    std::vector<uint64_t> pairKeys;
    std::vector<int> pairSinks, pinPair;
    enum { absent=-2 };
    static uint64_t key(int a,int b){return (uint64_t(uint32_t(a))<<32)|uint32_t(b);}
public:
    void reset(size_t pinCount,size_t sinkCount){
        pins.assign(pinCount,{});pinPair.resize(pinCount,-1);
        // Keys describe immutable connectivity. Only the active answers are
        // reset; inactive/ambiguous pairs must never reuse an earlier answer.
        pairSinks.assign(pairKeys.size(),absent);
        if(sinkCount>cellPairs.bucket_count()*cellPairs.max_load_factor())cellPairs.reserve(sinkCount);
    }
    void add(int pin,int plan,int sink,int sourceCell,int sinkCell){
        pins.at(pin)={plan,sink};
        const auto identity=key(sourceCell,sinkCell);int slot=pinPair.at(pin);
        if(slot<0||pairKeys.at(slot)!=identity){
            auto inserted=cellPairs.emplace(identity,int(pairKeys.size()));slot=inserted.first->second;
            if(inserted.second){pairKeys.push_back(identity);pairSinks.push_back(absent);}
            pinPair[pin]=slot;
        }
        int&answer=pairSinks[slot];answer=answer==absent?pin:-1; // Multiple pins remain ambiguous.
    }
    const Location* findPin(int pin)const{
        if(pin<0||size_t(pin)>=pins.size()||pins[pin].plan<0)return nullptr;
        return &pins[pin];
    }
    int uniqueSink(int sourceCell,int sinkCell)const{
        if(sourceCell<0||sinkCell<0)return -1;
        auto it=cellPairs.find(key(sourceCell,sinkCell));
        return it==cellPairs.end()||pairSinks[it->second]<0?-1:pairSinks[it->second];
    }
};
}
