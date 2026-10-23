#pragma once
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <map>
#include <vector>
namespace legalization_trace {
using Clock=std::chrono::steady_clock;
inline bool enabled(){static bool on=std::getenv("AMF_LEGALIZATION_TRACE")!=nullptr;return on;}
inline int round=0, localIteration=-1, phaseCall=0;
inline const char *phase="initialization";
inline std::uint64_t call=0;
inline thread_local double costSeconds=0;
inline thread_local std::uint64_t costMisses=0;
inline double seconds(Clock::time_point a){return std::chrono::duration<double>(Clock::now()-a).count();}
struct CostScope {
 bool on; Clock::time_point start;
 CostScope():on(enabled()){if(on){start=Clock::now();++costMisses;}}
 ~CostScope(){if(on)costSeconds+=seconds(start);}
};
struct Candidate {
 std::uint64_t raw=0, distanceRejected=0, illegal=0, occupiedByMatch=0, legal=0, kept=0, misses=0;
 double filterSeconds=0, sortWithCostSeconds=0, costSeconds=0;
};
struct Graph {
 std::uint64_t edges=0, zero=0, components=0, largestLeft=0, largestRight=0, deficit=0, maxCompetition=0;
 double p50=0,p95=0,meanCompetition=0;
};
template<class Edges> Graph graph(const Edges &adj, int nr) {
 Graph g; const int nl=adj.size();std::vector<int> p(nl+nr),ls(p.size()),rs(p.size()),degree(nr),counts;
 for(int i=0;i<(int)p.size();++i)p[i]=i;
 auto root=[&](int x){while(p[x]!=x){p[x]=p[p[x]];x=p[x];}return x;};
 for(int u=0;u<nl;++u){counts.push_back(adj[u].size());g.zero+=adj[u].empty();
  for(const auto &e:adj[u]){++g.edges;++degree[e.first];int a=root(u),b=root(nl+e.first);if(a!=b)p[b]=a;}}
 for(int u=0;u<nl;++u)++ls[root(u)];for(int v=0;v<nr;++v)++rs[root(nl+v)];
 for(int i=0;i<(int)p.size();++i)if(ls[i]){++g.components;g.deficit+=std::max(0,ls[i]-rs[i]);
  if((std::uint64_t)ls[i]>g.largestLeft){g.largestLeft=ls[i];g.largestRight=rs[i];}}
 std::sort(counts.begin(),counts.end());if(nl){g.p50=counts[(nl-1)/2];g.p95=counts[(std::size_t)((nl-1)*.95)];}
 for(int d:degree)g.maxCompetition=std::max(g.maxCompetition,(std::uint64_t)d);
 g.meanCompetition=nr?(double)g.edges/nr:0;return g;
}
}
