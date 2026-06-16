#include "ui/Transition.h"
#include <QOpenGLFunctions>   // pulls in the desktop GL fixed-function symbols
#include <algorithm>

namespace {

// Translate to the frame centre, scale, and translate back, so a wrapped draw
// call is scaled about the middle of the output.
void zoomAboutCentre(int w, int h, float scale) {
    glTranslatef(w / 2.f, h / 2.f, 0.f);
    glScalef(scale, scale, 1.f);
    glTranslatef(-w / 2.f, -h / 2.f, 0.f);
}

class CrossfadeTransition : public Transition {
public:
    void paint(const Context &c) const override {
        c.drawOut(1.f - c.p);
        c.drawIn(c.p);
    }
};

class CutTransition : public Transition {
public:
    void paint(const Context &c) const override {
        if (c.p < 0.5f) c.drawOut(1.f);
        else            c.drawIn(1.f);
    }
};

// ── Wipes: outgoing fills the frame, incoming is revealed by a scissor rect ──

class WipeLeftTransition : public Transition {
public:
    void paint(const Context &c) const override {
        c.drawOut(1.f);
        if (c.p > 0.f) {
            glEnable(GL_SCISSOR_TEST);
            // glScissor origin is bottom-left in framebuffer coordinates.
            glScissor(0, 0, static_cast<GLint>(c.p * c.width), c.height);
            c.drawIn(1.f);
            glDisable(GL_SCISSOR_TEST);
        }
    }
};

class WipeRightTransition : public Transition {
public:
    void paint(const Context &c) const override {
        c.drawOut(1.f);
        if (c.p > 0.f) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(static_cast<GLint>((1.f - c.p) * c.width), 0,
                      static_cast<GLint>(c.p * c.width), c.height);
            c.drawIn(1.f);
            glDisable(GL_SCISSOR_TEST);
        }
    }
};

class WipeUpTransition : public Transition {
public:
    void paint(const Context &c) const override {
        c.drawOut(1.f);
        if (c.p > 0.f) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, c.width, static_cast<GLint>(c.p * c.height));
            c.drawIn(1.f);
            glDisable(GL_SCISSOR_TEST);
        }
    }
};

class WipeDownTransition : public Transition {
public:
    void paint(const Context &c) const override {
        c.drawOut(1.f);
        if (c.p > 0.f) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, static_cast<GLint>((1.f - c.p) * c.height),
                      c.width, static_cast<GLint>(c.p * c.height));
            c.drawIn(1.f);
            glDisable(GL_SCISSOR_TEST);
        }
    }
};

// ── Slides: outgoing translates out one edge, incoming pushes in the other ──

class SlideLeftTransition : public Transition {
public:
    void paint(const Context &c) const override {
        glPushMatrix(); glTranslatef(-(c.p * c.width), 0.f, 0.f);
        c.drawOut(1.f); glPopMatrix();
        glPushMatrix(); glTranslatef((1.f - c.p) * c.width, 0.f, 0.f);
        c.drawIn(1.f);  glPopMatrix();
    }
};

class SlideRightTransition : public Transition {
public:
    void paint(const Context &c) const override {
        glPushMatrix(); glTranslatef(c.p * c.width, 0.f, 0.f);
        c.drawOut(1.f); glPopMatrix();
        glPushMatrix(); glTranslatef(-(1.f - c.p) * c.width, 0.f, 0.f);
        c.drawIn(1.f);  glPopMatrix();
    }
};

class SlideUpTransition : public Transition {
public:
    void paint(const Context &c) const override {
        glPushMatrix(); glTranslatef(0.f, -(c.p * c.height), 0.f);
        c.drawOut(1.f); glPopMatrix();
        glPushMatrix(); glTranslatef(0.f, (1.f - c.p) * c.height, 0.f);
        c.drawIn(1.f);  glPopMatrix();
    }
};

class SlideDownTransition : public Transition {
public:
    void paint(const Context &c) const override {
        glPushMatrix(); glTranslatef(0.f, c.p * c.height, 0.f);
        c.drawOut(1.f); glPopMatrix();
        glPushMatrix(); glTranslatef(0.f, -(1.f - c.p) * c.height, 0.f);
        c.drawIn(1.f);  glPopMatrix();
    }
};

// Outgoing fades to a flat colour while incoming fades in. The flat colour
// (black/white) is the GL clear colour set by the widget before painting.
class DipTransition : public Transition {
public:
    void paint(const Context &c) const override {
        c.drawOut(std::max(0.f, 1.f - 2.f * c.p));
        c.drawIn(std::max(0.f, 2.f * c.p - 1.f));
    }
};

class AdditiveTransition : public Transition {
public:
    void paint(const Context &c) const override {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);               // additive
        c.drawOut(1.f - c.p);
        c.drawIn(c.p);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // restore standard
    }
};

class CrossZoomTransition : public Transition {
public:
    void paint(const Context &c) const override {
        const float alphaOut = 1.f - c.p;
        const float alphaIn  = c.p;
        if (alphaOut > 0.f) {
            glPushMatrix();
            zoomAboutCentre(c.width, c.height, 1.f + c.p * 1.5f); // zoom past camera
            c.drawOut(alphaOut);
            glPopMatrix();
        }
        if (alphaIn > 0.f) {
            glPushMatrix();
            zoomAboutCentre(c.width, c.height, 0.5f + c.p * 0.5f); // zoom in 0.5→1.0
            c.drawIn(alphaIn);
            glPopMatrix();
        }
    }
};

class SplitDoorTransition : public Transition {
public:
    void paint(const Context &c) const override {
        // Incoming zooms in from the centre as the outgoing doors slide apart.
        glPushMatrix();
        zoomAboutCentre(c.width, c.height, 0.5f + 0.5f * c.p);
        c.drawIn(1.f);
        glPopMatrix();

        if (c.p < 1.f) {
            glEnable(GL_SCISSOR_TEST);

            glScissor(0, 0, c.width / 2, c.height);
            glPushMatrix(); glTranslatef(-c.p * (c.width / 2.f), 0.f, 0.f);
            c.drawOut(1.f); glPopMatrix();

            glScissor(c.width / 2, 0, c.width - (c.width / 2), c.height);
            glPushMatrix(); glTranslatef(c.p * (c.width / 2.f), 0.f, 0.f);
            c.drawOut(1.f); glPopMatrix();

            glDisable(GL_SCISSOR_TEST);
        }
    }
};

class SplitDoorVertTransition : public Transition {
public:
    void paint(const Context &c) const override {
        glPushMatrix();
        zoomAboutCentre(c.width, c.height, 0.5f + 0.5f * c.p);
        c.drawIn(1.f);
        glPopMatrix();

        if (c.p < 1.f) {
            glEnable(GL_SCISSOR_TEST);

            glScissor(0, 0, c.width, c.height / 2);
            glPushMatrix(); glTranslatef(0.f, c.p * (c.height / 2.f), 0.f);
            c.drawOut(1.f); glPopMatrix();

            glScissor(0, c.height / 2, c.width, c.height - (c.height / 2));
            glPushMatrix(); glTranslatef(0.f, -c.p * (c.height / 2.f), 0.f);
            c.drawOut(1.f); glPopMatrix();

            glDisable(GL_SCISSOR_TEST);
        }
    }
};

class VortexSpinTransition : public Transition {
public:
    void paint(const Context &c) const override {
        const float alphaOut = 1.f - c.p;
        const float alphaIn  = c.p;
        if (alphaOut > 0.f) {
            glPushMatrix();
            glTranslatef(c.width / 2.f, c.height / 2.f, 0.f);
            glScalef(alphaOut, alphaOut, 1.f);
            glRotatef(c.p * 180.f, 0.f, 0.f, 1.f);
            glTranslatef(-c.width / 2.f, -c.height / 2.f, 0.f);
            c.drawOut(alphaOut);
            glPopMatrix();
        }
        if (alphaIn > 0.f) {
            glPushMatrix();
            glTranslatef(c.width / 2.f, c.height / 2.f, 0.f);
            glScalef(alphaIn, alphaIn, 1.f);
            glRotatef((c.p - 1.f) * 180.f, 0.f, 0.f, 1.f);
            glTranslatef(-c.width / 2.f, -c.height / 2.f, 0.f);
            c.drawIn(alphaIn);
            glPopMatrix();
        }
    }
};

class SplitQuadrantsTransition : public Transition {
public:
    void paint(const Context &c) const override {
        // Incoming zooms in from the centre as the outgoing quadrants clear out.
        glPushMatrix();
        zoomAboutCentre(c.width, c.height, 0.5f + 0.5f * c.p);
        c.drawIn(1.f);
        glPopMatrix();

        if (c.p < 1.f) {
            glEnable(GL_SCISSOR_TEST);
            const int w2 = c.width / 2;
            const int h2 = c.height / 2;

            glScissor(0, h2, w2, c.height - h2);                  // top-left
            glPushMatrix(); glTranslatef(-c.p * w2, -c.p * h2, 0.f);
            c.drawOut(1.f); glPopMatrix();

            glScissor(w2, h2, c.width - w2, c.height - h2);        // top-right
            glPushMatrix(); glTranslatef(c.p * w2, -c.p * h2, 0.f);
            c.drawOut(1.f); glPopMatrix();

            glScissor(0, 0, w2, h2);                              // bottom-left
            glPushMatrix(); glTranslatef(-c.p * w2, c.p * h2, 0.f);
            c.drawOut(1.f); glPopMatrix();

            glScissor(w2, 0, c.width - w2, h2);                   // bottom-right
            glPushMatrix(); glTranslatef(c.p * w2, c.p * h2, 0.f);
            c.drawOut(1.f); glPopMatrix();

            glDisable(GL_SCISSOR_TEST);
        }
    }
};

} // namespace

const Transition &Transition::forMode(TransitionMode mode) {
    static const CrossfadeTransition      crossfade;
    static const CutTransition            cut;
    static const WipeLeftTransition       wipeLeft;
    static const WipeRightTransition      wipeRight;
    static const WipeUpTransition         wipeUp;
    static const WipeDownTransition       wipeDown;
    static const SlideLeftTransition      slideLeft;
    static const SlideRightTransition     slideRight;
    static const SlideUpTransition        slideUp;
    static const SlideDownTransition      slideDown;
    static const DipTransition            dip;
    static const AdditiveTransition       additive;
    static const CrossZoomTransition      crossZoom;
    static const SplitDoorTransition      splitDoor;
    static const SplitDoorVertTransition  splitDoorVert;
    static const VortexSpinTransition     vortexSpin;
    static const SplitQuadrantsTransition splitQuadrants;

    switch (mode) {
    case TransitionMode::Crossfade:      return crossfade;
    case TransitionMode::Cut:            return cut;
    case TransitionMode::WipeLeft:       return wipeLeft;
    case TransitionMode::WipeRight:      return wipeRight;
    case TransitionMode::WipeUp:         return wipeUp;
    case TransitionMode::WipeDown:       return wipeDown;
    case TransitionMode::SlideLeft:      return slideLeft;
    case TransitionMode::SlideRight:     return slideRight;
    case TransitionMode::SlideUp:        return slideUp;
    case TransitionMode::SlideDown:      return slideDown;
    case TransitionMode::DipToBlack:     return dip;
    case TransitionMode::DipToWhite:     return dip;
    case TransitionMode::Additive:       return additive;
    case TransitionMode::CrossZoom:      return crossZoom;
    case TransitionMode::SplitDoor:      return splitDoor;
    case TransitionMode::SplitDoorVert:  return splitDoorVert;
    case TransitionMode::VortexSpin:     return vortexSpin;
    case TransitionMode::SplitQuadrants: return splitQuadrants;
    }
    return crossfade;
}
