#ifndef AMF_SLL_PLANNER_H
#define AMF_SLL_PLANNER_H
#include "SllVirtualModel.h"
#include "Eigen/Core"
#include "Eigen/SparseCore"
#include <memory>
class PlacementInfo;
class SllPlanner {
public:
    SllPlanner(PlacementInfo*,const std::map<std::string,std::string>&);
    ~SllPlanner();
    void refresh(); // Serial outer-step boundary, never inside parallel readers.
    bool jointPlacement()const;
    size_t variableCount()const;
    std::vector<amf_sll_plan::Point> virtualPositions()const;
    void prepareDensityTargets();
    void addDensityModel(std::vector<float>&,Eigen::VectorXd&,bool)const;
    void projectQp(const Eigen::VectorXd&,const Eigen::VectorXd&,double displacementLimit=-10);
    bool handles(int designNetId)const;
    void addWireModel(int designNetId,std::vector<Eigen::Triplet<float>>&,std::vector<float>&,Eigen::VectorXd&,float,float,bool)const;
    bool pinDelay(int sourcePin,int sinkPin,amf_sll_plan::Point,amf_sll_plan::Point,double&)const;
    bool cellDelay(int sourceCell,int sinkCell,amf_sll_plan::Point,amf_sll_plan::Point,double&)const;
    double geometryDelay(amf_sll_plan::Point,amf_sll_plan::Point)const;
    bool crosses(amf_sll_plan::Point,amf_sll_plan::Point)const;
    void addTimingModel(std::vector<Eigen::Triplet<float>>&,std::vector<float>&,Eigen::VectorXd&,float,bool)const;
    double wireLength(int designNetId,double ratio,int trialUnit=-1,amf_sll_plan::Point trial={})const;
    void setLocalDelay(amf_sll_plan::LocalDelay);
    void exportPlan(const std::string&);
    const amf_sll_plan::Model& model()const;
private:
    struct Impl;std::unique_ptr<Impl> p;
};
#endif
