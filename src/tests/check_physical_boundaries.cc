#include "PhysicalBoundaryModel.h"
#include "RegionCapacityTracker.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
void need(bool b,const char *s){if(!b)throw std::runtime_error(s);}
int main(int argc,char **argv)
{
    try {
        if(argc!=2)throw std::runtime_error("Usage: checkPhysicalBoundaries physical_structure.tsv");
        PhysicalBoundaryModel m(argv[1]);
        need(m.getRegions().size()==8,"U250 must have eight logical regions");
        need(m.crossingCount(100,237,100,243,"SLR")==1,"SLR seam");
        need(m.crossingCount(100,239,100,239.5,"SLR")==0,"legacy seam midpoint");
        need(m.crossingCount(100,239,100,239.501,"SLR")==1,"above seam midpoint");
        need(m.crossingCount(100,200,100,800,"SLR")==3,"three seams");
        need(m.crossingCount(100,59,100,60,"SLR")==0,"ordinary CR is not SLR");
        need(m.crossingCount(150,100,170,100,"IO")==1,"IO band");
        need(m.crossingCount(158,100,170,100,"IO")==0,"IO endpoint is not through crossing");
        need(std::fabs(m.penalty(150,237,170,243,1.5f)-2.0f)<1e-6,"additive XY cuts");
        need(std::fabs(m.penalty(170,243,150,237,1.5f)-2.0f)<1e-6,"symmetric cuts");
        need(std::fabs(m.penalty(150,237,170,243,0)-0.5f)<1e-6,"SLR ablation");
        auto p=m.project(0,100,100);
        need(p.first==100 && p.second==100,"no center collapse");
        p=m.project(0,165,243);
        need(p.first<158 && p.second<239.5,"two-dimensional projection");
        float x,y;
        need(m.nearestSlice(0,p.first,p.second,false,0,157.9,0,239.4,x,y),"legal slice target");
        need(m.regionAt(x,y)==0,"nearest target region");
        bool throws=false;try{m.project(0,0,0,200,0.1);}catch(const std::exception &){throws=true;}
        need(throws,"oversize macro is rejected");

        using R=PhysicalBoundaryModel::Resources;using M=PhysicalBoundaryModel;
        R cap{},occupied{};cap.fill(100);cap[M::LUT]=8;cap[M::MLUT]=2;cap[M::BRAM18]=4;cap[M::BRAM36]=2;
        RegionCapacityTracker budget({cap,cap},{occupied,occupied});
        std::vector<R> a(2);a[1][M::LUT]=6;
        need(budget.reserve(1,a,true),"first reservation");
        std::vector<R> b(2);b[1][M::LUT]=3;
        need(!budget.reserve(2,b,true),"competing clusters cannot overbook");
        budget.release(1);need(budget.reserve(2,b,true),"released reservation is reusable");
        need(!budget.reserve(2,b,true),"duplicate reservation");
        std::vector<R> c(2);c[0][M::LUT]=3;c[0][M::MLUT]=3;
        need(!budget.reserve(3,c,true),"SLICEM subset capacity");
        c[0]={};c[0][M::BRAM18]=3;c[0][M::BRAM36]=1;
        need(!budget.reserve(3,c,true),"BRAM overlap");
        c[0]={};c[0][M::LUT]=-1;
        need(!budget.reserve(3,c,true),"negative occupancy rejected");
        std::cout<<"Physical boundary native checks passed: geometry, timing cuts, nearest sites, resource reservations\n";
        return 0;
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}
}

