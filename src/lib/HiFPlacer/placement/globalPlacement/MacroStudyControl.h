#pragma once
#include "MacroLegalizer.h"
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <set>
#include <vector>
#include <array>
#include <stdexcept>
namespace macro_study {
inline int round=0;
inline std::set<int> carryEligible;
inline std::vector<std::array<double,12>> spread;
inline const std::string &mode() {
 static const std::string value=[](){const char *p=std::getenv("AMF_MACRO_STUDY_MODE");return std::string(p?p:"control");}();
 return value;
}
inline int startRound() {
 static const int value=[](){const char *p=std::getenv("AMF_MACRO_STUDY_START");return p?std::stoi(p):29;}();
 return value;
}
inline bool active(const char *name) {return round>=startRound() && mode()==name;}
inline void begin(PlacementInfo *pi, MacroLegalizer *carry, const char *phase, int iteration, float weight) {
 ++round;carryEligible.clear();
 for(const auto &p:carry->diagnosticMatching())carryEligible.insert(p.first->getId());
 spread.assign(pi->getPlacementUnits().size(),{});
 print_info("AMF_STUDY_ROUND round="+std::to_string(round)+" mode="+mode()+" eligible="+std::to_string(carryEligible.size()));
 if(round==startRound()) {
  std::string dir=std::getenv("AMF_MACRO_STUDY_OUTPUT")?std::getenv("AMF_MACRO_STUDY_OUTPUT"):".";
  std::ofstream out(dir+"/pre-intervention-state.tsv");
  if(!out)throw std::runtime_error("Cannot write study pre-intervention state");
  out<<std::setprecision(17)<<"#round="<<round<<" phase="<<phase<<" iteration="<<iteration<<" weight="<<weight<<" progress="<<pi->getProgress()<<'\n';
  const auto &xy=pi->getPULegalXY();const auto &prefs=pi->getRegionPreferences();
  out<<"id\tname\tx\ty\tfixed\tlocked\tlegal_x\tlegal_y\tcarry_eligible\tguide_x\tguide_y\tpull_x\tpull_y\tstrength\n";
  for(auto pu:pi->getPlacementUnits()) {
   out<<pu->getId()<<'\t'<<pu->getName()<<'\t'<<pu->X()<<'\t'<<pu->Y()<<'\t'<<pu->isFixed()<<'\t'<<pu->isLocked()<<'\t';
   auto x=xy.first.find(pu),y=xy.second.find(pu);
   if(x==xy.first.end())out<<"NA";else out<<x->second;out<<'\t';
   if(y==xy.second.end())out<<"NA";else out<<y->second;out<<'\t'<<carryEligible.count(pu->getId());
   auto pr=prefs.find(pu);if(pr==prefs.end())out<<"\t0\t0\t0\t0\t0\n";
   else out<<'\t'<<pr->second.guideX<<'\t'<<pr->second.guideY<<'\t'<<pr->second.pullX<<'\t'<<pr->second.pullY<<'\t'<<pr->second.strength<<'\n';
  }
  if(!out)throw std::runtime_error("Failed writing study pre-intervention state");
 }
}
inline void workerStep(PlacementInfo *pi, PlacementInfo::PlacementUnit *pu, float x0,float y0,float xb,float yb,float xe,float ye,float xc,float yc) {
 if(!pu->checkHasCARRY())return;
 auto &a=spread.at(pu->getId());++a[0];
 a[1]+=std::fabs(xb-x0);a[2]+=std::fabs(yb-y0);
 a[3]+=std::fabs(xe-xb);a[4]+=std::fabs(ye-yb);
 a[5]+=std::fabs(xc-xe);a[6]+=std::fabs(yc-ye);
 const auto &xy=pi->getPULegalXY();auto x=xy.first.find(pu),y=xy.second.find(pu);
 if(x!=xy.first.end() && y!=xy.second.end()) {
  a[7]+=std::fabs(xb-x->second)-std::fabs(x0-x->second);
  a[8]+=std::fabs(yb-y->second)-std::fabs(y0-y->second);
  a[9]+=std::fabs(xc-x->second)-std::fabs(xe-x->second);
  a[10]+=std::fabs(yc-y->second)-std::fabs(ye-y->second);
 }
 a[11]+=active("carry-spread-y-half") && carryEligible.count(pu->getId());
}
inline void summarize() {
 std::array<double,12>a{};size_t n=0;
 for(const auto &b:spread)if(b[0]){++n;for(size_t i=0;i<a.size();++i)a[i]+=b[i];}
 std::ostringstream out;out<<std::setprecision(12)<<"AMF_STUDY_SPREAD round="<<round<<" carry_pus="<<n;
 const char *names[]={"updates","blend_abs_dx","blend_abs_dy","enforce_abs_dx","enforce_abs_dy","clip_abs_dx","clip_abs_dy","blend_target_delta_x","blend_target_delta_y","clip_target_delta_x","clip_target_delta_y","half_updates"};
 for(size_t i=0;i<a.size();++i)out<<' '<<names[i]<<'='<<a[i];print_info(out.str());
}
}
