#ifndef AMF_SPARSE_CLOCK_GEOMETRY_H
#define AMF_SPARSE_CLOCK_GEOMETRY_H
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
// Explicit physical tile membership, independent of the nominal X/Y CR indices.
// Missing indices and holes remain missing. Nearest-region lookup is a fallback
// for continuous GP coordinates only; it never creates a legal placement site.
class SparseClockGeometry {
public:
    using Key=std::pair<int,int>;
    using Rect=std::array<float,4>;
    struct Region { int slr; Rect bounds; std::vector<Rect> bands; };
    std::map<Key,Region> regions;
    int nx=0,ny=0;
    explicit SparseClockGeometry(const std::string &path,const std::string &part) {
        std::ifstream f(path); std::string line,magic,actual;
        if(!std::getline(f,line)) throw std::runtime_error("Cannot read sparse CR geometry");
        { std::istringstream s(line); s>>magic>>actual; }
        if(magic!="AMF_SPARSE_CR_V1" || actual!=part) throw std::runtime_error("Sparse CR part/schema mismatch");
        while(std::getline(f,line)) {
            if(line.empty()) continue;
            std::istringstream s(line); char tag; int x,y,slr; Rect b; s>>tag>>x>>y;
            if(tag=='R') s>>slr;
            s>>b[0]>>b[1]>>b[2]>>b[3]; std::string extra;
            if(!s || s>>extra || x<0 || y<0 || b[0]>=b[1] || b[2]>=b[3]) throw std::runtime_error("Invalid sparse CR record");
            for(auto v:b) if(!std::isfinite(v)) throw std::runtime_error("Nonfinite sparse geometry");
            Key k{x,y};
            if(tag=='R') {
                if(!regions.emplace(k,Region{slr,b,{}}).second) throw std::runtime_error("Duplicate CR");
                nx=std::max(nx,x+1); ny=std::max(ny,y+1);
            } else if(tag=='B') {
                auto &r=regions.at(k); r.bands.push_back(b);
                if(b[0]<r.bounds[0] || b[1]>r.bounds[1] || b[2]<r.bounds[2] || b[3]>r.bounds[3]) throw std::runtime_error("Band outside CR bbox");
                for(int cy=std::ceil(b[2]);cy<b[3];++cy) for(int cx=std::ceil(b[0]);cx<b[1];++cx) {
                    auto v=tiles.emplace(tileKey(cx,cy),k);
                    if(!v.second && v.first->second!=k) throw std::runtime_error("Overlapping CR tile ownership");
                }
            } else throw std::runtime_error("Unknown sparse CR record");
        }
        if(regions.empty()) throw std::runtime_error("Empty sparse geometry");
        for(const auto &r:regions) if(r.second.bands.empty()) throw std::runtime_error("CR without physical tiles");
    }
    bool exact(float x,float y,Key &key) const {
        if(!std::isfinite(x)||!std::isfinite(y)) throw std::runtime_error("Nonfinite CR lookup");
        auto it=tiles.find(tileKey(int(std::floor(x+.5f)),int(std::floor(y+.5f))));
        if(it==tiles.end()) return false; key=it->second; return true;
    }
    Key nearest(float x,float y) const {
        Key k; if(exact(x,y,k)) return k;
        double best=std::numeric_limits<double>::infinity();
        for(const auto &r:regions) for(const auto &b:r.second.bands) {
            double dx=std::max(0.f,std::max(b[0]-x,x-b[1]));
            double dy=std::max(0.f,std::max(b[2]-y,y-b[3]));
            double d=dx*dx+dy*dy; if(d<best) {best=d;k=r.first;}
        }
        return k;
    }
    bool intersects(const Key &key,float left,float right,float bottom,float top) const {
        for(const auto &b:regions.at(key).bands)
            if(b[0]<=right && b[1]>=left && b[2]<=top && b[3]>=bottom) return true;
        return false;
    }
private:
    static long long tileKey(int x,int y) { return static_cast<long long>((static_cast<unsigned long long>(static_cast<unsigned int>(x))<<32) | static_cast<unsigned int>(y)); }
    std::unordered_map<long long,Key> tiles;
};
#endif
