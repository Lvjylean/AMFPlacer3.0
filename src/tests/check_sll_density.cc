#include "../lib/HiFPlacer/placement/placementInfo/SllDensityModel.h"
#include <iostream>
using namespace amf_sll_plan;
static void check(bool x,const char*s){if(!x)throw std::runtime_error(s);}
static Channel channel(int col,int lane,bool down=false){
    Channel c;c.resource="column"+std::to_string(col)+"lane"+std::to_string(lane);
    c.segment=c.resource+"_imux,"+c.resource+"_ubump,"+c.resource+"_egress";
    c.from=down?1:0;c.to=down?0:1;c.column=100+col*13;
    c.in={double(col*10),down?110.:90.};c.out={double(col*10)-1,down?90.:110.};
    c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.1;return c;
}
static Pin pin(int id,int slr,double x){Pin p;p.id=id;p.unit=id;p.slr=slr;p.p={x,slr?150.:50.};p.movable=true;return p;}
int main(){try{
    Model m;m.jointPlacement=true;m.sourceNearInitialization=true;m.allowSplit=false;
    for(int col=0;col<16;++col)for(int lane=0;lane<2;++lane)for(bool down:{false,true})m.channels.push_back(channel(col,lane,down));
    m.index();Net a{1,"a",pin(0,0,0),{pin(1,1,150)},1},b{2,"b",pin(2,0,0),{pin(3,1,150)},1};
    m.rebuild({a,b});for(auto&p:m.plans)check(m.channels[p.crossings[0].channel].column==100,"newborn chooses source-near column despite far sink");
    DensityModel d;d.targetUtilization=.5;d.weight=1000;d.index(m);
    check(d.capacity.size()==16,"sixteen physical bins");for(auto&v:d.capacity)check(v.second==2,"opposite directions do not double capacity");
    auto near=m.candidates(0,1,{},[](const Channel&c){return c.in.x*c.in.x;});
    std::set<int> covered;for(int ci:near)covered.insert(m.channels[ci].column);check(covered.size()==16,"all sixteen columns have candidates");
    std::vector<Point> desired(2,{0,90});auto spread=d.assign(m,desired);
    check(spread.energyAfter<spread.energyBefore,"density pressure decreases overflow energy");
    check(m.channels[spread.channels[0]].column!=m.channels[spread.channels[1]].column,"density can move a virtual cell to another column");
    std::set<std::string> used;size_t k=0;
    for(auto&p:m.plans)for(auto&e:p.crossings){e.channel=spread.channels[k++];m.previous[e.key]=e.channel;check(!Model::conflicts(m.channels[e.channel],used),"no resource overlap");Model::occupy(m.channels[e.channel],used);}
    auto optimized=d.current(m);m.rebuild({a,b});check(d.current(m)==optimized,"refresh preserves optimized virtual cells instead of resetting to source");
    std::vector<Point> arbitrary{{34.25,103.7},{119.8,92.6}};auto projected=d.assign(m,arbitrary);
    for(int ci:projected.channels){check(m.channels[ci].from==0&&m.channels[ci].to==1,"direction preserved");check(covered.count(m.channels[ci].column),"legal column after projection");check(m.channels[ci].out.x==m.channels[ci].in.x-1,"paired endpoints");}
    a.sinks[0].slr=0;b.sinks[0].slr=0;m.rebuild({a,b});check(m.plans.empty()&&m.deleted==2,"all joint variables released when cross-die nets vanish");
    d.weight=0;m.previous.clear();a.sinks[0].slr=b.sinks[0].slr=1;m.rebuild({a,b});auto control=d.assign(m,desired);check(control.moved==0,"zero density weight control stays source-near");
    std::cout<<"PASS source initialization, 16-column coverage, shared capacity, density movement, projection and lifecycle\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
