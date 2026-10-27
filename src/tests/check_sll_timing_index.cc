#include "SllTimingIndex.h"
#include <stdexcept>
#include <iostream>
#include <map>
#include <random>
using amf_sll_plan::TimingIndex;
static void check(bool ok){if(!ok)throw std::runtime_error("SLL timing index mismatch");}
int main(){TimingIndex index;index.reset(1000000,100000);
    for(int i=0;i<100000;++i)index.add(i*7,i/3,i%3,i,100000-i);
    for(int i=0;i<100000;++i){auto p=index.findPin(i*7);check(p&&p->plan==i/3&&p->sink==i%3);check(index.uniqueSink(i,100000-i)==i*7);}
    check(!index.findPin(-1)&&!index.findPin(1000000)&&!index.findPin(999999));
    index.add(900001,0,1,7,99993);check(index.uniqueSink(7,99993)==-1);
    index.add(900002,0,2,7,99993);check(index.uniqueSink(7,99993)==-1);
    index.add(900003,0,3,0x7ffffffe,0x7ffffffd);check(index.uniqueSink(0x7ffffffe,0x7ffffffd)==900003);
    check(index.uniqueSink(0x7ffffffd,0x7ffffffe)==-1&&index.uniqueSink(-1,1)==-1);
    index.reset(10,2);check(!index.findPin(0)&&index.uniqueSink(1,99999)==-1);
    index.add(2,3,4,5,6);check(index.findPin(2)->plan==3&&index.uniqueSink(5,6)==2);
    std::mt19937 rng(2491);
    for(int generation=0;generation<100;++generation){
        const int size=generation%2?5000:9000;index.reset(size,size/2);
        std::map<std::pair<int,int>,int> reference;
        for(int pin=0;pin<size;++pin)if(rng()%3){
            int source=rng()%32,sink=rng()%1024;
            index.add(pin,0,pin,source,sink);
            auto inserted=reference.emplace(std::make_pair(source,sink),pin);
            if(!inserted.second)inserted.first->second=-1;
        }
        for(int source=0;source<32;++source)for(int sink=0;sink<1024;++sink){
            auto it=reference.find({source,sink});
            check(index.uniqueSink(source,sink)==(it==reference.end()?-1:it->second));
        }
    }
    std::cout<<"PASS 100000 timing lookups and 3276800 reference queries across 100 reset generations\n";
}
