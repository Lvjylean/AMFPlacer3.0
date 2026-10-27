#include "SllVirtualModel.h"
#include <iomanip>
#include <iostream>
#include <random>
using namespace amf_sll_plan;
int main(){
    std::cout<<std::setprecision(17);
    for(int scenario=0;scenario<5;++scenario){
        Model m;m.jointPlacement=true;m.sourceNearInitialization=scenario!=2;
        m.timingWeight=16;m.extraCrossingCost=24;
        m.local=[](Point a,Point b){return .05+.01*length(a,b,.4);};
        const int columns=scenario==4?1:(scenario==3?4:16),rows=scenario==4?1:(scenario==3?2:8),lanes=scenario>=3?1:(scenario==1?2:8);
        for(int seam=0;seam<2;++seam)for(int column=0;column<columns;++column)
            for(int row=0;row<rows;++row)for(int lane=0;lane<lanes;++lane){
                Channel c;c.resource="R"+std::to_string(seam)+"_"+std::to_string(column)+"_"+std::to_string(row)+"_"+std::to_string(lane);
                c.from=seam;c.to=seam+1;c.column=column;c.in={5.+11*column,80.+100*seam+row};c.out={c.in.x,100.+100*seam+row};
                c.slowMax=.789;c.slowMin=.6;c.fastMax=.5;c.fastMin=.4;
                c.segment="A/"+c.resource+",B/"+c.resource+","+c.resource+",C/"+c.resource+",D/"+c.resource;
                m.channels.push_back(c);std::swap(c.from,c.to);std::swap(c.in,c.out);m.channels.push_back(c);
            }
        m.index();if(scenario==1)for(size_t i=0;i<m.channels.size();i+=34)m.reserved.insert(m.channels[i].resource);m.indexTiles();
        for(int epoch=0;epoch<20;++epoch){
            std::mt19937 rng(1000+epoch);std::vector<Net> nets;
            const int count=scenario==4?1:(scenario==3?5:96);
            m.allowSplit=epoch%7!=0;
            for(int i=0;i<count;++i){
                if(epoch%5==2&&i%7==0)continue;
                Net n;n.id=i;n.name="net"+std::to_string(i);n.weight=1+(i%3)*.25;
                n.source.id=i*1000;n.source.name="source";n.source.slr=(i+epoch/4)%3;n.source.p={double(rng()%(columns*11)),100.*n.source.slr+20.};
                if(scenario==4){n.source.slr=0;n.source.p.y=20;}
                n.source.unit=i;n.source.movable=true;
                const int fanouts[]={1,2,4,6,32,128};int fanout=fanouts[(i+epoch)%6];
                if(scenario==4)fanout=2;
                for(int j=0;j<fanout;++j){Pin p;p.id=i*1000+j+1;p.name="sink"+std::to_string(j);p.unit=p.id;
                    p.slr=(j+i+1+epoch/3)%3;if(epoch%6==5&&i%9==0)p.slr=n.source.slr;
                    if(scenario==4)p.slr=1;
                    p.p={double(rng()%(columns*11)),100.*p.slr+30.+(rng()%30)};p.criticality=1+(rng()%8);p.movable=true;n.sinks.push_back(p);}
                nets.push_back(std::move(n));
            }
            try{m.rebuild(std::move(nets));}
            catch(const std::runtime_error&){std::cout<<"CAPACITY "<<scenario<<' '<<epoch<<'\n';continue;}
            std::cout<<"FRAME "<<scenario<<' '<<epoch<<' '<<m.created<<' '<<m.retained<<' '<<m.deleted<<' '<<m.initialTileEvaluations<<'\n';
            for(const auto&p:m.plans){std::cout<<"NET "<<p.net.id<<' '<<p.net.name<<' '<<p.net.weight<<' '<<m.wireCost(p)<<' '<<m.cost(p)<<'\n';
                for(const auto&s:p.net.sinks){std::cout<<"PIN "<<s.id<<' '<<s.slr<<' '<<s.p.x<<' '<<s.p.y<<' '<<s.criticality;
                    for(int k:p.paths.at(s.id))std::cout<<' '<<k;std::cout<<'\n';}
                for(const auto&e:p.crossings){std::cout<<"CROSS "<<e.key<<' '<<e.tile<<' '<<e.parent;for(int id:e.sinks)std::cout<<' '<<id;std::cout<<'\n';}}
            if(epoch%5==3)for(auto&old:m.previous)old.second=(old.second+3)%m.tiles.size();
        }
    }
}
