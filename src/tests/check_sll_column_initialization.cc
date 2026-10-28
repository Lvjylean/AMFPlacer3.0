#include "../lib/HiFPlacer/placement/placementInfo/SllVirtualModel.h"
#include <iostream>
using namespace amf_sll_plan;
static void check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
static Channel channel(int boundary,int col,int row,int lane,bool down){
    Channel c;c.resource="b"+std::to_string(boundary)+"c"+std::to_string(col)+"r"+std::to_string(row)+"l"+std::to_string(lane);
    c.segment=c.resource+"_in,"+c.resource+","+c.resource+"_out";
    c.from=boundary+(down?1:0);c.to=boundary+(down?0:1);c.column=col;
    c.in={double(col),double(boundary*200+row+(down?100:0))};
    c.out={double(col),double(boundary*200+row+(down?0:100))};
    c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.1;return c;
}
static Model inventory(int rows=2,int lanes=4){
    Model m;m.jointPlacement=m.sourceNearInitialization=true;
    for(int boundary:{0,1})for(int col:{20,80,140})for(int row=0;row<rows;++row)
        for(int lane=0;lane<lanes;++lane)for(bool down:{false,true})
            m.channels.push_back(channel(boundary,col,row,lane,down));
    m.index();return m;
}
static Pin pin(int id,int slr,double x,double y){Pin p;p.id=p.unit=id;p.slr=slr;p.p={x,y};return p;}
static Net net(int id,bool down=false){
    Net n;n.id=id;n.source=pin(id*10,down?1:0,21,down?150:-50);
    n.sinks={pin(id*10+1,down?0:1,80,down?-50:150)};return n;
}
static void occupy(std::map<TileKey,size_t>&used,int boundary,int col,int count,int lanes){
    for(int row=0;count;row++){const int n=std::min(count,lanes);used[{boundary,col,double(boundary*200+row)}]=n;count-=n;}
}
static int select(const Model&m,const std::map<TileKey,size_t>&used,int from=0,int to=1,
                  const std::map<ColumnKey,size_t>*cached=nullptr){
    TileBox a,b;a.add({21,from?150:-50});b.add({80,to?150:-50});
    return m.initialTile(from,to,{21,from?150.:-50.},a,b,1,used,-1,-1,cached);
}
static int column(const Model&m,int ti){check(ti>=0,"expected a tile");return m.tiles.at(ti).column;}
int main(){try{
    auto m=inventory();check(m.initialColumnUtilizationLimit==.75&&!m.allowSplit,"production defaults");
    for(const auto&v:m.columnCapacity)check(v.second==8,"opposite directions counted once");
    std::map<TileKey,size_t> used;
    occupy(used,0,20,5,4);
    check(column(m,select(m,used))==20,"5/8 admits one crossing to reach 75%, even when one tile is full");
    occupy(used,0,20,6,4);
    check(column(m,select(m,used))==80,"6/8 must expand though physical lanes remain");
    check(column(m,select(m,used,1,0))==80,"upward occupancy excludes downward admission to same column");
    check(column(m,select(m,used,1,2))==20,"another SLR boundary has independent capacity");
    occupy(used,0,80,6,4);
    check(column(m,select(m,used))==140,"skip all nearer columns at threshold");
    std::map<ColumnKey,size_t> cached{{{0,20},6},{{0,80},6}};
    check(select(m,used)==select(m,used,0,1,&cached),"cached and independently summed loads agree");
    occupy(used,0,140,6,4);
    check(select(m,used)==-1,"all columns at 75% explicitly fail without silently relaxing headroom");
    m.initialColumnUtilizationLimit=1;
    check(column(m,select(m,used))==20,"explicit 100% policy restores physical-capacity admission");
    m.initialColumnUtilizationLimit=.75;
    TileBox empty;
    int refinement=m.initialTile(0,1,{21,-50},empty,empty,1,used,-1,20);
    check(column(m,refinement)==20,"Y refinement does not reapply column admission");
    auto physical=inventory(60,24);used.clear();occupy(used,0,20,1079,24);
    check(physical.columnCapacity.at({0,20})==1440,"60 tiles times 24 physical lanes");
    check(column(physical,select(physical,used))==20,"1079/1440 admits the 1080th crossing");
    occupy(used,0,20,1080,24);
    check(column(physical,select(physical,used))==80,"1080/1440 expands to farther column");
    std::cout<<"RESULT column_capacity=1440 occupancy=1079 selected=20; occupancy=1080 selected=80\n";
    auto shared=inventory();std::vector<Net> nets;for(int i=0;i<18;i++)nets.push_back(net(i,i%2));
    shared.rebuild(nets);std::map<ColumnKey,size_t> counts;
    for(const auto&p:shared.plans)for(const auto&e:p.crossings)++counts[physicalColumn(shared.tiles.at(e.tile).key)];
    for(int col:{20,80,140})check(counts.at({0,col})==6,"mixed-direction rebuild tracks one column budget");
    shared.legalizeChannels();
    shared.rebuild(nets);check(shared.initialTileEvaluations==0,"refresh retains previously optimized tiles");
    auto before=shared.previous;nets.push_back(net(18));
    bool exhausted=false;try{shared.rebuild(nets);}catch(const std::runtime_error&e){
        exhausted=std::string(e.what()).find("column utilization limit")!=std::string::npos;}
    check(exhausted&&shared.previous==before&&shared.plans.size()==18,"threshold exhaustion preserves committed model");
    auto reserved=inventory();reserved.reserved.insert(reserved.channels.front().resource);reserved.index();
    check(reserved.columnCapacity.at({0,20})==7,"reserved physical lane removed once from usable supply");
    auto uneven=inventory(1,3);used.clear();occupy(used,0,20,2,3);
    check(column(uneven,select(uneven,used))==80,"post-admission must not round above 75%");
    auto invalid=inventory();invalid.initialColumnUtilizationLimit=1.1;
    bool rejected=false;try{select(invalid,{});}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"invalid threshold rejected");
    std::cout<<"PASS 75% column admission, shared up/down supply, independent boundaries, cached counts, rollback, retained tiles and physical-lane legalization\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
