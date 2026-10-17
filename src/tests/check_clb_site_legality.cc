#include "ParallelCLBPacker.h"
#include "simpleJSON.h"
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <omp.h>

static void require(bool value, const char *message) {if (!value) throw std::runtime_error(message);}

static void selfTest()
{
    using Cell=DesignInfo::DesignCell;
    using Mapping=ParallelCLBPacker::PackingCLBSite::SiteBELMapping;
    std::vector<std::unique_ptr<DesignInfo::DesignNet>> nets;
    for(int i=0;i<14;++i) {std::string name="n"+std::to_string(i);nets.emplace_back(new DesignInfo::DesignNet(name,i));}
    auto pin=[&](Cell &c,std::string ref,DesignInfo::DesignPinType type,int net){
        std::string name=c.getName()+"/"+ref;
        auto p=new DesignInfo::DesignPin(name,ref,type,true,&c,c.getPins().size());
        p->connectToNetVariable(nets[net].get());c.addPin(p);
    };
    Cell a(std::string("lut-a"),DesignInfo::CellType_LUT5,0);
    Cell b(std::string("lut-b"),DesignInfo::CellType_LUT5,1);
    Cell same(std::string("lut-same-inputs"),DesignInfo::CellType_LUT5,2);
    Cell six(std::string("lut-six"),DesignInfo::CellType_LUT6,3);
    for(int i=0;i<5;++i) {
        pin(a,"I"+std::to_string(i),DesignInfo::PinType_LUTInput,i);
        pin(b,"I"+std::to_string(i),DesignInfo::PinType_LUTInput,i+5);
        pin(same,"I"+std::to_string(i),DesignInfo::PinType_LUTInput,i);
    }
    require(!CLBSiteLegality::lutPairCompatible(&a,&b),"10 distinct LUT inputs accepted");
    require(CLBSiteLegality::lutPairCompatible(&a,&same),"shared five-input LUT pair rejected");
    require(!CLBSiteLegality::lutPairCompatible(&a,&six),"LUT6 partner accepted");
    Cell x(std::string("ff-x"),DesignInfo::CellType_FDRE,4);
    Cell y(std::string("ff-y"),DesignInfo::CellType_FDRE,5);
    Cell z(std::string("ff-z"),DesignInfo::CellType_FDRE,6);
    for(auto c:{&x,&y,&z}) {
        pin(*c,"C",DesignInfo::PinType_CLK,10);
        pin(*c,"CE",DesignInfo::PinType_E,c==&z?13:11);
        pin(*c,"R",DesignInfo::PinType_SR,c==&y?13:12);
    }
    DesignInfo::ControlSetInfo csX(&x,0),csY(&y,1),csZ(&z,2);
    x.setControlSetInfo(&csX);y.setControlSetInfo(&csY);z.setControlSetInfo(&csZ);
    Mapping m;m.FFs[0][1][0]=&x;
    require(!CLBSiteLegality::canPlaceFF(m,&y,0,0),"empty group bypassed sibling reset");
    require(CLBSiteLegality::canPlaceFF(m,&y,1,0),"independent half rejected");
    require(CLBSiteLegality::canPlaceFF(m,&z,0,0),"separate enable groups rejected");
    require(!CLBSiteLegality::canPlaceFF(m,&z,0,1),"same enable group mismatch accepted");
    Mapping dense;
    for(int i=0;i<2;++i)for(int j=0;j<2;++j)for(int k=0;k<4;++k)dense.LUTs[i][j][k]=&six;
    dense.LUTs[0][0][0]=&a;dense.LUTs[0][1][0]=nullptr;
    require(!dense.canDirectConnectInSlot(&b,&x),"post-map relocation accepted incompatible LUT pair");
    require(dense.canDirectConnectInSlot(&same,&x),"post-map relocation rejected compatible LUT pair");
    dense.addLUTFFPair(&same,&x);
    require(dense.LUTs[0][1][0]==&same && dense.FFs[0][1][0]==&x,"slot selection/commit differed");
    Mapping forbidden=dense;forbidden.removeLUTFFPair(&same,&x);
    forbidden.blockedFFMask=FfQPinMask::belBit("AFF2");
    require(!forbidden.canDirectConnectInSlot(&same,&x),"detailed placement reused blocked Q BEL");
    Mapping copied;copied=forbidden;require(copied.blockedFFMask==forbidden.blockedFFMask,"copied mapping lost mask");
    auto accept=[](const std::array<int,4>&){return true;};
    require(FfQPinMask::fits(0,{{4,4,4,4}},accept),"ordinary capacity changed");
    require(!FfQPinMask::fits(FfQPinMask::belBit("CFF2"),{{4,4,4,4}},accept),"16 FFs accepted into 15 BELs");
    require(FfQPinMask::fits(FfQPinMask::belBit("CFF2"),{{4,4,3,4}},accept),"logical groups cannot permute to available bank");
    require(!FfQPinMask::fits(FfQPinMask::belBit("CFF2"),{{4,4,3,4}},[](const std::array<int,4>&o){return o==std::array<int,4>{{0,1,2,3}};}),"fixed bank restriction ignored");
    std::cout<<"PASS CLB controls, shared LUT inputs, and detailed-placement slot commit\n";
}

int main(int argc, char **argv)
{
    try
    {
        if (argc==1) {selfTest();return 0;}
        if (argc != 4) throw std::runtime_error("Usage: checkCLBSiteLegality config.json requested.tsv violations.tsv");
        auto cfg = parseJSONFile(argv[1]); omp_set_num_threads(1);
        DeviceInfo device(cfg, cfg["device"]); DesignInfo design(cfg, &device);
        design.updateFFControlSets();
        using Mapping = ParallelCLBPacker::PackingCLBSite::SiteBELMapping;
        std::map<std::string, Mapping> sites;
        std::ifstream input(argv[2]);
        if (!input) throw std::runtime_error("Cannot open assignments");
        std::string line;
        while (std::getline(input, line))
        {
            auto tab = line.find('\t'); if (tab == std::string::npos) continue;
            std::string name = line.substr(0, tab), target = line.substr(tab+1);
            auto slash = target.find('/');
            if (target.substr(0, 6) != "SLICE_") continue;
            auto bel = target.substr(slash+1);
            if (bel.empty() || bel[0] < 'A' || bel[0] > 'H') continue;
            auto cell = design.getCell(name);
            int pos = bel[0]-'A', half = pos/4, row = pos%4;
            auto &mapping = sites[target.substr(0, slash)];
            if (bel.substr(1) == "FF") mapping.FFs[half][0][row] = cell;
            else if (bel.substr(1) == "FF2") mapping.FFs[half][1][row] = cell;
            else if (cell->isLUT() && bel.substr(1) == "6LUT") mapping.LUTs[half][0][row] = cell;
            else if (cell->isLUT() && bel.substr(1) == "5LUT") mapping.LUTs[half][1][row] = cell;
        }
        std::ofstream out(argv[3]); out << "site\trule\tcell\n";
        int badSites=0, lutPairs=0, controls=0;
        for (const auto &item : sites)
        {
            const auto &m = item.second; bool bad=false;
            for (int i=0; i<2; ++i) for (int k=0; k<4; ++k)
            {
                if (!CLBSiteLegality::lutPairCompatible(m.LUTs[i][0][k],m.LUTs[i][1][k]))
                {
                    out << item.first << "\tlut-inputs\t" << m.LUTs[i][0][k]->getName() << '\n';
                    ++lutPairs; bad=true;
                }
                for (int j=0;j<2;++j) if (!CLBSiteLegality::canPlaceFF(m,m.FFs[i][j][k],i,j))
                {
                    out << item.first << "\tff-controls\t" << m.FFs[i][j][k]->getName() << '\n';
                    ++controls; bad=true;
                }
            }
            badSites += bad;
        }
        std::cout << "CLB_LEGALITY_AUDIT sites=" << sites.size() << " bad_sites=" << badSites
                  << " lut_pairs=" << lutPairs << " ff_control_events=" << controls << '\n';
        return 0;
    }
    catch (const std::exception &e) {std::cerr << e.what() << '\n'; return 1;}
}
