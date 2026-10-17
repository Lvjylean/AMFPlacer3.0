#pragma once
#include <algorithm>
#include <cmath>

namespace AMFColumnOverflow
{
// Called only after capacity or contiguous-fit checks reject a column.
// Fractional budgets must still move at least one row; truncation can stall forever.
inline int requiredRelief(int usedRows, int availableRows, float budgetRatio)
{
    const float budget = availableRows * budgetRatio;
    return std::max(1, static_cast<int>(std::ceil(usedRows - budget)));
}

// Only the hard-region safe-transfer path uses these helpers. A positive share
// must not disappear through nearest-integer rounding. The caller still moves
// an indivisible whole macro and charges its actual height to the destination.
inline int directionalReliefCeil(int rows, double share)
{
    if (rows <= 0) return 0;
    return static_cast<int>(std::ceil(rows * std::max(0.0, std::min(1.0, share))));
}

inline bool wholeMacroFitsBudget(int usedRows, int macroRows, int availableRows, float budgetRatio)
{
    if (usedRows < 0 || macroRows <= 0 || availableRows < 0 ||
        !std::isfinite(budgetRatio) || budgetRatio <= 0 || budgetRatio > 1) return false;
    return static_cast<long long>(usedRows) + macroRows <=
           std::floor(static_cast<double>(availableRows) * budgetRatio);
}
}
