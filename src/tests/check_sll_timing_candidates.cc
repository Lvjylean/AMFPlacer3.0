#include "SllTimingCandidates.h"
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

using namespace amf_sll_plan;

static size_t checks=0;
static void compare(const std::vector<Channel>&channels,const std::vector<int>&ids,
                    TimingCandidates&index,Point source,double target,double ratio,size_t k){
    std::vector<std::pair<double,int>> expected;
    for(int id:ids){const auto&c=channels.at(id);
        expected.emplace_back(length(source,c.in,ratio)+std::abs(c.out.x-target),id);}
    auto count=std::min(k,expected.size());
    std::partial_sort(expected.begin(),expected.begin()+count,expected.end());expected.resize(count);
    const auto actual=index.nearest(source,target,ratio,k);
    if(actual!=expected)throw std::runtime_error("Indexed shortlist differs from full scan");
    ++checks;
}

int main(int argc,char**argv){
    if(argc!=2)throw std::runtime_error("Usage: checkSllTimingCandidates channels.tsv");
    std::ifstream input(argv[1]);if(!input)throw std::runtime_error("Missing channel table");
    std::vector<Channel> channels;std::string line;
    while(std::getline(input,line)){
        if(line.empty()||line[0]=='#'||line.rfind("resource\t",0)==0)continue;
        std::vector<std::string> f;std::istringstream fields(line);std::string v;
        while(std::getline(fields,v,'\t'))f.push_back(v);
        if(f.size()!=13)throw std::runtime_error("Unexpected table schema");
        Channel c;c.resource=f[0];c.from=std::stoi(f[1]);c.to=std::stoi(f[2]);c.column=std::stoi(f[3]);
        c.in={std::stod(f[4]),std::stod(f[5])};c.out={std::stod(f[6]),std::stod(f[7])};
        c.slowMax=std::stod(f[8]);channels.push_back(c);
    }
    if(channels.size()!=138240)throw std::runtime_error("Unexpected U250 inventory size");
    std::map<std::pair<int,int>,std::map<std::tuple<double,double,double,double>,int>> groups;
    for(size_t i=0;i<channels.size();++i){const auto&c=channels[i];auto&g=groups[{c.from,c.to}];
        auto key=std::make_tuple(c.in.x,c.in.y,c.out.x,c.out.y);auto old=g.find(key);
        if(old==g.end()||channels[old->second].slowMax<c.slowMax)g[key]=int(i);
    }
    std::mt19937 random(780527);std::uniform_real_distribution<double> x(-100,250),y(-100,1100);
    const double ratios[]={.4,0,1,1e-16,1000};
    const size_t counts[]={0,1,16,64,2000};
    size_t representatives=0;
    for(const auto&direction:groups){
        std::vector<int> ids;for(const auto&entry:direction.second)ids.push_back(entry.second);
        representatives+=ids.size();TimingCandidates index;index.build(channels,ids);
        for(int i=0;i<3000;++i)compare(channels,ids,index,{x(random),y(random)},x(random),ratios[i%5],counts[i%5]);
        for(int id:ids){const auto&c=channels[id];compare(channels,ids,index,c.in,c.out.x,.4,16);}
        // The index must honor the supplied availability set, including holes.
        std::vector<int> subset;for(size_t i=0;i<ids.size();++i)if(i%3)subset.push_back(ids[i]);
        index.build(channels,subset);
        for(int i=0;i<500;++i)compare(channels,subset,index,{x(random),y(random)},x(random),.4,16);
        index.build(channels,{});compare(channels,{},index,{0,0},0,.4,16);
    }
    // Artificial ties, duplicate coordinates, reversed IDs, and rounding plateaus.
    std::vector<Channel> synthetic;std::vector<int> ids;
    for(int i=0;i<320;++i){Channel c;c.in={double(i%4),double((i/4)%20)};
        c.out={double(i%3),c.in.y+100};synthetic.push_back(c);ids.push_back(i);}
    std::reverse(ids.begin(),ids.end());TimingCandidates index;index.build(synthetic,ids);
    for(int i=0;i<2000;++i)compare(synthetic,ids,index,
        {i%2?1e20:x(random),i%2?10:y(random)},x(random),ratios[i%5],counts[i%5]);
    std::cout<<"PASS exact shortlist comparisons="<<checks<<" directed_channels="<<channels.size()
             <<" representatives="<<representatives<<" directions="<<groups.size()<<"\n";
}
