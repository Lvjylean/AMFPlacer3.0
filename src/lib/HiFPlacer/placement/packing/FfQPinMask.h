#ifndef AMF_FF_Q_PIN_MASK_H
#define AMF_FF_Q_PIN_MASK_H
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

// Site-local FF positions; no device coordinates or design cell names.
namespace FfQPinMask {
inline unsigned bit(int half,int lane,int row) { return 1u << (half*8+lane*4+row); }
inline unsigned belBit(const std::string &bel) {
    if(bel.size()<3 || bel[0]<'A' || bel[0]>'H' ||
       (bel.substr(1)!="FF" && bel.substr(1)!="FF2"))
        throw std::runtime_error("Invalid FF Q access BEL: "+bel);
    const int letter=bel[0]-'A';
    return bit(letter/4,bel.size()==4,letter%4);
}
inline int capacity(unsigned mask,int bank) {
    int n=0;for(int k=0;k<4;++k) if(!(mask&bit(bank/2,bank%2,k)))++n;return n;
}
// Caller's compatibility predicate retains clock/reset, main-lane and macro
// restrictions. Logical control sets are not tied to physical lane numbers.
template<class Compatible>
bool fits(unsigned mask,const std::array<int,4> &sizes,Compatible compatible) {
    std::array<int,4> order{{0,1,2,3}};
    do {
        bool ok=true;
        for(int bank=0;bank<4;++bank)
            if(sizes[order[bank]]>capacity(mask,bank)) {ok=false;break;}
        if(ok&&compatible(order))return true;
    }while(std::next_permutation(order.begin(),order.end()));
    return false;
}
}
#endif
