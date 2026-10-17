#!/usr/bin/env python3
"""Apply reviewed, exact-anchor hooks only in the independent SLL experiment."""
from pathlib import Path
ROOT=Path('/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261013-sll-quantity-01/repo')
def replace(path, old, new):
    p=ROOT/path
    text=p.read_text()
    if text.count(old)!=1:
        raise RuntimeError(f'{path}: expected one anchor, got {text.count(old)}')
    p.write_text(text.replace(old,new))

H='src/lib/HiFPlacer/placement/globalPlacement/WirelengthOptimizer.h'
C='src/lib/HiFPlacer/placement/globalPlacement/WirelengthOptimizer.cc'
G='src/lib/HiFPlacer/placement/globalPlacement/GlobalPlacer.cc'
replace(H,'#include "QPSolverWrapper.h"','#include "QPSolverWrapper.h"\n#include "SllQuantityOptimizer.h"\n#include <memory>')
replace(H,'    void reloadPlacementInfo();','''    void reloadPlacementInfo();
    void recordSllStage(const char *phase, int iteration, const char *step) {
        if (sllQuantity) sllQuantity->record(phase, iteration, step);
    }
    void endSllPhase() { if (sllQuantity) sllQuantity->endPhase(); }
''')
replace(H,'  private:\n','  private:\n    std::unique_ptr<SllQuantityOptimizer> sllQuantity;\n')
# Constructor and reload share several lines; anchor the constructor-only guard.
replace(C,'''    if (qpStabilityGuard && !useUnconstrainedCG)
        throw std::invalid_argument("QPStabilityGuard currently requires useUnconstrainedCG=true");''','''    if (qpStabilityGuard && !useUnconstrainedCG)
        throw std::invalid_argument("QPStabilityGuard currently requires useUnconstrainedCG=true");
    bool sllRequested = false;
    for (const auto &key : {"SllQuantityEnable", "SllQuantityDiagnostics"}) {
        auto it = JSONCfg.find(key);
        if (it != JSONCfg.end()) {
            if (it->second != "true" && it->second != "false")
                throw std::invalid_argument(std::string(key) + " must be true/false");
            sllRequested |= it->second == "true";
        }
    }
    if (JSONCfg.count("SllRegionPolicy") && JSONCfg.at("SllRegionPolicy") != "preserve")
        sllRequested = true;
    if (sllRequested) sllQuantity.reset(new SllQuantityOptimizer(placementInfo, JSONCfg));''')
replace(C,'''    else
        targetCellId = -1;
}''','''    else
        targetCellId = -1;
    if (sllQuantity) sllQuantity->reload();
}''')
replace(C,'''    ySolver->solverSettings.solutionForward = forwardSolutionToNextIteration;
''','''    ySolver->solverSettings.solutionForward = forwardSolutionToNextIteration;
    const bool sllCandidate = sllQuantity && sllQuantity->prepare(*xSolver, *ySolver);
''')
replace(C,'''    solverWriteBackData(displacementLimit);
''','''    if (sllCandidate) sllQuantity->accept(*xSolver, *ySolver, displacementLimit);
    else solverWriteBackData(displacementLimit);
''')
replace(G,'''            macro_mechanism::snapshot(placementInfo, CARRYMacroLegalizer, phase, i, step);
''','''            macro_mechanism::snapshot(placementInfo, CARRYMacroLegalizer, phase, i, step);
            WLOptimizer->recordSllStage(phase, i, step);
''')
replace(G,'''    // macroSpreader->spreadPlacementUnits();
}

void GlobalPlacer::GlobalPlacement_fixedCLB''','''    WLOptimizer->endSllPhase();
    // macroSpreader->spreadPlacementUnits();
}

void GlobalPlacer::GlobalPlacement_fixedCLB''')
with (ROOT/'src/CMakeLists.txt').open('a') as f:
    f.write('\n# Independent finite-difference / parallel-reduction / QP-surrogate checks.\nadd_executable(checkSllQuantity EXCLUDE_FROM_ALL tests/check_sll_quantity.cc)\n')
print('Applied optional SLL adapter hooks; stage budgets and baseline defaults unchanged.')
