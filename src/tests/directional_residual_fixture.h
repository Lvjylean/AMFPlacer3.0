// This hook calls the actual residual entry on a cropped, two-die density
// grid. Charge/restore still use the real design's original placement bins.
struct CrossDieSpreadLimitTestAccess {
    static void run(const std::string &mode,PI &pi,std::map<std::string,std::string> cfg,float seam,
                    std::filesystem::path out){
        const bool quota=mode=="shared" || mode=="shared-zero";
        const bool hold=mode=="rollback" || mode=="shared-zero";
        cfg["SharedResidualSpreading"]=quota?"true":"false";
        if(quota) {
            using namespace shared_residual;
            check(!afterWrite(1,true,48,33).hard,"improving first write escalated");
            check(afterWrite(1,true,48,48).hard && afterWrite(1,true,48,51).hard,"stalled write not promoted");
            check(afterWrite(2,true,33,20).hard && !afterWrite(2,false,33,0).hard,"persistent or resolved residual");
            check(!afterWrite(0,true,0,4).hard,"unrelated new hotspot");
            std::vector<Bin> line{{-8,108,true,0,8},{-2,102,true,0,2},{10,90,true,0,0}};
            auto flow=allocate(line,3);
            check(flow.target==std::vector<double>({100,100,100}) && flow.relieved==10,"relay failed to reserve for interior");
            line[1].movable=6; flow=allocate(line,3);
            check(flow.relieved==10 && flow.target==std::vector<double>({100,4,100}),"whole box cannot use the far receiver");
            std::vector<Bin> twoReceivers{{4,0,true,0,0},{-10,10,true,0,10},{6,0,true,0,0}};
            flow=allocate(twoReceivers,3);
            check(flow.target==std::vector<double>({4,0,6}) && flow.relieved==10,"only one receiver was used");
            twoReceivers[1].movable=5; flow=allocate(twoReceivers,3);
            check(flow.target==std::vector<double>({2,0,3}) && flow.relieved==5,"fixed demand transferred");
            twoReceivers[1].open=false; flow=allocate(twoReceivers,3);
            check(flow.relieved==0 && flow.target[1]==5,"claimed bin participated");
            std::vector<Bin> fork{{-8,8,true,0,8},{10,0,true,0,0},{-8,8,true,0,8}};
            flow=allocate(fork,3);
            check(flow.relieved==10 && flow.target[1]==10 && flow.target[0]+flow.target[2]==6,"double-booked receiver");
            fork.back().die=1; flow=allocate(fork,3);
            check(flow.relieved==8 && flow.target[2]==8,"cross-die receiver");
        }
        std::string type="SLICEL_LUT";
        if(quota) {
            auto &grid=pi.getBinGrid(pi.getSharedBELTypeId(type));
            // Captured (8,115) seed and the conflicting neighbor at (8,116).
            residual_spread::Rect rect{8,8,106,118};
            GeneralSpreader::SpreadRegion region(grid[115][8],&pi,grid,.8f,100,0,int(grid.size()-1));
            std::set<die_spread::Bin*> claimed;
            region.claimResidualBox(rect,claimed);
            check(int(claimed.size())==rect.area(),"multi-row claim missed bins");
            residual_spread::each(rect,[&](int x,int y){check(claimed.count(grid[y][x]),"vertical rectangle hole");});
            check(!residual_spread::available({7,8,116,116},[&](int x,int y){
                return residual_spread::Account{0.,false,claimed.count(grid[y][x])!=0};
            }),"later region could occupy a registered row");
        }
        cfg["SllReportDirectory"]=out.string();
        cfg["DirectionalResidualSpreading"]="true";cfg["DetailedDiagnosticsEnable"]="true";
        cfg["CrossDieSpreadMaxBins"]="20";
        auto &units=pi.getPlacementUnits();
        const float anchorX=mode=="tie"?52.f:47.f;
        if(mode=="partial")units.back()->setFixed();
        for(auto pu:units){pu->setAnchorLocationAndForgetTheOriginalOne(anchorX,seam-2.5f);pu->recordSpreadLocatin();}
        if(mode=="group" || quota) units.back()->setAnchorLocationAndForgetTheOriginalOne(anchorX+5,seam-2.5f);
        if(quota) units.back()->recordSpreadLocatin();
        pi.updateElementBinGrid(false);
        CrossDieSpreadLimit limit;GeneralSpreader::initializeRoundLimit(&pi,cfg,limit);
        GeneralSpreader s(&pi,cfg,type,0,.8,false,&limit);
        check(s.directionalResidual && s.dieView.get(),"directional entry inactive");
        using B=die_spread::Bin;
        std::vector<std::unique_ptr<B>> storage;
        std::vector<std::unique_ptr<DeviceInfo::DeviceSite>> sites;
        std::string name="TEST_X0Y0",t="TEST";
        DeviceInfo::DeviceTile tile(name,t,nullptr,0);
        const int width=mode=="cross"?1:3;
        die_spread::Grid g(2);
        float cut=std::nextafter(seam,INFINITY);
        for(int y=0;y<2;++y)for(int x=0;x<width;++x) {
            storage.emplace_back(new B(type,45+x*5,50+x*5,cut+5*(y-1),cut+5*y,y,x,pi.getCompatiblePlacementTable()));
            auto b=storage.back().get();g[y].push_back(b);
            int n=y==1?8:(mode=="tie"?(x==1?1:8):(x==0?1:x==2?8:0));
            if(quota && y==0) n=x==2?8:1;
            for(int k=0;k<n;++k){
                std::string sn="SLICE_X"+std::to_string(x)+"Y"+std::to_string(8*y+k),st="SLICEL";
                sites.emplace_back(new DeviceInfo::DeviceSite(sn,st,&tile,47+x*5,cut+5*y-2,
                    0,0,int(sites.size()))); b->addSiteIntoBin(sites.back().get());
            }
        }
        if(quota) {
            const float charge=std::ceil(g[0][0]->spreadingDemandLimit(.8,true))+1;
            for(auto &w:pi.getCompatiblePlacementTable()->getcellId2Occupation()) w=charge;
            pi.updateElementBinGrid(false);
        }
        auto &xy=pi.getCellId2location();
        auto weight=[&](int i){return pi.getCellId2CellBinInfo().at(i).occupation;};
        for(auto c:pi.getDesignInfo()->getCells()) {
            int col=int((xy.at(c->getCellId()).X-45)/5);
            g[0][col]->addCell(c,weight(c->getCellId()));
        }
        s.dieView.reset(new die_spread::GridView(g,pi.getCompatiblePlacementTable(),{seam},xy.size(),
            [&](int i){return xy[i];},weight));
        s.binGrid=s.dieView->rows;s.dieSupply=s.dieView->supply();
        auto seed=s.dieView->at(anchorX,seam-2.5f);
        check(seed->isSpreadingOverflow(.8f),"fixture seed must be overflowing");
        if(mode=="reserved")s.coveredBinSet.insert(s.binGrid[0][2]);
        auto initial=xy;
        std::vector<PI::Location> last;
        for(auto pu:units)last.push_back({pu->lastX(),pu->lastY()});
        int initialLoad=0;
        for(auto &row:s.binGrid)for(auto b:row)initialLoad+=b->getUtilization();
        s.overflowBins.push_back(seed);
        s.localFailures[{seed->X(),seed->Y()}]={true,false};
        if(mode=="group" || quota) {
            auto neighbor=s.binGrid[0][1];
            s.overflowBins.push_back(neighbor);
            s.localFailures[{neighbor->X(),neighbor->Y()}]={true,false};
        }
        double seedBefore=seed->getUtilization();
        std::set<B*> handled;
        if(quota) {
            s.residualHistory.resize(width*2);
            for(const auto &row:s.binGrid) for(auto bin:row)
                s.preWriteOverflow.push_back(std::max(0.,double(bin->getUtilization())-bin->spreadingDemandLimit(.8,true)));
            s.residualHistory[seed->Y()*width+seed->X()]={1,true};
        }
        s.planResidualRegions(2,100,handled);
        for(auto c:pi.getDesignInfo()->getCells()){
            int i=c->getCellId();
            check(xy[i].X==initial[i].X && xy[i].Y==initial[i].Y,"selection changed cell coordinates");
        }
        for(size_t i=0;i<units.size();++i)
            check(last[i].X==units[i]->lastX() && last[i].Y==units[i]->lastY(),"selection changed anchor history");
        if(mode=="reserved") {
            check(s.expandedRegions.empty(),"claimed local receiver must defer cross");
            return;
        }
        check(s.expandedRegions.size()==1,"single selected region");
        auto region=s.expandedRegions.front();
        auto &d=region->decision;
        check(d.local==(mode!="cross"),"wrong local/cross decision");
        if(mode=="group") check(d.residualBins.size()==2,"adjacent residuals were not merged");
        if(mode=="local" || mode=="partial") check(region->right()==2 && region->top()==0,"bridge missing");
        std::set<DesignInfo::DesignCell*> involved(region->getCells().begin(),region->getCells().end());
        std::vector<PI::PlacementUnit*> selected;
        std::set<PI::PlacementUnit*> unique;
        std::map<int,PI::Location> before;
        for(auto cell:involved){
            auto pu=pi.getPlacementUnitByCell(cell);
            if(unique.insert(pu).second){selected.push_back(pu);before[pu->getId()]={pu->X(),pu->Y()};}
        }
        GeneralSpreader::SpreadRegion::SubBox box(&pi,region,s.binGrid,.8,100,true);
        if(quota) {
            check(d.local && d.plannedRelief>0 && d.residualBins.size()==2,"real shared planning and two source bins");
            s.partitionShared(*region);
        } else box.spreadAndPartition();
        // A stationary proposal makes the zero-relief case independent of the
        // fixed residual writeback weight.
        if (quota && hold) xy = initial;
        const auto proposed = xy;
        s.activateCrossDieLimits();
        s.updatePlacementUnitsWithSpreadedCellLocations(unique,involved,selected,hold?0.f:(quota ? .2f : 1.f),false,-1);
        if (quota && !hold) {
            auto firstWrite = xy;
            bool tested = false;
            for (auto cell : involved) {
                int id = cell->getCellId();
                auto pu = pi.getPlacementUnitByCell(cell);
                if (!dynamic_cast<PI::PlacementUnpackedCell*>(pu) || pu->isFixed() || pu->isLocked()) continue;
                if (std::fabs(proposed[id].X-initial[id].X)<.01f) continue;
                const auto &target = proposed[id];
                check(std::fabs(xy[id].X-target.X)<.0001f && std::fabs(xy[id].Y-target.Y)<.0001f,"secondary uses the full proposal");
                tested = true;
            }
            check(tested,"missing movable singleton with a changed proposal");
            xy = proposed;
            s.updatePlacementUnitsWithSpreadedCellLocations(unique,involved,selected,1.f,false,-1);
            for (auto cell : involved) {
                int id = cell->getCellId();
                check(std::fabs(xy[id].X-firstWrite[id].X)<.0001f &&
                      std::fabs(xy[id].Y-firstWrite[id].Y)<.0001f,"secondary writes accumulated");
            }
        }
        s.constrainLocalWrites(selected,before);
        s.constrainCrossDieWrites(selected,before);
        if(!s.crossTargetDie.empty())s.constrainLocalWrites(selected,before);
        s.finishDieFirstIteration(1,2,selected,before);
        s.residualLog.flush();
        const auto groupRows=rows(out/"spreading-residual-groups.tsv");
        check(groupRows.size()==1,"one completed group write");
        auto log=groupRows.front();
        bool changed=false;
        int total=0;
        for(auto &row:s.binGrid)for(auto b:row)total+=b->getUtilization();
        check(total==initialLoad,"private density not conserved");
        for(auto c:pi.getDesignInfo()->getCells()){
            const int i=c->getCellId();auto pu=pi.getPlacementUnitByCell(c);
            float ox=0,oy=0;
            if(auto m=dynamic_cast<PI::PlacementMacro*>(pu)){ox=m->getCellOffsetXInMacro(c);oy=m->getCellOffsetYInMacro(c);}
            check(xy[i].X==pu->X()+ox && xy[i].Y==pu->Y()+oy,"whole-PU offsets changed");
            check(s.dieView->at(xy[i].X,xy[i].Y)->contains(c),"private bin membership");
            int x,y;pi.getGridXY(xy[i].X,xy[i].Y,x,y);auto idx=pi.getCellId2CellBinInfo().at(i);
            check(idx.X==x && idx.Y==y && pi.getBinGrid(idx.sharedTypeId)[y][x]->contains(c),"original bin membership");
            changed|=xy[i].X!=initial[i].X || xy[i].Y!=initial[i].Y;
            if(mode!="cross")check(xy[i].Y<=seam,"unauthorized crossing");
        }
        check(changed==!hold,"unexpected disposition after real write");
        if(quota) {
            double after=0;
            for(const auto &row:s.binGrid) for(auto b:row) after+=std::max(0.,double(b->getUtilization())-b->spreadingDemandLimit(.8,true));
            double beforeTotal=std::accumulate(s.preWriteOverflow.begin(),s.preWriteOverflow.end(),0.);
            check(hold ? after==beforeTotal : after<beforeTotal,"whole-PU measured relief");
            if(hold) check(s.residualHistory[0].hard && s.residualHistory[1].hard,"stalled window failed to promote");
            std::cout<<"MEASURE "<<mode<<" planned="<<d.plannedRelief<<" actual="<<(beforeTotal-after)
                     <<" before="<<beforeTotal<<" after="<<after<<" left_bin_freed="<<seedBefore-seed->getUtilization()<<" partitions=1"<<std::endl;
        }
        if(hold) {
            check(!s.failedPlans.empty(),"unchanged hotspot must remember the failed shape");
        } else {
            check(std::stod(log["hot_after"])<std::stod(log["hot_before"]),"fixture residual failed to decline");
        }
        if(mode=="partial")units.back()->setUnfixed();
        delete region;
        s.expandedRegions.clear();
        std::cout<<"PASS "<<mode<<" group="<<log["group_bins"]<<" hot="<<log["hot_before"]<<" -> "<<log["hot_after"]<<"\n";
    }
};
