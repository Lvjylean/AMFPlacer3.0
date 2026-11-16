// Run on the real U250 bin grid using four single LUTs or four single FFs.
static void checkEmptyLogicPlacement(PI &pi, const std::vector<float> &seams, bool withTable)
{
    auto cells = pi.getDesignInfo()->getCells();
    const std::string resource = cells.front()->isFF() ? "SLICEL_FF" : "SLICEL_LUT";
    for (auto c : cells) check(resource=="SLICEL_FF" ? c->isFF() : c->isLUT(), "mixed logic fixture");
    pi.createGridBins(2,2);
    pi.updateElementBinGrid(false);
    auto &xy = pi.getCellId2location();
    auto &raw = pi.getBinGrid(pi.getSharedBELTypeId(resource));
    die_spread::GridView view(raw, pi.getCompatiblePlacementTable(), seams, xy.size(),
        [&](int id){ return xy.at(id); },
        [&](int id){ return int(pi.getCellId2CellBinInfo().at(id).occupation); });
    auto &grid = view.rows;
    const auto *mask = pi.logicEmptyBins.match(grid,resource,2,2);
    check(bool(mask)==withTable,"expected a matching device table");
    if (mask) check(mask==pi.logicEmptyBins.match(grid,resource,2,2),"verified grid not reused");

    auto run = [&](int x, int y, residual_spread::Rect bounds, bool secondary, bool useMask) {
        auto seed=grid.at(y).at(x);
        int n=0;
        for (auto c : cells) {
            int id=c->getCellId();
            check(view.memberBin(id)!=nullptr,"cell not charged to its resource");
            xy[id] = {seed->left()+(++n)*.2f*(seed->right()-seed->left()),seed->bottom()};
            view.update(c,xy[id].X,xy[id].Y);
        }
        auto initial=xy;
        const auto range=view.rowRange(y);
        GeneralSpreader::SpreadRegion region(seed,&pi,grid,.8f,100,range.first,range.second);
        std::set<die_spread::Bin*> claimed;
        region.claimResidualBox(bounds,claimed);
        region.decision.die=view.rowDie[y];
        if (secondary) region.decision.residualBins.push_back(seed);
        GeneralSpreader::SpreadRegion::SubBox box(&pi,&region,grid,.8f,100,true,useMask ? mask : nullptr);
        box.spreadAndPartition();
        std::vector<std::pair<int,int>> bins;
        for (auto c : cells) {
            int id=c->getCellId();
            auto b=view.at(xy[id].X,xy[id].Y);
            check(view.rowDie[b->Y()]==view.rowDie[y],"ordinary assignment crossed die");
            if (bounds.b==bounds.t) check(initial[id].Y==xy[id].Y,"horizontal split changed Y");
            bins.emplace_back(b->X(),b->Y());
        }
        return bins;
    };
    auto countX=[](const auto &bins,int x){return std::count_if(bins.begin(),bins.end(),
                                                [x](auto b){return b.first==x;});};
    const int y=115;
    check(grid[y][71]->getNominalCapacity()==0 && grid[y][70]->getNominalCapacity()>0 &&
          grid[y][72]->getNominalCapacity()>0,"fixture does not match U250 column 71");
    for (auto span : {std::pair<int,int>{70,71},{71,72},{70,72}}) {
        residual_spread::Rect box{span.first,span.second,y,y};
        auto prior=run(71,y,box,true,false);
        auto after=run(71,y,box,false,true);
        check(countX(after,71)==0,"assigned to a physically empty column");
        check(after==run(71,y,box,false,false),"file and nominal-slot paths differ");
        if (span.second-span.first==1)
            check(countX(prior,71)==4,"fixture failed to reproduce sole-strip bug");
        else check(countX(after,70)>0 && countX(after,72)>0,"both adjacent receivers needed");
        std::cout << "COLUMN " << resource << " box=" << span.first << ".." << span.second
                  << " legacy_empty=" << countX(prior,71) << " new_empty=" << countX(after,71)
                  << " left=" << countX(after,70) << " right=" << countX(after,72) << '\n';
    }
    check(countX(run(71,y,{71,71,y,y},false,true),71)==4,"invented an out-of-region receiver");
    check(run(69,y,{69,70,y,y},true,false)==run(69,y,{69,70,y,y},false,true),
          "populated strips changed");
    auto live=grid[y][70]; live->shrinkBinBy(1);
    check(live->getNominalCapacity()>0 && live->getCapacity()==0, "soft-zero fixture");
    check(run(70,y,{69,70,y,y},true,false)==run(70,y,{69,70,y,y},false,true),
          "soft target treated as a physical hole");
    live->resetBinShrinkRatio();
    const int seamRow=120;
    check(grid[seamRow][70]->getNominalCapacity()==0 && grid[seamRow-1][70]->getNominalCapacity()>0 &&
          view.rowDie[seamRow]==view.rowDie[seamRow-1],"missing SLR0 seam fragment");
    for (auto b : run(70,seamRow,{70,70,seamRow-1,seamRow},false,true))
        check(b.second==seamRow-1,"did not absorb from empty seam fragment on the same die");
    std::cout << "PASS physical zero-bin proposals " << resource << " table=" << withTable << '\n';
}
