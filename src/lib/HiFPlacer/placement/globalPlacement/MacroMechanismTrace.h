
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
namespace macro_mechanism {
static int round=0;
static std::map<int,std::array<double,4>> previous;
static std::string previousStep;
inline void snapshot(PlacementInfo *pi, MacroLegalizer *carry, const char *phase, int iteration, const char *step) {
 AMF_PROFILE_SCOPE("diagnostics", "macro position snapshots");
 static std::map<int,std::array<double,4>> mPrevious;
 std::map<int,std::array<double,4>> mCurrent;
 double txsum=0,tysum=0,mdx=0,mdy=0,deltaX=0,deltaY=0;size_t same=0,changedM=0;
 const auto &legal=pi->getPULegalXY();
 for(const auto &a:legal.first)if(a.first->isMCLB()) {
   auto yi=legal.second.find(a.first);if(yi==legal.second.end())continue;
   auto pu=a.first;double x=pu->X(),y=pu->Y(),tx=a.second,ty=yi->second;
   txsum+=std::fabs(x-tx);tysum+=std::fabs(y-ty);mCurrent[pu->getId()]={{x,y,tx,ty}};
   auto old=mPrevious.find(pu->getId());if(old!=mPrevious.end()){
     mdx+=std::fabs(x-old->second[0]);mdy+=std::fabs(y-old->second[1]);
     if(tx==old->second[2]&&ty==old->second[3]){++same;deltaX+=std::fabs(x-tx)-std::fabs(old->second[0]-tx);
       deltaY+=std::fabs(y-ty)-std::fabs(old->second[1]-ty);}else ++changedM;
   }
 }
 std::ostringstream mline;mline<<std::setprecision(12)<<"AMF_DIAG_MCLB round="<<round<<" phase="<<phase
   <<" iter="<<iteration<<" step="<<step<<" n="<<mCurrent.size()<<" distance_x_sum="<<txsum<<" distance_y_sum="<<tysum
   <<" move_x_sum="<<mdx<<" move_y_sum="<<mdy<<" same_targets="<<same<<" changed_targets="<<changedM
   <<" same_target_delta_x_sum="<<deltaX<<" same_target_delta_y_sum="<<deltaY;
 print_info(mline.str());mPrevious=std::move(mCurrent);
 const auto &matching=carry->diagnosticMatching();
 double sx=0,sy=0,mx=0,my=0,ex=0,ey=0,incx=0,incy=0,decx=0,decy=0;
 size_t changed=0,common=0,over=0,fixed=0,yPreferred=0;
 std::vector<double> ds;
 std::map<int,std::array<double,4>> current;
 for(const auto &entry:matching) {
  auto pu=entry.first; auto site=entry.second;
  double x=pu->X(),y=pu->Y(),tx=site->X(),ty=site->Y();
  double dx=std::fabs(x-tx),dy=std::fabs(y-ty);
  sx+=dx;sy+=dy;ds.push_back(dx+dy);over+=dx+dy>2;fixed+=pu->isFixed();
  int id=pu->getId();current[id]={{x,y,tx,ty}};
  auto pref=pi->getRegionPreferences().find(pu);
  if(pref!=pi->getRegionPreferences().end() && pref->second.guideY)++yPreferred;
  auto old=previous.find(id);
  if(old!=previous.end()) {
   mx+=std::fabs(x-old->second[0]);my+=std::fabs(y-old->second[1]);
   if(tx==old->second[2] && ty==old->second[3]) {
    ++common; double a=dx-std::fabs(old->second[0]-tx),b=dy-std::fabs(old->second[1]-ty);
    ex+=a;ey+=b;incx+=std::max(0.0,a);incy+=std::max(0.0,b);
    decx+=std::max(0.0,-a);decy+=std::max(0.0,-b);
   }else ++changed;
  }
 }
 std::sort(ds.begin(),ds.end());double n=matching.size(),den=n?n:1;
 std::ostringstream out;out<<std::setprecision(10)
  <<"AMF_DIAG_CARRY round="<<round<<" phase="<<phase<<" iter="<<iteration<<" step="<<step
  <<" previous="<<(previousStep.empty()?"none":previousStep)<<" n="<<n<<" dx="<<sx/den<<" dy="<<sy/den
  <<" mean="<<(sx+sy)/den<<" gate="<<carry->getAverageDisplacementOfExactLegalization()
  <<" p95="<<(ds.empty()?0:ds[static_cast<size_t>((ds.size()-1)*.95)])
  <<" above2="<<over<<" fixed="<<fixed<<" y_preferred="<<yPreferred
  <<" move_x="<<mx/den<<" move_y="<<my/den<<" same_targets="<<common<<" changed_targets="<<changed
  <<" delta_x_sum="<<ex<<" delta_y_sum="<<ey<<" increase_x_sum="<<incx<<" increase_y_sum="<<incy
  <<" decrease_x_sum="<<decx<<" decrease_y_sum="<<decy;
 print_info(out.str());previous=std::move(current);previousStep=step;
}
}
