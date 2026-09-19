/*
 * FortyFifthMidi - nested Circle of Fifths UI.
 *
 * Two concentric rings (outer majors, inner relative minors) with a third ring
 * reserved for diminished. Only the underlying theory is shared with printed chord
 * wheels - that theory is public domain, and every visual choice here (palette,
 * segment geometry, typography, proportions) is original. Do not trace artwork
 * from any commercial wheel. See docs/spec.md.
 *
 * v1 scope: layout, hit-testing and visual feedback. The drag-to-glide gesture is
 * wired through onMotion but the ramp itself lives in the plugin (run()), since
 * only the audio thread can place events at sample offsets.
 */

#include "DistrhoUI.hpp"
#include "CircleTheory.hpp"

#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

using namespace fortyfifth;

class FortyFifthUI : public UI
{
public:
    FortyFifthUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
    }

protected:
    void onNanoDisplay() override
    {
        const float w  = getWidth();
        const float h  = getHeight();
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;
        const float outer = (w < h ? w : h) * 0.46f;

        /* Ring radii: majors outermost, minors inside them, innermost reserved. */
        const float rMajorOut = outer;
        const float rMajorIn  = outer * 0.70f;
        const float rMinorOut = rMajorIn;
        const float rMinorIn  = outer * 0.44f;

        beginPath();
        rect(0, 0, w, h);
        fillColor(Color(0.09f, 0.10f, 0.13f));
        fill();

        for (int i = 0; i < 12; ++i) {
            drawSegment(cx, cy, rMajorIn, rMajorOut, i, kRingMajor);
            drawSegment(cx, cy, rMinorIn, rMinorOut, i, kRingMinor);
        }

        drawCenterReadout(cx, cy, rMinorIn);
    }

    /* Wedge for one wheel position on one ring, plus its label. */
    void drawSegment(float cx, float cy, float rIn, float rOut, int index, Ring ring)
    {
        const float step  = 2.0f * static_cast<float>(M_PI) / 12.0f;
        /* Rotate so position 0 (C) sits at twelve o'clock. */
        const float start = index * step - static_cast<float>(M_PI) * 0.5f - step * 0.5f;
        const float end   = start + step;

        const bool active = (fActivePosition == index && fActiveRing == ring);

        beginPath();
        arc(cx, cy, rOut, start, end, NanoVG::CW);
        arc(cx, cy, rIn, end, start, NanoVG::CCW);
        closePath();

        if (active) {
            fillColor(ring == kRingMajor ? Color(0.98f, 0.72f, 0.24f)
                                         : Color(0.42f, 0.78f, 0.95f));
        } else {
            fillColor(ring == kRingMajor ? Color(0.20f, 0.22f, 0.28f)
                                         : Color(0.15f, 0.17f, 0.22f));
        }
        fill();

        strokeColor(Color(0.07f, 0.08f, 0.10f));
        strokeWidth(2.0f);
        stroke();

        const float mid  = (start + end) * 0.5f;
        const float rMid = (rIn + rOut) * 0.5f;
        const float tx   = cx + std::cos(mid) * rMid;
        const float ty   = cy + std::sin(mid) * rMid;

        fontSize(ring == kRingMajor ? 22.0f : 17.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(active ? Color(0.08f, 0.09f, 0.12f) : Color(0.85f, 0.88f, 0.92f));
        text(tx, ty, labelForPosition(index, ring), nullptr);
    }

    void drawCenterReadout(float cx, float cy, float radius)
    {
        beginPath();
        circle(cx, cy, radius);
        fillColor(Color(0.12f, 0.13f, 0.17f));
        fill();

        if (fActivePosition < 0)
            return;

        fontSize(30.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(0.92f, 0.94f, 0.97f));
        text(cx, cy, labelForPosition(fActivePosition, fActiveRing), nullptr);
    }

    /* ---- input ------------------------------------------------------------ */

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.press) {
            Ring ring;
            const int pos = hitTest(ev.pos.getX(), ev.pos.getY(), ring);
            if (pos < 0)
                return false;

            fActivePosition = pos;
            fActiveRing     = ring;
            fDragging       = true;
            repaint();
            return true;
        }

        if (fDragging) {
            fDragging = false;
            repaint();
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (! fDragging)
            return false;

        Ring ring;
        const int pos = hitTest(ev.pos.getX(), ev.pos.getY(), ring);
        if (pos < 0 || (pos == fActivePosition && ring == fActiveRing))
            return false;

        /* Crossing into a new position mid-drag is the glide gesture. */
        fActivePosition = pos;
        fActiveRing     = ring;
        repaint();
        return true;
    }

    /* Returns the wheel position under a point, or -1 outside every ring. */
    int hitTest(double px, double py, Ring& outRing) const
    {
        const float w  = getWidth();
        const float h  = getHeight();
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;
        const float outer = (w < h ? w : h) * 0.46f;

        const double dx = px - cx;
        const double dy = py - cy;
        const double dist = std::sqrt(dx * dx + dy * dy);

        if (dist > outer || dist < outer * 0.44f)
            return -1;

        outRing = (dist >= outer * 0.70f) ? kRingMajor : kRingMinor;

        /* Undo the twelve-o'clock rotation to recover the index. */
        double angle = std::atan2(dy, dx) + M_PI * 0.5;
        while (angle < 0)            angle += 2.0 * M_PI;
        while (angle >= 2.0 * M_PI)  angle -= 2.0 * M_PI;

        const double step = 2.0 * M_PI / 12.0;
        return static_cast<int>((angle + step * 0.5) / step) % 12;
    }

    void parameterChanged(uint32_t, float) override {}

private:
    int  fActivePosition = -1;
    Ring fActiveRing     = kRingMajor;
    bool fDragging       = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FortyFifthUI)
};

UI* createUI()
{
    return new FortyFifthUI();
}

END_NAMESPACE_DISTRHO
