#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace amf_sll_plan {
struct ColumnPolicy {
    bool enabled=false;
    double target=.75,maximum=.85,step=.01;
    static constexpr int unavailable=1000000;
    void validate()const {
        if(!(target>0&&target<=maximum&&maximum<=1&&step>0)||
           !std::isfinite(target)||!std::isfinite(maximum)||!std::isfinite(step))
            throw std::invalid_argument("Invalid SLL column target/maximum/step");
    }
    int tier(size_t count,size_t capacity)const {
        if(!enabled)return 0;
        if(!capacity || double(count)>=maximum*capacity-1e-9)return unavailable;
        if(double(count)<=target*capacity+1e-9)return 0;
        return std::max(1,int(std::ceil((double(count)/capacity-target)/step-1e-9)));
    }
};
}
