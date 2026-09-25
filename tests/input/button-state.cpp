#include "../../android/sharedCode/src/main/cpp/VROInputButtonState.h"
#include <cassert>
int main() {
    using Edge = VROInputButtonEdge;
    bool held = false;
    assert(updateInputButton(true, false, held) == Edge::None);
    assert(updateInputButton(true, true, held) == Edge::Down);
    assert(updateInputButton(true, true, held) == Edge::None);
    assert(updateInputButton(true, false, held) == Edge::Up);
    // Lost action / lost aim cancels without synthesizing a click completion.
    assert(updateInputButton(true, true, held) == Edge::Down);
    assert(updateInputButton(false, true, held) == Edge::Cancel);
    assert(!held);
    assert(updateInputButton(false, false, held) == Edge::None);
    assert(updateInputButton(true, true, held) == Edge::Down);
    assert(updateInputButton(true, false, held) == Edge::Up);
    // Controller takeover suppresses a pinched hand and permits a fresh pinch
    // only after the controller relinquishes the side.
    bool hand = true;
    assert(updateInputButton(false, true, hand) == Edge::Cancel);
    assert(updateInputButton(false, true, hand) == Edge::None);
    assert(updateInputButton(true, true, hand) == Edge::Down);
    assert(updateInputButton(true, false, hand) == Edge::Up);
}
