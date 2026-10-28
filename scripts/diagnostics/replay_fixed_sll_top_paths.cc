#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "simpleJSON.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <omp.h>

// Standalone read-only audit: no placement/QP/planner refresh is performed.
using Graph=PlacementTimingInfo::TimingGraph<DesignInfo::DesignCell>;
std::vector<std::string> split(const std::string&s,char sep){std::vector<std::string>v;std::stringstream f(s);std::string x;while(std::getline(f,x,sep))v.push_back(x);return v;}
struct Channel{float ix,iy,ox,oy;double d;std::string tile;};
int main(int argc,char**argv){try{
 if(argc!=5)throw std::runtime_error("config requested.tsv flattened-plan.tsv output-directory");
 omp_set_num_threads(8);
 auto cfg=parseJSONFile(argv[1]);cfg["SllPlanningEnable"]="false";
 std::filesystem::path out=argv[4];
 DeviceInfo dev(cfg,cfg["device"]);DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
 pi.getTimingInfo()->buildSimpleTimingGraph();pi.getTimingInfo()->setDSPInnerDelay();PlacementTimingOptimizer timing(&pi,cfg);
 auto g=pi.getTimingInfo()->getSimplePlacementTimingGraph();
 std::unordered_map<std::string,DeviceInfo::DeviceSite*> inventory;for(auto s:dev.getSites())inventory[s->getName()]=s;
 std::unordered_map<std::string,DeviceInfo::DeviceSite*> cellSites;std::string line;
 std::ifstream pf(argv[2]);while(std::getline(pf,line)){auto v=split(line,'\t');if(v.size()<2)continue;auto site=v[1].substr(0,v[1].find('/'));if(inventory.count(site))cellSites[v[0]]=inventory.at(site);}
 std::unordered_map<std::string,Channel> channels;std::unordered_map<std::string,double> envelope;
 std::ifstream cf(cfg.at("SllChannelFile"));while(std::getline(cf,line)){
  if(line.empty()||line[0]=='#')continue;auto v=split(line,'\t');if(v[0]=="resource")continue;
  Channel c{std::stof(v[4]),std::stof(v[5]),std::stof(v[6]),std::stof(v[7]),std::stod(v[8]),v[1]+"|"+v[2]+"|"+v[4]+"|"+v[5]+"|"+v[6]+"|"+v[7]};
  channels[v[1]+"|"+v[0]]=c;envelope[c.tile]=std::max(envelope[c.tile],c.d);
 }
 std::unordered_map<std::string,std::vector<Channel>> plans;
 std::ifstream pl(argv[3]);std::getline(pl,line);while(std::getline(pl,line)){
  auto v=split(line,'\t');if(v.size()<3)continue;auto &seq=plans[v[0]+"\t"+v[1]];
  for(auto &key:split(v[2],','))seq.push_back(channels.at(key));
 }
 auto &edges=g->getEdges();std::vector<float> old(edges.size()),now(edges.size());std::vector<int> crosses(edges.size());
 std::ofstream ef(out/"edge-models.tsv");ef<<std::setprecision(10)<<"edge\tsource\tsink\tsource_pin\tsink_pin\told_ns\tnew_ns\tcrossings\n";
 size_t absent=0;
 for(auto e:edges){auto sp=e->getSourcePin(),tp=e->getSinkPin();int id=e->getId();
  auto sn=sp->getCell()->getName(),tn=tp->getCell()->getName();
  if(!cellSites.count(sn)||!cellSites.count(tn)){old[id]=now[id]=e->getDelay();++absent;continue;}
  auto a=cellSites.at(sn),b=cellSites.at(tn);
  float sx=a->X()+sp->getOffsetXInCell(),sy=a->Y()+sp->getOffsetYInCell(),tx=b->X()+tp->getOffsetXInCell(),ty=b->Y()+tp->getOffsetYInCell();
  auto it=plans.find(sp->getName()+"\t"+tp->getName());
  if(it!=plans.end()&&!it->second.empty()){
   double d=0;float x=sx,y=sy;
   for(auto &c:it->second){d+=timing.getDelayByModel_conservative(x,y,c.ix,c.iy,true)+envelope.at(c.tile);x=c.ox;y=c.oy;}
   double tail=timing.getDelayByModel_conservative(x,y,tx,ty,true);old[id]=d+tail;now[id]=d+tail+1.1430759607609453*std::max(0.,tail-1.5);crosses[id]=it->second.size();
  }else old[id]=now[id]=timing.getDelayForPins(sp,tp,sx,sy,tx,ty);
  ef<<id<<'\t'<<e->getSource()->getId()<<'\t'<<e->getSink()->getId()<<'\t'<<sp->getName()<<'\t'<<tp->getName()<<'\t'<<old[id]<<'\t'<<now[id]<<'\t'<<crosses[id]<<'\n';
 }
 ef.close();
 std::ofstream nf(out/"nodes.tsv");nf<<std::setprecision(10)<<"id\tcell\tregister\tforward_level\tinner_ns\n";
 for(auto n:g->getNodes())nf<<n->getId()<<'\t'<<n->getDesignNode()->getName()<<'\t'<<n->checkIsRegister()<<'\t'<<n->getForwardLevel()<<'\t'<<n->getInnerDelay()<<'\n';
 for(int variant=0;variant<2;++variant){
  auto &delays=variant?now:old;for(auto e:edges)e->setDelay(delays[e->getId()]);
  g->propogateArrivalTime();g->backPropogateRequiredArrivalTime();g->sortedEndpointByDelay();auto endpoints=g->getSortedTimingEndpoints();
  std::sort(endpoints.begin(),endpoints.end(),[](auto a,auto b){float sa=a->getRequiredArrivalTime()-a->getLatestInputArrival(),sb=b->getRequiredArrivalTime()-b->getLatestInputArrival();return sa==sb?a->getId()<b->getId():sa<sb;});
  std::ofstream f(out/(variant?"predicted-current.tsv":"predicted-old.tsv"));f<<std::setprecision(10)<<"rank\tendpoint_id\tendpoint_cell\tdelay_ns\tslack_ns\tedge_ids\tsource_pin\tsink_pin\tcrossings\n";
  int rank=0;
  for(auto n:endpoints){auto trace=g->backTraceDelayLongestPathFromNode(n->getId());if(trace.size()<2||trace.back()<0)continue;std::reverse(trace.begin(),trace.end());std::vector<Graph::TimingEdge*> chain;bool valid=true;int cross=0;
   for(size_t k=1;k<trace.size();++k){Graph::TimingEdge*best=nullptr;for(auto e:g->getNodes()[trace[k]]->getInEdges())if(e->getSource()->getId()==trace[k-1]&&(!best||e->getDelay()>best->getDelay()))best=e;if(!best){valid=false;break;}chain.push_back(best);cross+=crosses[best->getId()];}
   if(!valid||chain.empty())continue;
   f<<++rank<<'\t'<<n->getId()<<'\t'<<n->getDesignNode()->getName()<<'\t'<<n->getLatestInputArrival()<<'\t'<<n->getRequiredArrivalTime()-n->getLatestInputArrival()<<'\t';
   for(size_t k=0;k<chain.size();++k)f<<(k?",":"")<<chain[k]->getId();
   f<<'\t'<<chain.front()->getSourcePin()->getName()<<'\t'<<chain.back()->getSinkPin()->getName()<<'\t'<<cross<<'\n';
  }
  std::cout<<"REPLAY "<<variant<<" endpoints="<<rank<<"\n";
 }
 std::cout<<"DONE nodes="<<g->getNodes().size()<<" edges="<<edges.size()<<" missing_placement_edges="<<absent<<" plans="<<plans.size()<<std::endl;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
