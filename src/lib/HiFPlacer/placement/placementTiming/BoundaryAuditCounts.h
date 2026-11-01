#ifndef AMF_BOUNDARY_AUDIT_COUNTS_H
#define AMF_BOUNDARY_AUDIT_COUNTS_H
#include "../../../utils/RuntimeProfiler.h"
#include <cstddef>
#include <omp.h>

namespace boundary_audit {
struct Counts { long long slr=0,io=0,crossed=0,valid=0; };
// Read-only snapshot; exact integer reductions. The caller still sorts endpoints
// and writes every report in the legacy order. No timing/placement state changes.
template<class Edges,class Pins,class Model>
Counts count(const Edges &edges,const Pins &pins,const Model &model) {
    AMF_PROFILE_FUNCTION("boundary_report_counts");
    long long slr=0,io=0,crossed=0,valid=0;
#pragma omp parallel for schedule(static) if(edges.size() >= 4096 && !omp_in_parallel()) reduction(+:slr,io,crossed,valid)
    for(std::size_t k=0;k<edges.size();++k) {
        auto edge=edges[k];
        auto a=pins[edge->getSourcePin()->getElementIdInType()],b=pins[edge->getSinkPin()->getElementIdInType()];
        if((a.X<-5 && a.Y<-5)||(b.X<-5 && b.Y<-5))continue;
        int s=model.crossingCount(a.X,a.Y,b.X,b.Y,"SLR"),i=model.crossingCount(a.X,a.Y,b.X,b.Y,"IO");
        slr+=s;io+=i;crossed+=(s+i)>0;++valid;
    }
    return {slr,io,crossed,valid};
}
}
#endif
