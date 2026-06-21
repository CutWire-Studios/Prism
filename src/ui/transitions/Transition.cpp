#include "ui/transitions/Transition.h"
#include <QOpenGLFunctions>   // pulls in the desktop GL fixed-function symbols
#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;

// Translate to the frame centre, scale, and translate back, so a wrapped draw
// call is scaled about the middle of the output.
void zoomAboutCentre(int w, int h, float scale) {
    glTranslatef(w / 2.f, h / 2.f, 0.f);
    glScalef(scale, scale, 1.f);
    glTranslatef(-w / 2.f, -h / 2.f, 0.f);
}

// Sets up a 45° perspective projection and a fresh modelview, saving the
// previous matrices, and enables depth testing. Pair with end3D().
void begin3D(float aspect) {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    const float fH = std::tan(22.5f / 180.f * kPi);
    const float fW = fH * aspect;
    glFrustum(-fW, fW, -fH, fH, 1.f, 100.f);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
}

void end3D() {
    glDisable(GL_DEPTH_TEST);
    glPopMatrix();                 // modelview
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();                 // projection
    glMatrixMode(GL_MODELVIEW);
}

// Draws a deck's full texture as a 3D quad spanning [-aspect, aspect] × [-1, 1].
void draw3DDeck(GLuint tex, bool ready, float alpha, float aspect, bool texFlipped) {
    if (!tex || !ready || alpha <= 0.f) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1.f, 1.f, 1.f, alpha);
    glBegin(GL_QUADS);
    if (texFlipped) {
        glTexCoord2f(0.f, 0.f); glVertex3f(-aspect, -1.f, 0.f);
        glTexCoord2f(1.f, 0.f); glVertex3f( aspect, -1.f, 0.f);
        glTexCoord2f(1.f, 1.f); glVertex3f( aspect,  1.f, 0.f);
        glTexCoord2f(0.f, 1.f); glVertex3f(-aspect,  1.f, 0.f);
    } else {
        glTexCoord2f(0.f, 1.f); glVertex3f(-aspect, -1.f, 0.f);
        glTexCoord2f(1.f, 1.f); glVertex3f( aspect, -1.f, 0.f);
        glTexCoord2f(1.f, 0.f); glVertex3f( aspect,  1.f, 0.f);
        glTexCoord2f(0.f, 0.f); glVertex3f(-aspect,  1.f, 0.f);
    }
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
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

// ── 3D transitions ──────────────────────────────────────────────────────────
// These composite the full deck textures themselves under a perspective
// projection, so they read the raw A/B textures from the context rather than
// using the direction-aware drawOut/drawIn callbacks.

class Gallery3DTransition : public Transition {
public:
    void paint(const Context &c) const override {
        const float t       = c.t;
        const float alphaA  = 1.f - t;
        const float alphaB  = t;
        const float aspect  = (float)c.width / c.height;
        const bool  flip    = c.texFlipped;

        begin3D(aspect);

        // Eased fader value for camera position.
        const float ease = t * t * (3.f - 2.f * t);

        // 3D layout for deck B (right, slightly lower, angled and further back).
        const float XB = 2.4f * aspect, YB = -0.3f, ZB = -1.8f, rotB = -15.f;

        const float d = 2.41421356f;
        const float rotB_rad = rotB * kPi / 180.f;
        const float X1 = XB + d * std::sin(rotB_rad);
        const float Y1 = YB;
        const float Z1 = ZB + d * std::cos(rotB_rad);

        const float Cx = ease * X1;
        const float Cy = ease * Y1;
        const float Cz = (1.f - ease) * d + ease * Z1 + std::sin(t * kPi) * 1.0f;
        const float CrotY = ease * rotB;
        const float CrotX = std::sin(t * kPi) * 4.f;

        glRotatef(-CrotX, 1.f, 0.f, 0.f);
        glRotatef(-CrotY, 0.f, 1.f, 0.f);
        glTranslatef(-Cx, -Cy, -Cz);

        // 1. Styled studio gallery wall background (Z = -20).
        glPushMatrix();
        glTranslatef(XB * 0.5f, 0.f, -20.f);
        glScalef(15.f * aspect, 15.f, 1.f);
        glDisable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        glColor4f(0.18f, 0.14f, 0.08f, 1.0f); glVertex3f(-1.f,  1.f, 0.f);
        glColor4f(0.04f, 0.03f, 0.02f, 1.0f); glVertex3f( 1.f,  1.f, 0.f);
        glColor4f(0.01f, 0.01f, 0.01f, 1.0f); glVertex3f( 1.f, -1.f, 0.f);
        glColor4f(0.04f, 0.03f, 0.02f, 1.0f); glVertex3f(-1.f, -1.f, 0.f);
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glPopMatrix();

        // 2. Ambient video glow textures in the background (Z = -18).
        if (alphaA > 0.f && c.readyA) {
            glPushMatrix();
            glTranslatef(0.f, 0.f, -18.f);
            glScalef(12.f * aspect, 12.f, 1.f);
            glBindTexture(GL_TEXTURE_2D, c.texA);
            glColor4f(0.35f, 0.35f, 0.35f, alphaA * 0.18f);
            glBegin(GL_QUADS);
            if (flip) {
                glTexCoord2f(0.f, 0.f); glVertex3f(-1.f, -1.f, 0.f);
                glTexCoord2f(1.f, 0.f); glVertex3f( 1.f, -1.f, 0.f);
                glTexCoord2f(1.f, 1.f); glVertex3f( 1.f,  1.f, 0.f);
                glTexCoord2f(0.f, 1.f); glVertex3f(-1.f,  1.f, 0.f);
            } else {
                glTexCoord2f(0.f, 1.f); glVertex3f(-1.f, -1.f, 0.f);
                glTexCoord2f(1.f, 1.f); glVertex3f( 1.f, -1.f, 0.f);
                glTexCoord2f(1.f, 0.f); glVertex3f( 1.f,  1.f, 0.f);
                glTexCoord2f(0.f, 0.f); glVertex3f(-1.f,  1.f, 0.f);
            }
            glEnd();
            glPopMatrix();
        }
        if (alphaB > 0.f && c.readyB) {
            glPushMatrix();
            glTranslatef(XB, YB, -18.f);
            glScalef(12.f * aspect, 12.f, 1.f);
            glBindTexture(GL_TEXTURE_2D, c.texB);
            glColor4f(0.35f, 0.35f, 0.35f, alphaB * 0.18f);
            glBegin(GL_QUADS);
            if (flip) {
                glTexCoord2f(0.f, 0.f); glVertex3f(-1.f, -1.f, 0.f);
                glTexCoord2f(1.f, 0.f); glVertex3f( 1.f, -1.f, 0.f);
                glTexCoord2f(1.f, 1.f); glVertex3f( 1.f,  1.f, 0.f);
                glTexCoord2f(0.f, 1.f); glVertex3f(-1.f,  1.f, 0.f);
            } else {
                glTexCoord2f(0.f, 1.f); glVertex3f(-1.f, -1.f, 0.f);
                glTexCoord2f(1.f, 1.f); glVertex3f( 1.f, -1.f, 0.f);
                glTexCoord2f(1.f, 0.f); glVertex3f( 1.f,  1.f, 0.f);
                glTexCoord2f(0.f, 0.f); glVertex3f(-1.f,  1.f, 0.f);
            }
            glEnd();
            glPopMatrix();
        }

        // Draws a framed gallery picture (drop shadow, golden moldings, shine).
        auto drawGalleryDeck = [&](GLuint tex, bool ready, float alpha) {
            if (!tex || !ready || alpha <= 0.f) return;

            glDisable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            const float d0 = 0.006f, d1 = 0.016f, d2 = 0.040f, d3 = 0.052f, d4 = 0.068f;

            // Drop shadow.
            for (int i = 4; i > 0; --i) {
                float shadowOffset = i * 0.012f;
                float shadowAlpha = 0.22f / i * alpha;
                glColor4f(0.0f, 0.0f, 0.0f, shadowAlpha);
                float sx_min = -aspect - d4 - shadowOffset + 0.01f;
                float sx_max =  aspect + d4 + shadowOffset + 0.01f;
                float sy_min = -1.f - d4 - shadowOffset - 0.01f;
                float sy_max =  1.f + d4 + shadowOffset - 0.01f;
                glBegin(GL_QUADS);
                glVertex3f(sx_min, sy_min, -0.025f);
                glVertex3f(sx_max, sy_min, -0.025f);
                glVertex3f(sx_max, sy_max, -0.025f);
                glVertex3f(sx_min, sy_max, -0.025f);
                glEnd();
            }

            auto frameTier = [&](float a, float b,
                                 float r_lt, float g_lt, float b_lt,
                                 float r_rb, float g_rb, float b_rb,
                                 float z) {
                glBegin(GL_QUADS);
                glColor4f(r_lt, g_lt, b_lt, alpha);
                glVertex3f(-aspect - b, -1.f - b, z);
                glVertex3f(-aspect - a, -1.f - a, z);
                glVertex3f(-aspect - a,  1.f + a, z);
                glVertex3f(-aspect - b,  1.f + b, z);
                glEnd();
                glBegin(GL_QUADS);
                glColor4f(r_rb, g_rb, b_rb, alpha);
                glVertex3f(aspect + a, -1.f - a, z);
                glVertex3f(aspect + b, -1.f - b, z);
                glVertex3f(aspect + b,  1.f + b, z);
                glVertex3f(aspect + a,  1.f + a, z);
                glEnd();
                glBegin(GL_QUADS);
                glColor4f(r_lt, g_lt, b_lt, alpha);
                glVertex3f(-aspect - b, -1.f - b, z);
                glVertex3f( aspect + b, -1.f - b, z);
                glVertex3f( aspect + a, -1.f - a, z);
                glVertex3f(-aspect - a, -1.f - a, z);
                glEnd();
                glBegin(GL_QUADS);
                glColor4f(r_rb, g_rb, b_rb, alpha);
                glVertex3f(-aspect - a,  1.f + a, z);
                glVertex3f( aspect + a,  1.f + a, z);
                glVertex3f( aspect + b,  1.f + b, z);
                glVertex3f(-aspect - b,  1.f + b, z);
                glEnd();
            };

            frameTier(0.f, d0, 0.08f, 0.06f, 0.04f, 0.04f, 0.03f, 0.02f, -0.002f);
            frameTier(d0, d1, 0.95f, 0.82f, 0.35f, 0.55f, 0.40f, 0.12f, -0.005f);
            frameTier(d1, d2, 0.45f, 0.32f, 0.15f, 0.28f, 0.18f, 0.08f, -0.010f);
            frameTier(d2, d3, 1.00f, 0.92f, 0.50f, 0.65f, 0.48f, 0.08f, -0.015f);
            frameTier(d3, d4, 0.90f, 0.75f, 0.25f, 0.50f, 0.35f, 0.05f, -0.020f);

            // Specular shine sweep.
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            float shinePos = -aspect * 1.5f + t * (aspect * 3.0f);
            float sw = aspect * 0.25f;
            glColor4f(1.0f, 0.97f, 0.85f, 0.55f * alpha);
            glBegin(GL_QUADS);
            glVertex3f(shinePos - sw, -1.f - d4, -0.018f);
            glVertex3f(shinePos + sw, -1.f - d4, -0.018f);
            glVertex3f(shinePos + sw + 0.4f, 1.f + d4, -0.018f);
            glVertex3f(shinePos - sw + 0.4f, 1.f + d4, -0.018f);
            glEnd();
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // Video quad.
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex);
            glColor4f(1.f, 1.f, 1.f, alpha);
            glBegin(GL_QUADS);
            if (flip) {
                glTexCoord2f(0.f, 0.f); glVertex3f(-aspect, -1.f, 0.f);
                glTexCoord2f(1.f, 0.f); glVertex3f( aspect, -1.f, 0.f);
                glTexCoord2f(1.f, 1.f); glVertex3f( aspect,  1.f, 0.f);
                glTexCoord2f(0.f, 1.f); glVertex3f(-aspect,  1.f, 0.f);
            } else {
                glTexCoord2f(0.f, 1.f); glVertex3f(-aspect, -1.f, 0.f);
                glTexCoord2f(1.f, 1.f); glVertex3f( aspect, -1.f, 0.f);
                glTexCoord2f(1.f, 0.f); glVertex3f( aspect,  1.f, 0.f);
                glTexCoord2f(0.f, 0.f); glVertex3f(-aspect,  1.f, 0.f);
            }
            glEnd();
            glBindTexture(GL_TEXTURE_2D, 0);
        };

        if (alphaA > 0.f) {
            glPushMatrix();
            drawGalleryDeck(c.texA, c.readyA, alphaA);
            glPopMatrix();
        }
        if (alphaB > 0.f) {
            glPushMatrix();
            glTranslatef(XB, YB, ZB);
            glRotatef(rotB, 0.f, 1.f, 0.f);
            drawGalleryDeck(c.texB, c.readyB, alphaB);
            glPopMatrix();
        }

        end3D();

        // Sweeping golden light-leak overlay in screen space.
        float shineAlpha = 0.6f * std::sin(t * kPi);
        if (shineAlpha > 0.f) {
            float w = c.width, h = c.height;
            float tilt = 120.f, bandWidth = 280.f;
            float center = -bandWidth + t * (w + bandWidth * 2.f);

            glDisable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glBegin(GL_QUADS);
            glColor4f(1.f, 0.92f, 0.82f, 0.f);          glVertex2f(center - bandWidth / 2.f, 0.f);
            glColor4f(1.f, 0.92f, 0.82f, shineAlpha);    glVertex2f(center, 0.f);
            glColor4f(1.f, 0.92f, 0.82f, shineAlpha);    glVertex2f(center + tilt, h);
            glColor4f(1.f, 0.92f, 0.82f, 0.f);          glVertex2f(center + tilt - bandWidth / 2.f, h);
            glColor4f(1.f, 0.92f, 0.82f, shineAlpha);    glVertex2f(center, 0.f);
            glColor4f(1.f, 0.92f, 0.82f, 0.f);          glVertex2f(center + bandWidth / 2.f, 0.f);
            glColor4f(1.f, 0.92f, 0.82f, 0.f);          glVertex2f(center + tilt + bandWidth / 2.f, h);
            glColor4f(1.f, 0.92f, 0.82f, shineAlpha);    glVertex2f(center + tilt, h);
            glEnd();
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_TEXTURE_2D);
        }
    }
};

class Cube3DTransition : public Transition {
public:
    void paint(const Context &c) const override {
        const float t      = c.t;
        const float alphaA = 1.f - t;
        const float alphaB = t;
        const float aspect = (float)c.width / c.height;

        begin3D(aspect);
        // Z translation matches the FOV so that scale = 1 fills the viewport.
        glTranslatef(0.f, 0.f, -(2.41421356f + aspect));

        if (alphaA > 0.f) {                       // face A rotates out to the left
            glPushMatrix();
            glRotatef(-t * 90.f, 0.f, 1.f, 0.f);
            glTranslatef(0.f, 0.f, aspect);
            draw3DDeck(c.texA, c.readyA, alphaA, aspect, c.texFlipped);
            glPopMatrix();
        }
        if (alphaB > 0.f) {                       // face B rotates in from the right
            glPushMatrix();
            glRotatef(90.f - t * 90.f, 0.f, 1.f, 0.f);
            glTranslatef(0.f, 0.f, aspect);
            draw3DDeck(c.texB, c.readyB, alphaB, aspect, c.texFlipped);
            glPopMatrix();
        }

        end3D();
    }
};

class Flip3DTransition : public Transition {
public:
    void paint(const Context &c) const override {
        const float t      = c.t;
        const float alphaA = 1.f - t;
        const float alphaB = t;
        const float aspect = (float)c.width / c.height;

        begin3D(aspect);
        glTranslatef(0.f, 0.f, -2.41421356f);     // card at Z = 0 locally

        glPushMatrix();
        glRotatef(-t * 180.f, 0.f, 1.f, 0.f);     // flip the whole card
        if (t < 0.5f) {
            draw3DDeck(c.texA, c.readyA, alphaA, aspect, c.texFlipped);
        } else {
            glRotatef(180.f, 0.f, 1.f, 0.f);      // face the back card's texture forward
            draw3DDeck(c.texB, c.readyB, alphaB, aspect, c.texFlipped);
        }
        glPopMatrix();

        end3D();
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
    static const Gallery3DTransition      gallery3D;
    static const Cube3DTransition         cube3D;
    static const Flip3DTransition         flip3D;

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
    case TransitionMode::Gallery3D:      return gallery3D;
    case TransitionMode::Cube3D:         return cube3D;
    case TransitionMode::Flip3D:         return flip3D;
    }
    return crossfade;
}
