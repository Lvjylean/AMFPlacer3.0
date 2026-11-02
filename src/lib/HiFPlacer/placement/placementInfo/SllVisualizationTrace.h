#pragma once
// Independent, read-only visualization. Never refresh, legalize or export routes.
#include "SllVirtualModel.h"
#include <zlib.h>
#include <cstdio>

namespace amf_sll_trace {
inline std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if(c=='\\')out+="\\\\";
        else if(c=='\t')out+="\\t";
        else if(c=='\n')out+="\\n";
        else if(c=='\r')out+="\\r";
        else out+=c;
    }
    return out;
}

inline size_t writeSnapshot(const amf_sll_plan::Model* model, const std::string& path) {
    const std::string temporary=path+".tmp";
    gzFile file=gzopen(temporary.c_str(),"wb3");
    if(!file)throw std::runtime_error("Cannot create SLL visualization trace");
    size_t count=0;
    try {
        if(gzprintf(file,"#amf_sll_trace_v1\n#state\t%s\nkey\tnet_id\tnet\tdriver\tfrom\tto\tcolumn\tin_x\tin_y\tout_x\tout_y\tcapacity\tresource\n",
                    model?"recorded":"uninitialized")<=0)
            throw std::runtime_error("SLL trace header write failed");
        if(model)for(const auto& plan:model->plans)for(const auto& edge:plan.crossings) {
            const auto& c=model->geometry(edge);
            const amf_sll_plan::TileKey key{std::min(c.from,c.to),c.column,std::min(c.in.y,c.out.y)};
            const auto capacity=model->tileCapacity.at(key);
            if(gzprintf(file,"%s\t%d\t%s\t%s\t%d\t%d\t%d\t%.17g\t%.17g\t%.17g\t%.17g\t%zu\t%s\n",
                escape(edge.key).c_str(),plan.net.id,escape(plan.net.name).c_str(),
                escape(plan.net.source.name).c_str(),c.from,c.to,c.column,
                c.in.x,c.in.y,c.out.x,c.out.y,capacity,
                edge.channel>=0?escape(c.resource).c_str():"")<=0)
                throw std::runtime_error("SLL trace record write failed");
            ++count;
        }
        if(gzprintf(file,"#count\t%zu\n",count)<=0)
            throw std::runtime_error("SLL trace footer write failed");
    } catch(...) {gzclose(file);throw;}
    if(gzclose(file)!=Z_OK || std::rename(temporary.c_str(),path.c_str()))
        throw std::runtime_error("SLL visualization trace finalize failed");
    return count;
}
}
