#include "../../src/lib/HiFPlacer/designInfo/native/LocalAccessDatabase.h"
#include <cassert>
using DB=amf_native::LocalAccessDatabase;
int main() {
 const std::string data="AMF_LOCAL_ACCESS\t2\nN\t0\tnode0\t-1\t0\t1\t-1\ta/out\nN\t1\tnode1\t-1\t0\t0\t-1\t\nS\ta\tsite\nP\ta\tsite\tAFF\tQ\tOUT\t0,1\n";
 DB db;db.load(data,"a","new/Q\ta/out\n");
 auto paths=db.lookup("site","AFF","Q");assert(paths&&paths->size()==1);
 assert(db.access.solve({db.demand("Q","new/Q",*paths)}).feasible);
 assert(!db.access.solve({db.demand("Q","different/Q",*paths)}).feasible);
 assert(db.lookup("outside","AFF","Q")==nullptr);
 bool caught=false;try{db.lookup("site","AFF","UNKNOWN");}catch(const std::runtime_error&){caught=true;}assert(caught);
 caught=false;try{DB b;b.load(data,"b","");}catch(const std::runtime_error&){caught=true;}assert(caught);
 caught=false;try{DB b;b.load(data+"P\ta\tsite\tAFF\tQ\tOUT\t0,1\n","a","");}catch(const std::runtime_error&){caught=true;}assert(caught);
}
