#ifndef AMF_BOUNDARY_AWARE_CLUSTERER_H
#define AMF_BOUNDARY_AWARE_CLUSTERER_H
#include "PlacementTimingOptimizer.h"
#include <map>
#include <vector>
class BoundaryAwareClusterer
{
  public:
    using PU = PlacementInfo::PlacementUnit;
    using Node = PlacementTimingInfo::TimingGraph<DesignInfo::DesignCell>::TimingNode;
    using Edge = PlacementTimingInfo::TimingGraph<DesignInfo::DesignCell>::TimingEdge;
    BoundaryAwareClusterer(PlacementInfo *placement, PlacementTimingOptimizer *timing,
                           std::map<std::string,std::string> &config);
    void run();
    void refresh();
    void audit(const std::string &stage);
  private:
    struct Score { double weighted=0, worst=0, displacement=0; };
    std::vector<Edge *> affectedEdges(const std::vector<PU *> &units) const;
    bool targets(const std::vector<PU *> &units,int region,std::map<PU *,std::pair<float,float>> &positions) const;
    Score score(const std::vector<Edge *> &edges,const std::map<PU *,std::pair<float,float>> &positions) const;
    PlacementInfo *placement;
    PlacementTimingOptimizer *timing;
    PhysicalBoundaryModel *model;
    std::map<std::string,std::string> &config;
    int maxClusters=512,maxPathCells=128,maxEdges=8192;
    float y2xRatio=0.4f;
    float nearCriticalFraction=0.15f,maxDisplacement=280.0f,minGain=0.05f,displacementCost=0.0001f;
};
#endif

