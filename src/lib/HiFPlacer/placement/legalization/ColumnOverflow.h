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
}
