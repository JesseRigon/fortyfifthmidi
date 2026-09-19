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
#include <cstring>

#if FORTYFIFTH_MIDI_MONITOR
# include <cstdlib>
# include <deque>
# include <string>
#endif

START_NAMESPACE_DISTRHO

using namespace fortyfifth;

class FortyFifthUI : public UI
{
#if FORTYFIFTH_MIDI_MONITOR
    static constexpr size_t kLogLines = 14;
#endif

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
        drawControls();

#if FORTYFIFTH_MIDI_MONITOR
        drawMonitor();
#endif

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
            if (ring == kRingKey && ! fKeyLocked)
                fSelectedKey = pos;

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

        if (fDragging) {
            fDragging = false;
            sendGesture("release", fActivePosition, fActiveRing);

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

    /* The wheel is centred in whatever space the control strip and (in the test
     * rig) the event log leave behind, so nothing overlaps. */
    /* Two control rows: toggles, then the per-ring dropdowns. */
    float chromeTop() const { return kDropY + kDropH + 10.0f; }

    float chromeBottom() const
    {
#if FORTYFIFTH_MIDI_MONITOR
        return kLogLines * 14.0f + 16.0f;
#else
        return 8.0f;
#endif
    }

    float wheelCentreX() const { return getWidth() * 0.5f; }

    float wheelCentreY() const
    {
        return chromeTop() + (getHeight() - chromeTop() - chromeBottom()) * 0.5f;
    }

    float wheelRadius() const
    {
        const float usableH = getHeight() - chromeTop() - chromeBottom();
        const float usableW = getWidth();
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

        /* Key lock is purely a UI concern - the DSP never needs to know which
         * wedge is highlighted. */
        if (hit(keyLockButton(), px, py)) {
            fKeyLocked = ! fKeyLocked;
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


#if FORTYFIFTH_MIDI_MONITOR
    /* ---- test-rig MIDI monitor (standalone build only) -------------------- */

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
                std::snprintf(line, sizeof(line), "NoteOn   %-4s(%3d) vel %d",
                              noteName(b), b, c);
                break;
            case 0x80:
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

    void drawMonitor()
    {
        const float w = getWidth();
        const float h = getHeight();
        const float panelH = kLogLines * 14.0f + 16.0f;
        const float top = h - panelH;

        beginPath();
        rect(0, top, w, panelH);
        fillColor(Color(0.05f, 0.06f, 0.08f, 0.92f));
        fill();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);

        if (fLog.empty()) {
            fillColor(Color(0.45f, 0.48f, 0.55f));
            text(10.0f, top + 8.0f,
                 "MIDI monitor - click the wheel to emit events", nullptr);
            return;
        }

        float y = top + 8.0f;
        for (const std::string& line : fLog) {
            fillColor(Color(0.62f, 0.85f, 0.65f));
            text(10.0f, y, line.c_str(), nullptr);
            y += 14.0f;
        }
    }

    /* Drain the shared ring on the UI thread. uiIdle runs at roughly frame rate,
     * which is ample for a human-readable log. */
    void uiIdle() override
    {
        uint32_t word;
        bool     any = false;

        while (monitorRing().pop(word)) {
            pushLogLine(word);
            any = true;
        }

        if (any)
            repaint();
    }

    std::deque<std::string> fLog;
#endif /* FORTYFIFTH_MIDI_MONITOR */

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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FortyFifthUI)
};

UI* createUI()
{
    return new FortyFifthUI();
}

END_NAMESPACE_DISTRHO
