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
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>

START_NAMESPACE_DISTRHO

using namespace fortyfifth;

class FortyFifthUI : public UI
{
    static constexpr size_t kLogLines = 14;
    /* Always-visible header bar for the collapsible monitor. */
    static constexpr float  kHeaderH  = 22.0f;

public:
    FortyFifthUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        /* Without a font every text() call silently draws nothing - no labels, no
         * buttons, no event log, with no error anywhere.
         *
         * Note the font name: loadSharedResources() registers DPF's bundled
         * DejaVu under NANOVG_DEJAVU_SANS_TTF ("__dpf_dejavusans_ttf__"), NOT
         * under "sans". Selecting a name that was never registered fails
         * silently, which is exactly how this was missed the first time. */
        if (! loadSharedResources())
            createFontFromFile(NANOVG_DEJAVU_SANS_TTF,
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    }

protected:
    void onNanoDisplay() override
    {
        const float w  = getWidth();
        const float h  = getHeight();
        const float cx = wheelCentreX();
        const float cy = wheelCentreY();
        const float outer = wheelRadius();

        beginPath();
        rect(0, 0, w, h);
        fillColor(Color(0.09f, 0.10f, 0.13f));
        fill();

        /* Outermost first so inner rings overlay their borders cleanly. */
        for (int ring = kRingCount - 1; ring >= 0; --ring) {
            const Ring  r    = static_cast<Ring>(ring);
            const float rIn  = ringInnerRadius(r, outer);
            const float rOut = ringOuterRadius(r, outer);

            for (int i = 0; i < segmentsInRing(r); ++i)
                drawSegment(cx, cy, rIn, rOut, i, r);
        }

        drawCenterReadout(cx, cy, ringInnerRadius(kRingKey, outer));
        drawOctaveSlider();
        drawControls();

        drawMonitor();

        /* Last, so the open list overlays the wheel and the log. */
        drawOpenMenu();
    }

    /* Angular span of one cell on one ring. */
    void segmentAngles(int index, Ring ring, float& start, float& end) const
    {
        const int   n    = segmentsInRing(ring);
        const float step = 2.0f * static_cast<float>(M_PI) / n;
        const float half = static_cast<float>(M_PI) * 0.5f;

        /*
         * Cell 0 of every ring is anchored at twelve o'clock. fMinorOffsetCells
         * shifts the 24-cell minor ring relative to that, in units of its own
         * cell width:
         *
         *   0.0  Em centred on C - the rings share a centre line.
         *   0.5  Em's edge on C  - each minor straddles a key boundary, so the
         *        rings interlock.
         *
         * Press 'o' in the test rig to flip between them.
         */
        start = index * step - half - step * 0.5f;
        if (ring == kRingMinor)
            start += step * fMinorOffsetCells;

        end = start + step;
    }

    void drawSegment(float cx, float cy, float rIn, float rOut, int index, Ring ring)
    {
        float start, end;
        segmentAngles(index, ring, start, end);

        const bool active = (fActivePosition == index && fActiveRing == ring);

        beginPath();
        arc(cx, cy, rOut, start, end, NanoVG::CW);
        arc(cx, cy, rIn, end, start, NanoVG::CCW);
        closePath();

        /* Each ring gets its own hue so the three are distinguishable at a
         * glance; the diminished ring is dimmest, matching its lighter use. */
        /* A cell belonging to the selected key's wedge is lifted, so the seven
         * diatonic chords read as one shape - the whole point of the wheel. */
        const CellRole role    = roleInKey(index, ring, fSelectedKey);
        const bool     inWedge = (role != kCellOutside);

        if (active) {
            switch (ring) {
                case kRingKey:   fillColor(Color(0.98f, 0.72f, 0.24f)); break;
                case kRingMinor: fillColor(Color(0.42f, 0.78f, 0.95f)); break;
                default:         fillColor(Color(0.72f, 0.56f, 0.90f)); break;
            }
        } else if (role == kCellSecondary) {
            /* Borrowed II and III: present but visibly outside the core seven,
             * so the diatonic wedge still reads as one shape. */
            fillColor(Color(0.33f, 0.26f, 0.20f));
        } else if (role == kCellDiatonic) {
            switch (ring) {
                case kRingKey:   fillColor(Color(0.30f, 0.33f, 0.42f)); break;
                case kRingMinor: fillColor(Color(0.24f, 0.29f, 0.38f)); break;
                default:         fillColor(Color(0.20f, 0.22f, 0.30f)); break;
            }
        } else {
            switch (ring) {
                case kRingKey:   fillColor(Color(0.17f, 0.18f, 0.23f)); break;
                case kRingMinor: fillColor(Color(0.14f, 0.15f, 0.20f)); break;
                default:         fillColor(Color(0.11f, 0.12f, 0.16f)); break;
            }
        }
        fill();

        strokeColor(Color(0.07f, 0.08f, 0.10f));
        strokeWidth(1.5f);
        stroke();

        const float mid  = (start + end) * 0.5f;
        const float rMid = (rIn + rOut) * 0.5f;
        const float tx   = cx + std::cos(mid) * rMid;
        const float ty   = cy + std::sin(mid) * rMid;

        float size;
        switch (ring) {
            case kRingKey:   size = 19.0f; break;
            case kRingMinor: size = 13.0f; break;  /* 24 cells: less room */
            default:         size = 12.0f; break;
        }

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(size);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        if (active)
            fillColor(Color(0.08f, 0.09f, 0.12f));
        else if (role == kCellSecondary)
            fillColor(Color(0.92f, 0.82f, 0.66f));
        else if (role == kCellDiatonic)
            fillColor(Color(0.94f, 0.96f, 0.99f));
        else
            fillColor(Color(0.52f, 0.56f, 0.64f));

        /* Nudge up so the numeral below has room. */
        const float ty0 = inWedge ? ty - size * 0.30f : ty;
        text(tx, ty0, labelForPosition(index, ring), nullptr);

        /* Roman numeral under the chord name, so the progression can be read
         * off the wheel directly. */
        if (inWedge) {
            const char* deg = degreeInKey(index, ring, fSelectedKey);
            fontSize(size * 0.68f);
            if (active)
                fillColor(Color(0.15f, 0.16f, 0.20f));
            else if (role == kCellSecondary)
                fillColor(Color(0.88f, 0.68f, 0.42f));
            else
                fillColor(Color(0.55f, 0.78f, 0.98f));
            text(tx, ty0 + size * 0.80f, deg, nullptr);
        }
    }

    void drawCenterReadout(float cx, float cy, float radius)
    {
        beginPath();
        circle(cx, cy, radius);
        fillColor(Color(0.12f, 0.13f, 0.17f));
        fill();

        if (fActivePosition < 0)
            return;

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(30.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(0.92f, 0.94f, 0.97f));
        text(cx, cy, labelForPosition(fActivePosition, fActiveRing), nullptr);
    }

    /* ---- input ------------------------------------------------------------ */

    /*
     * Gestures travel to the DSP side through the "gesture" state key as
     * "<verb>:<position>:<ring>". Sending the gesture rather than finished notes
     * is deliberate: the DSP owns chord construction, so it reads the current
     * settings at the instant the gesture arrives and bakes concrete MIDI from
     * them (spec section 5). sendNote() cannot express a chord or a glide.
     */
    void sendGesture(const char* verb, int position, Ring ring)
    {
        char value[32];
        std::snprintf(value, sizeof(value), "%s:%d:%d",
                      verb, position, static_cast<int>(ring));
        setState("gesture", value);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.press) {
            if (handleControlClick(ev.pos.getX(), ev.pos.getY()))
                return true;

            Ring ring;
            const int pos = hitTest(ev.pos.getX(), ev.pos.getY(), ring);
            if (pos < 0)
                return false;

            /* Clicking the key ring re-centres the highlighted wedge, so the
             * diatonic set follows the key you are playing in - unless the key
             * is locked, in which case the chord still sounds but the reference
             * wedge stays put. */
            if (ring == kRingKey && ! fKeyLocked) {
                fSelectedKey = pos;
                /* The DSP needs the key too now: an incoming MIDI note selects
                 * a cell by degree, so it cannot be resolved without it. */
                char kb[16];
                std::snprintf(kb, sizeof(kb), "%d", fSelectedKey);
                setState("selectedKey", kb);
            }

            /* In latch mode a press on the lit selection turns it off, matching
             * the DSP's toggle. */
            const bool same = (fLatchEnabled &&
                               pos == fActivePosition && ring == fActiveRing);

            sendGesture("press", pos, ring);

            if (same) {
                fActivePosition = -1;
            } else {
                fActivePosition = pos;
                fActiveRing     = ring;
            }
            fDragging = true;
            repaint();
            return true;
        }

        /* A slider drag ends without touching the wheel's gesture state. */
        if (fSliderDrag) {
            fSliderDrag = false;
            return true;
        }

        if (fDragging) {
            fDragging = false;
            /* Position 0 rather than fActivePosition: a latch toggle clears the
             * active cell to -1, which the DSP rejects as out of range - and a
             * dropped release is a stuck note. Release stops everything
             * regardless of which cell it names, so any valid value serves. */
            sendGesture("release",
                        fActivePosition < 0 ? 0 : fActivePosition, fActiveRing);

            /* Latched selections stay lit because they are still sounding;
             * momentary ones must go dark, or the highlight lies about what is
             * audible. */
            if (! fLatchEnabled)
                fActivePosition = -1;

            repaint();
            return true;
        }
        return false;
    }

    /* 'o' flips the minor ring between aligned and interlocked, so the layout
     * can be judged on screen instead of guessed at. */
    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (! ev.press)
            return false;

        if (ev.key == 'o' || ev.key == 'O') {
            fMinorOffsetCells = (fMinorOffsetCells == 0.0f) ? 0.5f : 0.0f;
            repaint();
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        /* Dragging the octave slider transposes whatever is sounding. The DSP
         * rebuilds held groups at the new octave, so the chord travels. */
        if (fSliderDrag) {
            setOctave(octaveAtY(ev.pos.getY()));
            return true;
        }

        if (! fDragging)
            return false;

        Ring ring;
        const int pos = hitTest(ev.pos.getX(), ev.pos.getY(), ring);
        if (pos < 0 || (pos == fActivePosition && ring == fActiveRing))
            return false;

        /* Crossing into a new position mid-drag is the glide gesture. */
        fActivePosition = pos;
        fActiveRing     = ring;
        sendGesture("move", pos, ring);
        repaint();
        return true;
    }

    /* ---- ring geometry ----------------------------------------------------
     *
     * One definition, used by both drawing and hit-testing, so a wedge can never
     * be drawn somewhere its hit box is not. Bands are proportional to
     * kRingWeight, and the hub keeps a fixed share of the radius.
     */

    static constexpr float kHubFraction = 0.34f;

    /* Cumulative weight below a ring, and the total, as fractions of the band
     * area available outside the hub. */
    static float ringWeightBelow(Ring ring)
    {
        float sum = 0.0f;
        for (int i = 0; i < static_cast<int>(ring); ++i)
            sum += kRingWeight[i];
        return sum;
    }

    static float ringWeightTotal()
    {
        float sum = 0.0f;
        for (int i = 0; i < kRingCount; ++i)
            sum += kRingWeight[i];
        return sum;
    }

    static float ringInnerRadius(Ring ring, float outer)
    {
        const float hub  = outer * kHubFraction;
        const float band = outer - hub;
        return hub + band * (ringWeightBelow(ring) / ringWeightTotal());
    }

    static float ringOuterRadius(Ring ring, float outer)
    {
        const float hub  = outer * kHubFraction;
        const float band = outer - hub;
        return hub + band * ((ringWeightBelow(ring) + kRingWeight[ring])
                             / ringWeightTotal());
    }

    /* ---- octave slider -----------------------------------------------------
     *
     * Vertical, down the left edge, so on a touch screen it falls under the
     * left hand while the right plays the wheel - and so it can be DRAGGED
     * while notes sound, which is what makes it an instrument control rather
     * than a preference. Dragging it glides the sounding chord through the
     * octaves, using the same glide machinery as movement between cells.
     */
    static constexpr float kSliderW     = 46.0f;
    static constexpr float kOctaveMin   = 1.0f;
    static constexpr float kOctaveMax   = 7.0f;

    /* The wheel is centred in whatever the chrome leaves behind, so nothing
     * overlaps. Two control rows above; the monitor below, which is just its
     * header bar until expanded; the octave slider down the left. */
    float chromeTop() const { return kDropY + kDropH + 10.0f; }

    /* Horizontal space the slider claims, so the wheel never sits under it. */
    float chromeLeft() const { return kSliderW + 16.0f; }

    float chromeBottom() const
    {
        return kHeaderH + (fMonitorOpen ? kLogLines * 14.0f + 10.0f : 0.0f);
    }

    /* Centred in the space left of the slider, not of the window. */
    float wheelCentreX() const
    {
        return chromeLeft() + (getWidth() - chromeLeft()) * 0.5f;
    }

    float wheelCentreY() const
    {
        return chromeTop() + (getHeight() - chromeTop() - chromeBottom()) * 0.5f;
    }

    float wheelRadius() const
    {
        const float usableH = getHeight() - chromeTop() - chromeBottom();
        const float usableW = getWidth() - chromeLeft();
        return (usableW < usableH ? usableW : usableH) * 0.46f;
    }

    /* Returns the wheel position under a point, or -1 outside every ring. */
    int hitTest(double px, double py, Ring& outRing) const
    {
        const float cx = wheelCentreX();
        const float cy = wheelCentreY();
        const float outer = wheelRadius();

        const double dx = px - cx;
        const double dy = py - cy;
        const double dist = std::sqrt(dx * dx + dy * dy);

        if (dist > outer || dist < outer * kHubFraction)
            return -1;

        /* Same radii the drawing used, so the hit box always matches the wedge. */
        outRing = kRingKey;
        for (int i = 0; i < kRingCount; ++i) {
            const Ring r = static_cast<Ring>(i);
            if (dist >= ringInnerRadius(r, outer) && dist <= ringOuterRadius(r, outer)) {
                outRing = r;
                break;
            }
        }

        /* Undo the twelve-o'clock rotation to recover the index. */
        double angle = std::atan2(dy, dx) + M_PI * 0.5;
        while (angle < 0)            angle += 2.0 * M_PI;
        while (angle >= 2.0 * M_PI)  angle -= 2.0 * M_PI;

        /* Mirror segmentAngles(), including the minor ring's offset, so a hit box
         * never drifts from the cell that was drawn. */
        const int    n    = segmentsInRing(outRing);
        const double step = 2.0 * M_PI / n;
        double       bias = step * 0.5;

        if (outRing == kRingMinor)
            bias -= step * fMinorOffsetCells;

        double a = angle + bias;
        while (a < 0)            a += 2.0 * M_PI;
        while (a >= 2.0 * M_PI)  a -= 2.0 * M_PI;

        return static_cast<int>(a / step) % n;
    }

    void parameterChanged(uint32_t, float) override {}

    /* ---- control strip ----------------------------------------------------
     *
     * Deliberately hand-drawn rather than built from a widget toolkit: these are
     * settings, not parameters (spec section 5), so they must not be wired to
     * anything the host can automate. Plain rects and hit-tests keep that
     * boundary obvious.
     */

    struct Button { float x, y, w, h; };

    /* Row 1: mode toggles. */
    Button latchButton() const { return { 10.0f,  8.0f,  78.0f, 22.0f }; }
    Button glideButton() const { return { 94.0f,  8.0f,  92.0f, 22.0f }; }
    Button keyLockButton() const { return { 192.0f, 8.0f, 96.0f, 22.0f }; }
    /* Bypass chord generation: the wheel becomes a note selector. */
    Button singleNoteButton() const { return { 294.0f, 8.0f, 104.0f, 22.0f }; }

    /*
     * Row 2: one dropdown pair per ring. Extensions and voicings are per-ring,
     * never per cell - that uniformity is what keeps every chord in a ring the
     * same shape, which is the precondition for single-bend glide.
     */
    static constexpr float kDropY = 36.0f;
    static constexpr float kDropH = 22.0f;

    Button extButton(int ring) const
    {
        const float w = (getWidth() - 20.0f) / 3.0f;
        return { 10.0f + ring * w, kDropY, w * 0.48f - 2.0f, kDropH };
    }

    Button voiceButton(int ring) const
    {
        const float w = (getWidth() - 20.0f) / 3.0f;
        return { 10.0f + ring * w + w * 0.50f, kDropY, w * 0.50f - 2.0f, kDropH };
    }

    static constexpr float kMenuRowH = 19.0f;

    /* Menu rows hang below whichever button opened them. */
    Button menuRow(const Button& anchor, int index) const
    {
        return { anchor.x, anchor.y + anchor.h + 2.0f + index * kMenuRowH,
                 anchor.w < 120.0f ? 140.0f : anchor.w, kMenuRowH };
    }

    Button octaveSlider() const
    {
        const float top = chromeTop();
        return { 8.0f, top,
                 kSliderW, getHeight() - top - chromeBottom() - 8.0f };
    }

    /* Octave under a y coordinate. Inverted: up is higher, as on a keyboard. */
    int octaveAtY(double py) const
    {
        const Button s = octaveSlider();
        float t = static_cast<float>(py - s.y) / s.h;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        const int span = static_cast<int>(kOctaveMax - kOctaveMin);
        return static_cast<int>(kOctaveMax) -
               static_cast<int>(t * span + 0.5f);
    }

    void drawOctaveSlider()
    {
        const Button s = octaveSlider();

        beginPath();
        roundedRect(s.x, s.y, s.w, s.h, 6.0f);
        fillColor(Color(0.12f, 0.13f, 0.17f));
        fill();
        strokeColor(Color(0.26f, 0.28f, 0.34f));
        strokeWidth(1.0f);
        stroke();

        const int span = static_cast<int>(kOctaveMax - kOctaveMin);
        const float rowH = s.h / (span + 1);

        for (int i = 0; i <= span; ++i) {
            const int   oct = static_cast<int>(kOctaveMax) - i;
            const float y   = s.y + i * rowH;
            const bool  on  = (oct == fOctave);

            if (on) {
                beginPath();
                roundedRect(s.x + 3.0f, y + 2.0f, s.w - 6.0f, rowH - 4.0f, 4.0f);
                fillColor(Color(0.30f, 0.62f, 0.45f));
                fill();
            }

            char lbl[8];
            std::snprintf(lbl, sizeof(lbl), "%d", oct);

            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(13.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(on ? Color(0.96f, 0.98f, 0.96f)
                         : Color(0.55f, 0.59f, 0.66f));
            text(s.x + s.w * 0.5f, y + rowH * 0.5f, lbl, nullptr);
        }

        /* Label the control, below the last row. */
        fontSize(9.5f);
        fillColor(Color(0.45f, 0.48f, 0.55f));
        text(s.x + s.w * 0.5f, s.y + s.h + 1.0f, "OCT", nullptr);
    }

    /* Push a new octave to the DSP. Dragging sends every step, so a sounding
     * chord glides through the octaves as the finger moves. */
    void setOctave(int oct)
    {
        if (oct < static_cast<int>(kOctaveMin)) oct = static_cast<int>(kOctaveMin);
        if (oct > static_cast<int>(kOctaveMax)) oct = static_cast<int>(kOctaveMax);
        if (oct == fOctave)
            return;

        fOctave = oct;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", fOctave);
        setState("octave", buf);
        repaint();
    }


    static bool hit(const Button& b, double px, double py)
    {
        return px >= b.x && px <= b.x + b.w && py >= b.y && py <= b.y + b.h;
    }

    void drawButton(const Button& b, const char* label, bool on)
    {
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 4.0f);
        fillColor(on ? Color(0.30f, 0.62f, 0.45f) : Color(0.17f, 0.18f, 0.23f));
        fill();
        strokeColor(Color(0.30f, 0.32f, 0.38f));
        strokeWidth(1.0f);
        stroke();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(on ? Color(0.96f, 0.98f, 0.96f) : Color(0.62f, 0.66f, 0.72f));
        text(b.x + b.w * 0.5f, b.y + b.h * 0.5f, label, nullptr);
    }

    /* Compact dropdown: label on the left, caret on the right. */
    void drawDropdown(const Button& b, const char* label, bool open)
    {
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 3.0f);
        fillColor(Color(0.13f, 0.14f, 0.18f));
        fill();
        strokeColor(open ? Color(0.45f, 0.66f, 0.85f) : Color(0.28f, 0.30f, 0.36f));
        strokeWidth(1.0f);
        stroke();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(10.5f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(0.82f, 0.86f, 0.91f));
        text(b.x + 6.0f, b.y + b.h * 0.5f, label, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(Color(0.50f, 0.55f, 0.63f));
        text(b.x + b.w - 5.0f, b.y + b.h * 0.5f, open ? "▲" : "▼", nullptr);
    }

    void drawControls()
    {
        drawButton(latchButton(), fLatchEnabled ? "Latch: on" : "Latch: off",
                   fLatchEnabled);
        drawButton(glideButton(), kGlideModeName[fGlideMode],
                   fGlideMode != kGlideOff);
        drawButton(keyLockButton(),
                   fKeyLocked ? "Key: locked" : "Key: follows",
                   fKeyLocked);
        drawButton(singleNoteButton(),
                   fSingleNotes ? "Single notes" : "Chords",
                   fSingleNotes);

        /* Per-ring dropdowns, labelled by ring so the mapping is unambiguous. */
        static const char* const kRingTag[kRingCount] = { "Maj", "Min", "Dim" };

        for (int r = 0; r < kRingCount; ++r) {
            char ext[48], voi[48];
            std::snprintf(ext, sizeof(ext), "%s: %s",
                          kRingTag[r], kExtensionName[fRingExt[r]]);
            std::snprintf(voi, sizeof(voi), "%s", kVoicingName[fRingVoice[r]]);

            drawDropdown(extButton(r), ext,
                         fOpenMenu == kMenuExt && fOpenMenuRing == r);
            drawDropdown(voiceButton(r), voi,
                         fOpenMenu == kMenuVoicing && fOpenMenuRing == r);
        }
    }

    /* Unused now that the chord selector is per-ring, but kept because the
     * toggle buttons still use it. */
    /* Drawn last so an open list sits above the wheel. */
    void drawOpenMenu()
    {
        if (fOpenMenu == kMenuNone)
            return;

        const bool   isExt  = (fOpenMenu == kMenuExt);
        const int    rows   = isExt ? static_cast<int>(kExtCount)
                                    : static_cast<int>(kVoicingCount);
        const Button anchor = isExt ? extButton(fOpenMenuRing)
                                    : voiceButton(fOpenMenuRing);
        const int    cur    = isExt ? static_cast<int>(fRingExt[fOpenMenuRing])
                                    : static_cast<int>(fRingVoice[fOpenMenuRing]);

        const Button first = menuRow(anchor, 0);
        const Button last  = menuRow(anchor, rows - 1);

        beginPath();
        roundedRect(first.x - 2.0f, first.y - 2.0f,
                    first.w + 4.0f, (last.y + last.h) - first.y + 4.0f, 4.0f);
        fillColor(Color(0.10f, 0.11f, 0.15f, 0.98f));
        fill();
        strokeColor(Color(0.35f, 0.38f, 0.45f));
        strokeWidth(1.0f);
        stroke();

        for (int i = 0; i < rows; ++i) {
            const Button row = menuRow(anchor, i);

            if (i == cur) {
                beginPath();
                rect(row.x, row.y, row.w, row.h);
                fillColor(Color(0.24f, 0.40f, 0.56f));
                fill();
            }

            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(i == cur ? Color(0.96f, 0.98f, 1.00f)
                               : Color(0.78f, 0.82f, 0.88f));
            text(row.x + 7.0f, row.y + row.h * 0.5f,
                 isExt ? kExtensionName[i] : kVoicingName[i], nullptr);
        }
    }

    /* Returns true when a control consumed the click. */
    bool handleControlClick(double px, double py)
    {
        char buf[16];

        /* An open menu swallows clicks first, so a row cannot fall through to
         * the wheel underneath it. */
        if (fOpenMenu != kMenuNone) {
            const bool   isExt  = (fOpenMenu == kMenuExt);
            const int    rows   = isExt ? static_cast<int>(kExtCount)
                                        : static_cast<int>(kVoicingCount);
            const Button anchor = isExt ? extButton(fOpenMenuRing)
                                        : voiceButton(fOpenMenuRing);

            for (int i = 0; i < rows; ++i) {
                if (hit(menuRow(anchor, i), px, py)) {
                    std::snprintf(buf, sizeof(buf), "%d", i);
                    if (isExt) {
                        fRingExt[fOpenMenuRing] = static_cast<Extension>(i);
                        char key[8];
                        std::snprintf(key, sizeof(key), "ext%d", fOpenMenuRing);
                        setState(key, buf);
                    } else {
                        fRingVoice[fOpenMenuRing] = static_cast<Voicing>(i);
                        char key[8];
                        std::snprintf(key, sizeof(key), "voice%d", fOpenMenuRing);
                        setState(key, buf);
                    }
                    fOpenMenu = kMenuNone;
                    repaint();
                    return true;
                }
            }
            fOpenMenu = kMenuNone;
            repaint();
            return true;   /* click-away closes without selecting */
        }

        /* Octave slider: takes the press and keeps receiving motion, so it can
         * be dragged through the octaves while notes sound. */
        if (hit(octaveSlider(), px, py)) {
            fSliderDrag = true;
            setOctave(octaveAtY(py));
            repaint();
            return true;
        }

        /* Panic first: it sits inside the header bar. */
        if (hit(panicButton(), px, py)) {
            setState("panic", "1");
            fLog.clear();
            /* Nothing is sounding after a panic, so the chord view must not go
             * on claiming otherwise. */
            std::memset(fSounding, 0, sizeof(fSounding));
            fSoundingCount = 0;
            fChordDirty    = false;
            fLastChord.clear();
            repaint();
            return true;
        }

        if (hit(monitorHeader(), px, py)) {
            fMonitorOpen = ! fMonitorOpen;
            repaint();
            return true;
        }

        if (hit(latchButton(), px, py)) {
            fLatchEnabled = ! fLatchEnabled;
            std::snprintf(buf, sizeof(buf), "%d", fLatchEnabled ? 1 : 0);
            setState("latch", buf);
            repaint();
            return true;
        }

        /* Glide cycles off -> on -> MPE -> off. */
        if (hit(glideButton(), px, py)) {
            fGlideMode = static_cast<GlideMode>((fGlideMode + 1) % kGlideModeCount);
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(fGlideMode));
            setState("glideMode", buf);
            repaint();
            return true;
        }

        /* Key lock is purely a UI concern for the highlight - but the DSP does
         * need the key itself, for resolving incoming MIDI notes. */
        if (hit(keyLockButton(), px, py)) {
            fKeyLocked = ! fKeyLocked;
            repaint();
            return true;
        }

        if (hit(singleNoteButton(), px, py)) {
            fSingleNotes = ! fSingleNotes;
            std::snprintf(buf, sizeof(buf), "%d", fSingleNotes ? 1 : 0);
            setState("singleNotes", buf);
            repaint();
            return true;
        }

        for (int r = 0; r < kRingCount; ++r) {
            if (hit(extButton(r), px, py)) {
                fOpenMenu = kMenuExt;
                fOpenMenuRing = r;
                repaint();
                return true;
            }
            if (hit(voiceButton(r), px, py)) {
                fOpenMenu = kMenuVoicing;
                fOpenMenuRing = r;
                repaint();
                return true;
            }
        }

        return false;
    }


    /* ---- MIDI monitor ------------------------------------------------------
     *
     * Built into every format, not just the standalone: seeing the exact note,
     * velocity and bend stream is as useful inside a DAW as outside it, and it
     * is what turns "a note is stuck" into "this note-off never went out".
     * Collapsed by default so it costs nothing until asked for. */

    /* Decode one packed message into something a human can check against the
     * spec: note numbers with names, bend in semitones, RPN by name. */
    void pushLogLine(uint32_t word)
    {
        const uint8_t a = static_cast<uint8_t>((word >> 16) & 0xFF);
        const uint8_t b = static_cast<uint8_t>((word >> 8) & 0xFF);
        const uint8_t c = static_cast<uint8_t>(word & 0xFF);

        const uint8_t status = a & 0xF0;
        char line[96];

        switch (status) {
            case 0x90:
                /* Collect the note into the chord being assembled, so the log
                 * can show what actually sounded together rather than only a
                 * run of separate note-ons. */
                noteOnForChord(b);
                std::snprintf(line, sizeof(line), "NoteOn   %-4s(%3d) vel %d",
                              noteName(b), b, c);
                break;
            case 0x80:
                noteOffForChord(b);
                std::snprintf(line, sizeof(line), "NoteOff  %-4s(%3d)",
                              noteName(b), b);
                break;
            case 0xE0: {
                const int raw = (static_cast<int>(c) << 7) | b;
                const float st = (raw - 8192) / 8191.0f * 12.0f;
                std::snprintf(line, sizeof(line), "Bend     %+.2f st  (%d)", st, raw);
                break;
            }
            case 0xB0:
                std::snprintf(line, sizeof(line), "CC       %d = %d%s",
                              b, c, ccNote(b));
                break;
            default:
                std::snprintf(line, sizeof(line), "raw      %02X %02X %02X", a, b, c);
                break;
        }

        fLog.push_back(line);
        while (fLog.size() > kLogLines)
            fLog.pop_front();
    }

    /* ---- chord view --------------------------------------------------------
     *
     * The per-event lines say what was sent; they do not say what is SOUNDING.
     * Reading three note-ons and holding them in your head is exactly the sort
     * of thing the monitor should do for you - especially when checking whether
     * a chord sits in the key, where the set of notes is the whole question.
     *
     * So the sounding set is tracked, named, and printed as one line whenever
     * it changes.
     */

    void noteOnForChord(uint8_t note)
    {
        if (! fSounding[note]) {
            fSounding[note] = true;
            ++fSoundingCount;
        }
        fChordDirty = true;
    }

    void noteOffForChord(uint8_t note)
    {
        if (fSounding[note]) {
            fSounding[note] = false;
            if (fSoundingCount > 0)
                --fSoundingCount;
        }
        fChordDirty = true;
    }

    /*
     * Name the sounding set by its interval pattern, reduced to pitch classes
     * and rotated so each note in turn is treated as the root. Matching the
     * pattern rather than assuming the lowest note is the root is what lets an
     * inversion be recognised - "C/E" rather than a puzzle.
     */
    static bool nameChord(const bool* sounding, char* out, size_t outSize)
    {
        int  pcs[12];
        int  npc = 0;
        int  lowest = -1;

        bool seen[12] = { false };
        for (int n = 0; n < 128; ++n) {
            if (! sounding[n])
                continue;
            if (lowest < 0)
                lowest = n;
            if (! seen[n % 12]) {
                seen[n % 12] = true;
                if (npc < 12)
                    pcs[npc++] = n % 12;
            }
        }

        if (npc == 0)
            return false;

        static const char* const kPC[12] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };

        if (npc == 1) {
            std::snprintf(out, outSize, "%s", kPC[pcs[0]]);
            return true;
        }

        /* Try each pitch class as the root and look for a known shape. */
        for (int r = 0; r < npc; ++r) {
            const int root = pcs[r];

            int rel[12];
            int nrel = 0;
            for (int i = 0; i < npc; ++i)
                rel[nrel++] = ((pcs[i] - root) % 12 + 12) % 12;

            /* Sort, so the comparison is against a canonical pattern. */
            for (int i = 1; i < nrel; ++i) {
                const int v = rel[i];
                int j = i - 1;
                while (j >= 0 && rel[j] > v) { rel[j + 1] = rel[j]; --j; }
                rel[j + 1] = v;
            }

            for (int t = 0; t < kChordTypeCount; ++t) {
                const ChordShape& sh = kChordShape[t];

                /* Reduce the shape to sorted, unique pitch classes too. */
                int want[12];
                int nwant = 0;
                bool wseen[12] = { false };
                for (int i = 0; i < sh.count; ++i) {
                    const int pc = ((sh.interval[i] % 12) + 12) % 12;
                    if (! wseen[pc]) { wseen[pc] = true; want[nwant++] = pc; }
                }
                for (int i = 1; i < nwant; ++i) {
                    const int v = want[i];
                    int j = i - 1;
                    while (j >= 0 && want[j] > v) { want[j + 1] = want[j]; --j; }
                    want[j + 1] = v;
                }

                if (nwant != nrel)
                    continue;

                bool same = true;
                for (int i = 0; i < nrel && same; ++i)
                    same = (rel[i] == want[i]);

                if (! same)
                    continue;

                /* Name the bass when it is not the root, since that is the
                 * difference between a chord and its inversion. */
                if (lowest >= 0 && (lowest % 12) != root) {
                    std::snprintf(out, outSize, "%s%s/%s",
                                  kPC[root], sh.suffix, kPC[lowest % 12]);
                } else {
                    std::snprintf(out, outSize, "%s%s", kPC[root], sh.suffix);
                }
                return true;
            }
        }

        std::snprintf(out, outSize, "(%d notes)", npc);
        return true;
    }

    /* Emit the chord line if the sounding set changed. Called once per drain,
     * so the notes of one chord are summarised together rather than producing
     * a line per note. */
    void flushChordLine()
    {
        if (! fChordDirty)
            return;
        fChordDirty = false;

        if (fSoundingCount == 0) {
            if (! fLastChord.empty()) {
                fLastChord.clear();
                fLog.push_back("         -- silence --");
                while (fLog.size() > kLogLines)
                    fLog.pop_front();
            }
            return;
        }

        char notes[96] = {0};
        for (int n = 0; n < 128; ++n) {
            if (! fSounding[n])
                continue;
            char one[16];
            std::snprintf(one, sizeof(one), "%s ", noteName(static_cast<uint8_t>(n)));
            if (std::strlen(notes) + std::strlen(one) < sizeof(notes) - 1)
                std::strcat(notes, one);
        }

        char name[32] = {0};
        nameChord(fSounding, name, sizeof(name));

        char line[128];
        std::snprintf(line, sizeof(line), "CHORD    %-9s %s", name, notes);

        /* Only report a genuine change, or a chord would reprint every time a
         * voice is re-sent. */
        if (fLastChord == line)
            return;
        fLastChord = line;

        fLog.push_back(line);
        while (fLog.size() > kLogLines)
            fLog.pop_front();
    }

    static const char* noteName(uint8_t note)
    {
        static const char* const kNames[12] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        static char buf[8];
        std::snprintf(buf, sizeof(buf), "%s%d", kNames[note % 12], (note / 12) - 1);
        return buf;
    }

    /* Annotate the CCs that make up an RPN, so a bend-range announcement is
     * recognisable in the log rather than four anonymous CC lines. */
    static const char* ccNote(uint8_t cc)
    {
        switch (cc) {
            case 101: return "  (RPN MSB)";
            case 100: return "  (RPN LSB)";
            case 6:   return "  (data MSB - bend range)";
            case 38:  return "  (data LSB)";
            default:  return "";
        }
    }

    /* The header bar is always present; the log below it only when expanded. */
    Button monitorHeader() const
    {
        return { 0.0f, getHeight() - kHeaderH,
                 static_cast<float>(getWidth()), kHeaderH };
    }

    Button panicButton() const
    {
        return { getWidth() - 62.0f, getHeight() - kHeaderH + 3.0f, 54.0f,
                 kHeaderH - 6.0f };
    }

    void drawMonitor()
    {
        const float w = getWidth();
        const float h = getHeight();
        const float logH = fMonitorOpen ? kLogLines * 14.0f + 10.0f : 0.0f;
        const float top  = h - kHeaderH - logH;

        /* Expanded log body. */
        if (fMonitorOpen) {
            beginPath();
            rect(0, top, w, logH);
            fillColor(Color(0.05f, 0.06f, 0.08f, 0.96f));
            fill();

            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);

            if (fLog.empty()) {
                fillColor(Color(0.45f, 0.48f, 0.55f));
                text(10.0f, top + 6.0f,
                     "No events yet - click the wheel.", nullptr);
            } else {
                float y = top + 6.0f;
                for (const std::string& line : fLog) {
                    fillColor(Color(0.62f, 0.85f, 0.65f));
                    text(10.0f, y, line.c_str(), nullptr);
                    y += 14.0f;
                }
            }
        }

        /* Header bar: disclosure triangle, title, panic. */
        const Button hdr = monitorHeader();
        beginPath();
        rect(hdr.x, hdr.y, hdr.w, hdr.h);
        fillColor(Color(0.12f, 0.13f, 0.17f));
        fill();

        beginPath();
        moveTo(0.0f, hdr.y);
        lineTo(w, hdr.y);
        strokeColor(Color(0.26f, 0.28f, 0.34f));
        strokeWidth(1.0f);
        stroke();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(0.72f, 0.76f, 0.83f));

        char title[64];
        std::snprintf(title, sizeof(title), "%s  MIDI Monitor%s",
                      fMonitorOpen ? "▼" : "▶",
                      fMonitorOpen ? "" : "  (click to expand)");
        text(10.0f, hdr.y + hdr.h * 0.5f, title, nullptr);

        const Button pb = panicButton();
        beginPath();
        roundedRect(pb.x, pb.y, pb.w, pb.h, 3.0f);
        fillColor(Color(0.42f, 0.18f, 0.18f));
        fill();
        strokeColor(Color(0.60f, 0.28f, 0.28f));
        strokeWidth(1.0f);
        stroke();

        fontSize(10.5f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(0.95f, 0.85f, 0.85f));
        text(pb.x + pb.w * 0.5f, pb.y + pb.h * 0.5f, "Panic", nullptr);
    }

    /* Drain this instance's ring on the UI thread. uiIdle runs at roughly frame
     * rate, which is ample for a human-readable log. */
    void uiIdle() override
    {
        if (fRing == nullptr) {
            /* The plugin registers its ring at construction; look it up once. */
            fRing = monitorRingFor(getPluginInstancePointer());
            if (fRing == nullptr)
                return;
        }

        /* Only drain while the log is visible, so a collapsed panel cannot grow
         * an unbounded backlog of work. The ring drops its own overflow. */
        if (! fMonitorOpen)
            return;

        uint32_t word;
        bool     any = false;

        while (fRing->pop(word)) {
            pushLogLine(word);
            any = true;
        }

        /* Once, after the whole block is drained: the notes of a chord arrive
         * together, so summarising here gives one line per chord rather than
         * one per note. */
        if (any) {
            flushChordLine();
            repaint();
        }
    }

    std::deque<std::string> fLog;
    /* Collapsed by default: the wheel is the point, the log is for debugging. */
    bool         fMonitorOpen = false;
    MonitorRing* fRing        = nullptr;

    /* What is currently sounding, rebuilt from the note-on/note-off stream, so
     * the log can report the chord rather than only the events that made it. */
    bool        fSounding[128] = { false };
    int         fSoundingCount = 0;
    bool        fChordDirty    = false;
    std::string fLastChord;

private:
    int  fActivePosition = -1;
    Ring fActiveRing     = kRingKey;
    bool fDragging       = false;

    /* Which key's wedge is highlighted. Clicking the inner ring moves it. */
    int  fSelectedKey    = 0;   /* C */

    /* Rotation of the 24-cell minor ring, in units of its own cell width.
     * 0 = centres aligned with the key ring, 0.5 = interlocked. Toggled with
     * 'o' so the correct value can be settled by eye. */
    float fMinorOffsetCells = 0.0f;

    /* Mirrors of DSP settings, so the UI can render them and toggle them. */
    bool      fLatchEnabled = false;
    GlideMode fGlideMode    = kGlideOn;

    /* Per-ring, never per cell: uniform shape within a ring is what makes
     * single-bend glide valid. */
    Extension fRingExt[kRingCount]   = { kExtNone, kExtNone, kExtNone };
    Voicing   fRingVoice[kRingCount] = {
        kVoicingRegular, kVoicingRegular, kVoicingRegular
    };

    /* Which dropdown is open, if any. */
    enum OpenMenu { kMenuNone = 0, kMenuExt, kMenuVoicing };
    OpenMenu fOpenMenu     = kMenuNone;
    int      fOpenMenuRing = 0;

    /* When locked, clicking the key ring plays the chord but leaves the
     * highlighted wedge where it is. */
    bool fKeyLocked = false;

    /* Bypass chord generation and sound the root alone. */
    bool fSingleNotes = false;

    /* Octave for pointer and touch input. Mirrors the DSP's setting; a played
     * MIDI note carries its own octave instead. */
    int  fOctave = 4;

    /* A drag that started on the octave slider, kept separate from the wheel's
     * drag so the two gestures cannot interfere. */
    bool fSliderDrag = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FortyFifthUI)
};

UI* createUI()
{
    return new FortyFifthUI();
}

END_NAMESPACE_DISTRHO
