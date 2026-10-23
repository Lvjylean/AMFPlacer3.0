#include "../lib/HiFPlacer/designInfo/DSPTimingProperties.h"
#include <iostream>
#include <functional>
int main(int argc,char **argv) {
 try {
  std::string header="AMF_DSP_TIMING 1\nsource_dcp_sha256 "+std::string(64,'a')+"\nnetlist_sha256 "+std::string(64,'b')+"\ncell\tPREG\tAREG\tACASCREG\tBREG\tBCASCREG\tMREG\tCREG\tDREG\tADREG\n";
  auto parse=[&](std::string rows,dsp_timing::Outputs outputs) {std::istringstream in(header+rows);return dsp_timing::read(in,outputs);};
  const std::string reg="r 1 0 0 0 0 0 0 0 0\n",comb="c 0 0 0 0 0 0 0 0 0\n";
  auto m=parse(reg+comb,{{"r",{"P[0]","P[47]"}},{"c",{"P[0]","ACOUT[0]"}}});
  dsp_timing::need(m.cells.at("r").preg()==1 && m.cells.at("c").preg()==0,"PREG lost");
  unsigned rejected=0;
  auto reject=[&](std::string rows,dsp_timing::Outputs outputs){try {parse(rows,outputs);}catch(const std::runtime_error&){++rejected;return;}throw std::runtime_error("bad metadata accepted");};
  reject(reg,{{"r",{"P[0]"}},{"c",{"P[0]"}}});
  reject(reg+reg,{{"r",{"P[0]"}}});
  reject(reg,{{"other",{"P[0]"}}});
  reject("r 2 0 0 0 0 0 0 0 0\n",{{"r",{"P[0]"}}});
  reject("r 1 0 0 0 0 0 0 0\n",{{"r",{"P[0]"}}});
  reject("r 1 0 0 0 0 0 0 0 0 junk\n",{{"r",{"P[0]"}}});
  for(auto pin:{"ACOUT[0]","PCOUT[0]","P[48]","P[x]"})reject(reg,{{"r",{"P[0]",pin}}});
  size_t count=0,registered=0;
  if(argc==3) {
   std::ifstream pins(argv[2]);std::string line,name,pin;dsp_timing::Outputs outputs;
   while(std::getline(pins,line)){std::istringstream row(line);row>>name;auto &out=outputs[name];while(row>>pin)out.push_back(pin);}
   std::ifstream input(argv[1]);auto actual=dsp_timing::read(input,outputs);
   count=actual.cells.size();for(auto &entry:actual.cells)registered+=entry.second.preg();
  }
  std::cout<<"PASS property schema, explicit 0/1, complete coverage, mixed-output rejection; negative_cases="<<rejected<<" real_DSPs="<<count<<" real_PREG1="<<registered<<"\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
