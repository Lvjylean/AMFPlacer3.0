#include "../../src/lib/HiFPlacer/designInfo/native/LocalPinAccess.h"
#include <cassert>
using P=amf_native::LocalPinAccess;
int main() {
 P p;p.resources.resize(7);
 p.resources[0].occupied=true;p.resources[0].ownerPorts={"a/OUT"};
 p.resources[1].occupied=true; // Different static signal, even if its value happens to be zero.
 p.resources[2].tie=0;p.resources[3].tie=1;p.resources[4].bad=true;
 P::Demand x{"Q","new/Q",-1,{"a/OUT"},{{0}}};
 assert(p.solve({x}).feasible);x.ports={"a/OTHER"};assert(!p.solve({x}).feasible);
 x.ports.clear();x.constant=0;x.net="<const0>";x.entrances={{1},{3},{4}};assert(!p.solve({x}).feasible);
 x.entrances={{2}};assert(p.solve({x}).feasible);
 P::Demand a{"A","a",-1,{},{{5},{6}}},b{"B","b",-1,{},{{5}}},c{"C","c",-1,{},{{6}}};
 assert(p.solve({a,b}).feasible);assert(!p.solve({a,b,c}).feasible);
 c.net=b.net;c.entrances={{5}};assert(p.solve({a,b,c}).feasible);
 a.entrances={{5,6}};b.entrances={{6}};assert(!p.solve({a,b}).feasible);
 p.resources[1].ownerConstant=0;x.entrances={{1}};assert(p.solve({x}).feasible);
 x.constant=1;assert(!p.solve({x}).feasible);
 bool unresolved=false;try {p.solve({a,b},0);} catch(const std::runtime_error&){unresolved=true;}assert(unresolved);
}
