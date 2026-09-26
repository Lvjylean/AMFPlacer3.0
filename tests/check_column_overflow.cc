#include "ColumnOverflow.h"
#include <cassert>

int main()
{
    using AMFColumnOverflow::requiredRelief;
    // GETRF reached 365 DSP rows against a 384 * 0.95 = 364.8 budget.
    // The previous integer truncation returned zero and never moved a macro.
    assert(requiredRelief(365, 384, 0.95f) == 1);
    assert(requiredRelief(366, 384, 0.95f) == 2);
    assert(requiredRelief(385, 384, 1.0f) == 1);
    // A fragmented column can fail contiguous fit before its row count is full.
    assert(requiredRelief(300, 384, 0.95f) >= 1);
    for (int capacity = 1; capacity <= 1024; ++capacity)
        assert(requiredRelief(capacity, capacity, 0.95f) >= 1);
}
