#include "SparseResourceGrid.h"
#include <cassert>
#include <iostream>
#include <random>
int main() {
    SparseResourceGrid grid;grid.reset(41.5f,11.5f,2,2,130);
    // A large empty gap, one isolated column, a hole in that column, and
    // another distant island. Deliberately nonzero device origin.
    for(int r=0;r<30;++r)grid.add(r,33);
    for(int r=40;r<90;++r)for(int c:{33,41,42})grid.add(r,c);
    for(int r=110;r<120;++r)grid.add(r,100);
    std::mt19937 gen(17);std::uniform_real_distribution<float> pos(-100,400);
    for(int i=0;i<5000;++i) {
        float x=pos(gen),y=pos(gen),px=x,py=y;grid.project(px,py);
        assert(grid.contains(px,py));
        float brute=INFINITY;
        for(size_t r=0;r<grid.columns.size();++r)for(int c:grid.columns[r]) {
            float lx=grid.x0+c*grid.w,hx=std::nextafter(lx+grid.w,-INFINITY);
            float ly=grid.y0+r*grid.h,hy=std::nextafter(ly+grid.h,-INFINITY);
            float dx=std::max(lx,std::min(x,hx))-x,dy=std::max(ly,std::min(y,hy))-y;
            brute=std::min(brute,dx*dx+dy*dy);
        }
        float actual=(px-x)*(px-x)+(py-y)*(py-y);
        assert(std::fabs(actual-brute)<.02f);
        float qx=px,qy=py;grid.project(qx,qy);assert(qx==px&&qy==py);
    }
    float x=109,y=14;grid.project(x,y);assert(x==109&&y==14);
    x=76.05f;y=96.5f;grid.project(x,y);assert(grid.contains(x,y)&&x>100);
    SparseResourceGrid empty;empty.reset(1,2,2,2,5);
    bool failed=false;try{empty.project(x,y);}catch(const std::runtime_error&){failed=true;}assert(failed);
    std::cout<<"5000 nearest-support/brute-force/idempotence tests passed\n";
}
