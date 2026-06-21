#pragma once

#include <functional>
#include <qopengl.h>   // GLuint

// Which built-in transition the crossfader applies between deck A and deck B.
// The numeric order is persisted in session files and mirrored by the
// transition combo box in MainWindow, so do not reorder existing entries.
enum class TransitionMode {
    Crossfade,      // alpha blend A→B (default)
    Cut,            // hard switch at fader centre
    WipeLeft,       // incoming wipes in from the left edge
    WipeRight,      // incoming wipes in from the right edge
    WipeUp,         // incoming wipes in from the bottom edge (moves up)
    WipeDown,       // incoming wipes in from the top edge (moves down)
    SlideLeft,      // outgoing slides out left; incoming pushes in from the right
    SlideRight,     // outgoing slides out right; incoming pushes in from the left
    SlideUp,        // outgoing slides out top; incoming pushes in from the bottom
    SlideDown,      // outgoing slides out bottom; incoming pushes in from the top
    DipToBlack,     // outgoing fades to black, then incoming fades in
    DipToWhite,     // outgoing fades to white, then incoming fades in
    Additive,       // additive mix (bright glow)
    CrossZoom,      // scale zooming transition
    SplitDoor,      // horizontal split doors sliding apart
    SplitDoorVert,  // vertical split doors sliding apart
    VortexSpin,     // spinning warp transition
    SplitQuadrants, // 4-corner split reveal
    Gallery3D,      // 3D picture-gallery slideshow transition
    Cube3D,         // 3D rotating cube transition
    Flip3D          // 3D flipping card transition
};

// Strategy interface for compositing the crossfade between two decks.
//
// Each concrete transition is stateless and paints the outgoing and incoming
// decks for a given progress, using whatever GL transforms/scissor it needs.
// The widget decides which deck is outgoing vs. incoming (so a transition reads
// the same whether the fader moves A→B or B→A) and exposes that through the
// Context's drawOut/drawIn callbacks.
class Transition {
public:
    struct Context {
        int   width  = 0;
        int   height = 0;
        // Progress: 0 = fully outgoing deck visible, 1 = fully incoming deck.
        float p      = 0.f;
        // Raw crossfade 0..1 (A→B). Used by transitions that are keyed to the
        // A/B decks directly rather than the direction-aware out/in roles.
        float t      = 0.f;
        // Paint the respective deck (clip + node chain) at the given alpha,
        // under the GL transform/scissor state set up by the transition.
        std::function<void(float alpha)> drawOut;
        std::function<void(float alpha)> drawIn;
        // Raw deck textures and readiness, for transitions (e.g. the 3D modes)
        // that composite full deck frames themselves under their own projection.
        GLuint texA   = 0;
        GLuint texB   = 0;
        bool   readyA = false;
        bool   readyB = false;
        // Deck FBO color textures are V-flipped vs. source textures.
        bool   texFlipped = false;
    };

    virtual ~Transition() = default;
    virtual void paint(const Context &ctx) const = 0;

    // Returns the shared, stateless strategy instance for a mode.
    static const Transition &forMode(TransitionMode mode);
};
