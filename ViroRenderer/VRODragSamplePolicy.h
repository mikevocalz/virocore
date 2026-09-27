#ifndef VRODragSamplePolicy_h
#define VRODragSamplePolicy_h

#include <cmath>

// Drawing surfaces opt in with highAccuracyEvents + dragTransform=None.
// They need every changed hit, including sub-millimetre handwriting. Ordinary
// object drags retain their bridge throttling. This adds no temporal filtering
// or prediction; the XR runtime already owns pose prediction.
inline bool VROShouldEmitDragSample(float distance, bool precisionSurface,
                                    float ordinaryThreshold) {
    return std::isfinite(distance) && distance > 0.0f &&
           (precisionSurface || distance >= ordinaryThreshold);
}

#endif
