#include "../ViroRenderer/VRODragSamplePolicy.h"
#include <cassert>
#include <limits>
#include <initializer_list>

int main() {
    // Slow writing: 100 sub-millimetre steps over a 5 mm letter stroke.
    int precise = 0;
    int ordinary = 0;
    float preciseLast = 0;
    float ordinaryLast = 0;
    for (int i = 1; i <= 100; ++i) {
        const float position = i * 0.00005f;
        if (VROShouldEmitDragSample(position - preciseLast, true, 0.01f)) {
            ++precise;
            preciseLast = position;
        }
        if (VROShouldEmitDragSample(position - ordinaryLast, false, 0.01f)) {
            ++ordinary;
            ordinaryLast = position;
        }
    }
    assert(precise == 100);
    assert(ordinary == 0);
    assert(VROShouldEmitDragSample(0.01f, false, 0.01f));
    assert(VROShouldEmitDragSample(0.02f, false, 0.01f));
    for (bool precision : {false, true}) {
        assert(!VROShouldEmitDragSample(0, precision, 0.01f));
        assert(!VROShouldEmitDragSample(-1, precision, 0.01f));
        assert(!VROShouldEmitDragSample(std::numeric_limits<float>::infinity(), precision, 0.01f));
        assert(!VROShouldEmitDragSample(std::numeric_limits<float>::quiet_NaN(), precision, 0.01f));
    }
}
