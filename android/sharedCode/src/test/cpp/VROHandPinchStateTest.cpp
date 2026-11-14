#include "VROHandPinchState.h"
#include <cassert>
#include <limits>
int main() {
    assert(VROHandDirectionIsValid(0, 0, -1));
    assert(!VROHandDirectionIsValid(0, 0, 0));
    assert(!VROHandDirectionIsValid(std::numeric_limits<float>::infinity(), 0, 0));
    using Edge = VROHandPinchEdge;
    VROHandPinchState distance;
    assert(distance.updateDistance(true, .020f) == Edge::Down);
    assert(distance.updateDistance(true, .023f) == Edge::None);
    assert(distance.updateDistance(true, .025f) == Edge::Up);
    assert(distance.updateDistance(true, .019f) == Edge::Down);
    assert(distance.updateDistance(false, .019f) == Edge::Cancel);
    assert(distance.updateDistance(true, .019f) == Edge::None);
    assert(distance.updateDistance(true, .026f) == Edge::None);
    assert(distance.updateDistance(true, .019f) == Edge::Down);
    VROHandPinchState left, right;
    assert(left.update(true, .75f) == Edge::Down);
    assert(left.update(true, .65f) == Edge::None);
    assert(left.update(true, .56f) == Edge::None);
    assert(right.update(true, .8f) == Edge::Down);
    assert(left.update(false, .9f) == Edge::Cancel);
    assert(right.update(true, .65f) == Edge::None);
    assert(left.update(true, .9f) == Edge::None);
    assert(left.update(true, .55f) == Edge::None);
    assert(left.update(true, .75f) == Edge::Down);
    assert(left.update(true, .55f) == Edge::Up);
    assert(left.update(true, .75f) == Edge::Down);
    assert(left.update(true, std::numeric_limits<float>::quiet_NaN()) == Edge::Cancel);
    assert(left.update(true, .9f) == Edge::None);
    assert(left.update(true, 0) == Edge::None);
    assert(left.update(true, .9f) == Edge::Down);
    assert(right.update(true, .55f) == Edge::Up);
}
