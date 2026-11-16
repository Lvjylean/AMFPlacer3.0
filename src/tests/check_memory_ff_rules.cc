#include "MemoryFFRules.h"
#include <algorithm>
#include <cassert>
#include <iostream>
int main(){
 unsigned checked=0;
 for(unsigned packed=0;packed<65536;++packed){
  std::array<unsigned,4> m;for(unsigned i=0;i<4;++i)m[i]=(packed>>(4*i))&15;
  std::array<int,4> a={{-1,-1,-1,-1}},p={{0,1,2,3}};bool expected=false;
  do{bool ok=true;for(unsigned i=0;i<4;++i)ok &= bool(m[i]&(1u<<p[i]));expected |= ok;}while(std::next_permutation(p.begin(),p.end()));
  bool actual=MemoryFFRules::matchBank(m,4,a);assert(expected==actual);
  if(actual){unsigned used=0;for(unsigned i=0;i<4;++i){assert(m[i]&(1u<<a[i]));assert(!(used&(1u<<a[i])));used|=1u<<a[i];}}
  ++checked;
 }
 int n[9];std::array<int*,8> out;for(unsigned i=0;i<8;++i)out[i]=&n[i];
 for(unsigned mem=0;mem<256;++mem)for(unsigned d=0;d<9;++d){
  auto mask=MemoryFFRules::allowedSlots(mem,std::array<int*,8>{},out,&n[d],0);
  for(unsigned row=0;row<8;++row){
   assert(mask&MemoryFFRules::slotBit(row,0));
   assert(bool(mask&MemoryFFRules::slotBit(row,1))==(!(mem&(1u<<row))||d==row));
  }
  assert(MemoryFFRules::allowedSlots(mem,std::array<int*,8>{},out,&n[d],65535)==0);
 }
 assert(MemoryFFRules::allowedSlots(0,std::array<int*,8>{},out,static_cast<int*>(nullptr),0)==0);
 std::array<unsigned,4> m={{1,1,0,0}};std::array<int,4>a;
 assert(!MemoryFFRules::matchBank(m,2,a));assert(!MemoryFFRules::matchBank(m,5,a));
 std::cout<<"PASS "<<checked<<" exact matching domains and 2304 footprint/data cases\n";
}
