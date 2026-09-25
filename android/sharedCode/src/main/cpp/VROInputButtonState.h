#pragma once

// A disappearing action is a cancellation, never a successful click release.
// Reset its edge history so a newly active press can start a fresh gesture.
enum class VROInputButtonEdge { None, Down, Up, Cancel };
inline VROInputButtonEdge updateInputButton(bool active, bool pressed, bool &previous) {
    const bool wasPressed = previous;
    previous = active && pressed;
    if (!active) return wasPressed ? VROInputButtonEdge::Cancel : VROInputButtonEdge::None;
    if (previous == wasPressed) return VROInputButtonEdge::None;
    return previous ? VROInputButtonEdge::Down : VROInputButtonEdge::Up;
}
