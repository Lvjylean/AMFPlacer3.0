#include "../../src/lib/HiFPlacer/designInfo/native/LutPinAssignment.h"
#include <cassert>
using A=amf_native::LocalPinAccess;using L=amf_native::LutPinAssignment;
int main() {
 A a;a.resources.resize(6);a.resources[0].occupied=true;
 L::Input x{0,"I0",{"I0","n0",-1,{},{}},{{1,{{0}}},{2,{{1}}}}};
 L::Input y{0,"I1",{"I1","n1",-1,{},{}},{{1,{{2}}},{2,{{1}}}}};
 auto r=L::solve(a,{x,y});assert(r.feasible && r.lockPins[0]["I0"]==2 && r.lockPins[0]["I1"]==1);
 // Same net on two different logical inputs still requires distinct inputs.
 y.demand.net=x.demand.net;y.physical={{2,{{1}}}};assert(!L::solve(a,{x,y}).feasible);
 // The second LUT may share that physical input, if and only if the net agrees.
 y.cell=1;assert(L::solve(a,{x,y}).feasible);
 y.demand.net="different";assert(!L::solve(a,{x,y}).feasible);
 // Two accessible logical inputs still compete for a common routing entrance.
 x.physical={{1,{{3,5}}}};y.physical={{2,{{4,5}}}};
 assert(!L::solve(a,{x,y}).feasible);
 // Physical input numbers on distinct rows are independent.
 y.row=1;x.physical={{1,{{0}}}};y.physical={{1,{{1}}}};x.checkEntrance=false;y.checkEntrance=false;
 assert(L::solve(a,{x,y}).feasible);
 y.row=0;assert(!L::solve(a,{x,y}).feasible);
}
