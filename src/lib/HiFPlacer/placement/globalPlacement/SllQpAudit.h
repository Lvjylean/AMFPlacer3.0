#pragma once
#include "FrozenQpArchive.h"
#include <memory>
class PlacementInfo;
class SllQpAudit {
public:
    SllQpAudit(PlacementInfo*,const std::map<std::string,std::string>&);
    ~SllQpAudit();
    void begin(size_t,const std::string&,int,int,QPSolverWrapper&,QPSolverWrapper&,float,float);
    bool target(int) const;
    void timingAnchor(int,bool,const QPSolverWrapper&,const QPSolverWrapper&);
    void checkpoint(const char*,const QPSolverWrapper&,const QPSolverWrapper&);
    void freeze(const amf_sll::Model&,const std::vector<unsigned char>&,const amf_sll::FrozenAxis&,
                const std::vector<double>&,double,bool,float,QPSolverWrapper&,QPSolverWrapper&);
    void result(const char*,const std::vector<double>&,const std::vector<double>&,QPSolverWrapper&);
private:
    struct Impl;std::unique_ptr<Impl> p;
};
