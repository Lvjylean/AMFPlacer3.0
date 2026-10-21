#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

// Continuous GP support at the current density-bin resolution. A positive bin
// must contain real compatible sites; this is not a site/BEL legalizer.
class SparseResourceGrid {
public:
    float x0=0,y0=0,w=0,h=0;
    std::vector<std::vector<int>> columns;
    void reset(float x,float y,float width,float height,size_t rows) {
        x0=x;y0=y;w=width;h=height;columns.assign(rows,{});
    }
    void add(int row,int column) { columns.at(row).push_back(column); }
    bool contains(float x,float y) const {
        if(w<=0 || h<=0 || x<x0 || y<y0) return false;
        int r=int(std::floor((y-y0)/h)),c=int(std::floor((x-x0)/w));
        return r>=0 && r<int(columns.size()) &&
            std::binary_search(columns[r].begin(),columns[r].end(),c);
    }
    void project(float &x,float &y,float left=0,float right=0,float bottom=0,float top=0) const {
        if(!std::isfinite(x)||!std::isfinite(y))throw std::runtime_error("Non-finite sparse GP coordinate");
        if(left==0 && right==0 && bottom==0 && top==0 && contains(x,y))return;
        float best=std::numeric_limits<float>::infinity(),bx=x,by=y;
        // Search rows and only the two closest occupied columns in each row.
        // Read-only after construction; safe for parallel spreading updates.
        for(size_t r=0;r<columns.size();++r) {
            if(columns[r].empty())continue;
            float loY=y0+r*h-bottom,hiY=y0+(r+1)*h-top;
            hiY=std::nextafter(hiY,-std::numeric_limits<float>::infinity());
            if(loY>hiY)continue;
            float py=std::max(loY,std::min(y,hiY)),dy=py-y;
            if(dy*dy>best)continue;
            int c=int(std::floor((x-x0)/w));
            auto it=std::lower_bound(columns[r].begin(),columns[r].end(),c);
            int indices[2]={it==columns[r].end()?-1:*it,it==columns[r].begin()?-1:*(it-1)};
            for(int col:indices) {
                if(col<0)continue;
                float loX=x0+col*w-left,hiX=x0+(col+1)*w-right;
                hiX=std::nextafter(hiX,-std::numeric_limits<float>::infinity());
                if(loX>hiX)continue;
                float px=std::max(loX,std::min(x,hiX)),dx=px-x,d=dx*dx+dy*dy;
                if(d<best){best=d;bx=px;by=py;}
            }
        }
        if(!std::isfinite(best))throw std::runtime_error("Sparse GP has no resource bin fitting this PU");
        x=bx;y=by;
    }
};
