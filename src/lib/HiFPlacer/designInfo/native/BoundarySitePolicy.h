#ifndef AMF_BOUNDARY_SITE_POLICY_H
#define AMF_BOUNDARY_SITE_POLICY_H
#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace amf_native {
// Placement-only, opt-in DFX policy. Does not modify device topology, Pblocks,
// physical routing, fixed context, netlists, or ordinary full-placement code.
struct BoundarySitePolicy {
    static constexpr const char *version = "clock-region-slice-dsp-v1";
    struct Site { std::string name, type, family, cr; int x=0,y=0; bool prohibit=false; };
    struct Result {
        std::map<std::string,std::set<std::string>> prohibited, margin;
        std::string audit;
    };
    static void require(bool ok,const std::string &msg) {
        if(!ok)throw std::runtime_error("DFX boundary policy: "+msg);
    }
    static std::vector<std::string> fields(const std::string &s) {
        std::vector<std::string> r;std::size_t p=0,q;
        do {q=s.find('\t',p);r.push_back(s.substr(p,q-p));p=q+1;}while(q!=std::string::npos);
        return r;
    }
    static std::vector<std::vector<std::string>> rows(const std::string &path,const std::string &header) {
        std::ifstream f(path);std::string s;require(bool(std::getline(f,s))&&s==header,"bad table "+path);
        std::vector<std::vector<std::string>> out;
        while(std::getline(f,s)){require(!s.empty(),"blank row");out.push_back(fields(s));}return out;
    }
    static Result derive(const std::string &inventory,const std::string &geometry,
                         const std::map<std::string,std::set<std::string>> &allowed,
                         const std::string &sidePolicy) {
        require(sidePolicy=="any-prohibit"||sidePolicy=="interior-prohibit","unknown side policy");
        std::map<std::string,std::set<std::string>> crs;
        const std::regex range("CLOCKREGION_X([0-9]+)Y([0-9]+)(:CLOCKREGION_X([0-9]+)Y([0-9]+))?");
        for(const auto &r:rows(geometry,"region\tclock_regions")) {
            require(r.size()==2&&allowed.count(r[0])&&!crs.count(r[0]),"unknown/duplicate region");
            std::istringstream words(r[1]);std::string word;std::set<std::pair<int,int>> points;
            while(words>>word) {
                std::smatch m;require(std::regex_match(word,m,range),"requires whole clock-region ranges");
                int x0=std::stoi(m[1]),y0=std::stoi(m[2]),x1=m[3].matched?std::stoi(m[4]):x0,y1=m[3].matched?std::stoi(m[5]):y0;
                require(x0<=x1&&y0<=y1&&x1-x0<1024&&y1-y0<1024,"invalid clock-region range");
                for(int x=x0;x<=x1;++x)for(int y=y0;y<=y1;++y)points.emplace(x,y);
            }
            require(!points.empty(),"empty clock-region range");
            std::set<int> xs,ys;for(auto p:points){xs.insert(p.first);ys.insert(p.second);}
            require(points.size()==xs.size()*ys.size()&&*xs.rbegin()-*xs.begin()+1==int(xs.size())&&
                    *ys.rbegin()-*ys.begin()+1==int(ys.size()),"nonrectangular/disconnected Pblock needs explicit policy");
            for(auto p:points)crs[r[0]].insert("CLOCKREGION_X"+std::to_string(p.first)+"Y"+std::to_string(p.second));
        }
        require(crs.size()==allowed.size(),"incomplete region geometry");
        std::map<std::string,std::vector<Site>> sites;std::map<std::string,std::set<std::string>> seen;
        Result out;
        const std::regex xy("(SLICE|DSP48E2)_X([0-9]+)Y([0-9]+)");
        for(const auto &r:rows(inventory,"region\tsite\ttype\tclock_region\tprohibit")) {
            require(r.size()==5&&allowed.count(r[0])&&seen[r[0]].insert(r[1]).second,"unknown/duplicate site row");
            require(r[4]=="0"||r[4]=="1","invalid PROHIBIT value");
            const auto cr=r[3].compare(0,12,"CLOCKREGION_")==0?r[3]:"CLOCKREGION_"+r[3];
            require(crs.at(r[0]).count(cr),"site clock region outside Pblock");
            Site s;s.name=r[1];s.type=r[2];s.cr=r[3];s.prohibit=r[4]=="1";
            // Existing true prohibitions apply to every resource, including
            // BRAM. The additional geometric margin applies ONLY to SLICE/DSP.
            if(s.prohibit&&allowed.at(r[0]).count(s.name))out.prohibited[r[0]].insert(s.name);
            std::smatch m;
            if(std::regex_match(s.name,m,xy)) {
                s.family=m[1];s.x=std::stoi(m[2]);s.y=std::stoi(m[3]);
                require((s.family=="SLICE"&&(s.type=="SLICEL"||s.type=="SLICEM"))||
                        (s.family=="DSP48E2"&&s.type=="DSP48E2"),"site family/type mismatch");
                sites[r[0]].push_back(s);
            }else require(s.type!="SLICEL"&&s.type!="SLICEM"&&s.type!="DSP48E2","unrecognized fabric site name");
        }
        std::ostringstream audit;
        audit<<"region\tfamily\txmin\txmax\tymin\tymax\tleft_prohibit_all\tright_prohibit_all\tleft_prohibit_interior\tright_prohibit_interior\tleft_margin\tright_margin\tmargin_sites\n";
        for(const auto &a:allowed) {
            for(const auto &name:a.second)require(seen[a.first].count(name),"incomplete cached site coverage: "+name);
            for(const auto &family:{std::string("SLICE"),std::string("DSP48E2")}) {
                std::vector<Site> group;for(const auto &s:sites[a.first])if(s.family==family)group.push_back(s);
                if(group.empty())continue;
                int xmin=group[0].x,xmax=xmin,ymin=group[0].y,ymax=ymin;
                for(const auto &s:group){xmin=std::min(xmin,s.x);xmax=std::max(xmax,s.x);ymin=std::min(ymin,s.y);ymax=std::max(ymax,s.y);}
                int la=0,ra=0,li=0,ri=0;
                for(const auto &s:group)if(s.prohibit) {
                    if(s.x==xmin){++la;if(s.y!=ymin&&s.y!=ymax)++li;}
                    if(s.x==xmax){++ra;if(s.y!=ymin&&s.y!=ymax)++ri;}
                }
                bool left=(sidePolicy=="any-prohibit"?la:li)>0,right=(sidePolicy=="any-prohibit"?ra:ri)>0;
                std::size_t count=0,remaining=0;
                for(const auto &s:group)if(a.second.count(s.name)) {
                    if(s.y==ymin||s.y==ymax||(left&&s.x==xmin)||(right&&s.x==xmax)) {
                        out.margin[a.first].insert(s.name);++count;
                    }else if(!s.prohibit)++remaining;
                }
                require(remaining>0,"margin exhausts "+a.first+"/"+family);
                audit<<a.first<<'\t'<<family<<'\t'<<xmin<<'\t'<<xmax<<'\t'<<ymin<<'\t'<<ymax<<'\t'
                     <<la<<'\t'<<ra<<'\t'<<li<<'\t'<<ri<<'\t'<<left<<'\t'<<right<<'\t'<<count<<'\n';
            }
        }
        out.audit=audit.str();return out;
    }
};
}
#endif
