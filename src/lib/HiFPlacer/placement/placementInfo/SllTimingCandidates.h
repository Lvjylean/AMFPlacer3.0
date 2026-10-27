#pragma once
#include "SllVirtualModel.h"
namespace amf_sll_plan {
class TimingCandidates {
    struct Bucket {double inputX,outputX;std::vector<std::pair<double,int>> rows;};
    std::vector<Bucket> buckets;
public:
    void build(const std::vector<Channel>&channels,const std::vector<int>&ids){
        buckets.clear();std::map<std::pair<double,double>,std::vector<std::pair<double,int>>> grouped;
        for(int id:ids){const auto&c=channels.at(id);grouped[{c.in.x,c.out.x}].emplace_back(c.in.y,id);}
        for(auto&item:grouped){std::sort(item.second.begin(),item.second.end());
            buckets.push_back({item.first.first,item.first.second,std::move(item.second)});}
    }
    std::vector<std::pair<double,int>> nearest(Point source,double targetX,double ratio,size_t count)const{
        std::vector<std::pair<double,int>> near;if(!count)return near;near.reserve(count*buckets.size());
        for(const auto&bucket:buckets){const auto&rows=bucket.rows;if(rows.empty())continue;
            auto split=std::lower_bound(rows.begin(),rows.end(),source.y,[](const auto&row,double y){return row.first<y;});
            std::ptrdiff_t left=split-rows.begin()-1;size_t right=split-rows.begin();
            // Preserve the legacy floating-point expression, including its addition order.
            auto score=[&](size_t i){return length(source,{bucket.inputX,rows[i].first},ratio)+std::abs(bucket.outputX-targetX);};
            auto takeLeft=[&]{near.emplace_back(score(size_t(left)),rows[size_t(left)].second);--left;};
            auto takeRight=[&]{near.emplace_back(score(right),rows[right].second);++right;};
            for(size_t i=0;i<std::min(count,rows.size());++i){
                if(left>=0&&(right==rows.size()||score(size_t(left))<=score(right)))takeLeft();else takeRight();}
            // Include every score tie at the cutoff. This also handles ratio=0
            // and floating-point plateaus without dropping a lower channel ID.
            const double cutoff=near.back().first;
            while(left>=0&&score(size_t(left))<=cutoff)takeLeft();
            while(right<rows.size()&&score(right)<=cutoff)takeRight();
        }
        const size_t keep=std::min(count,near.size());
        std::partial_sort(near.begin(),near.begin()+keep,near.end());near.resize(keep);return near;
    }
};
}
