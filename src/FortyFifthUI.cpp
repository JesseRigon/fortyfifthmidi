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

    /* A hit-testable rectangle. Declared here because almost everything below
     * - tabs, buttons, menus, slider, slide strips - is expressed in them. */
    struct Button { float x, y, w, h; };

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

        /* Same factory bindings the DSP starts with, so the editor shows the
         * truth before anything has been changed. */
        std::memcpy(fKeyMap, kDefaultKeyMap, sizeof(fKeyMap));
    }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth();
        const float h = getHeight();

        beginPath();
        rect(0, 0, w, h);
        fillColor(Color(0.09f, 0.10f, 0.13f));
        fill();

        drawTabBar();

        /* The two screens are different control surfaces over the same engine,
         * so only the central area swaps - the slider, the control row and the
         * monitor are shared. */
        if (fScreen == kScreenCircle)
            drawWheel();
        else if (fScreen == kScreenSlide)
            drawSlides();
        else if (fScreen == kScreenProgressions)
            drawProgressions();
        else
            drawKeyboardSetup();

        /* The setup screen edits bindings rather than playing, so the
         * performance controls would only be noise there. */
        if (fScreen != kScreenKeys) {
            drawOctaveSlider();
            drawControls();
        }

        drawMonitor();

        /* Last, so an open list overlays everything beneath it. */
        drawOpenMenu();
    }

    void drawWheel()
    {
        const float cx = wheelCentreX();
        const float cy = wheelCentreY();
        const float outer = wheelRadius();

        /* Outermost first so inner rings overlay their borders cleanly. */
        for (int ring = kRingCount - 1; ring >= 0; --ring) {
            const Ring  r    = static_cast<Ring>(ring);
            const float rIn  = ringInnerRadius(r, outer);
            const float rOut = ringOuterRadius(r, outer);

            for (int i = 0; i < segmentsInRing(r); ++i)
                drawSegment(cx, cy, rIn, rOut, i, r);
        }

        drawCenterReadout(cx, cy, ringInnerRadius(kRingKey, outer));
    }

    /* ---- tab bar -----------------------------------------------------------
     *
     * Two screens over one engine. The tab bar sits above everything so the
     * shared chrome below it never moves when the screen changes.
     */

    /*
     * Setup sits first because it is where a session starts - bindings before
     * playing. The three performance screens follow in the order they were
     * added, which is also increasing order of structure: a wheel, a strip, a
     * sequence.
     */
    enum Screen {
        kScreenKeys = 0,
        kScreenCircle,
        kScreenSlide,
        kScreenProgressions,
        kScreenCount
    };

    static constexpr float kTabH = 28.0f;

    /* Setup is an icon, so it needs less room than the word tabs. */
    static constexpr float kSetupTabW = 34.0f;
    static constexpr float kTabW      = 104.0f;

    Button tabButton(int index) const
    {
        if (index == kScreenKeys)
            return { 10.0f, 4.0f, kSetupTabW, kTabH - 8.0f };

        const float x = 10.0f + kSetupTabW + 4.0f
                      + (index - 1) * (kTabW + 4.0f);
        return { x, 4.0f, kTabW, kTabH - 8.0f };
    }

    void drawTabBar()
    {
        /* "Mode" told the user nothing the tab bar did not already say. */
        static const char* const kNames[kScreenCount] = {
            "⚙", "CIRCLE", "SLIDE", "PROGRESSIONS"
        };

        for (int i = 0; i < kScreenCount; ++i) {
            const Button b  = tabButton(i);
            const bool   on = (static_cast<int>(fScreen) == i);

            beginPath();
            roundedRect(b.x, b.y, b.w, b.h, 4.0f);
            fillColor(on ? Color(0.24f, 0.40f, 0.56f)
                         : Color(0.14f, 0.15f, 0.20f));
            fill();
            strokeColor(on ? Color(0.45f, 0.66f, 0.85f)
                           : Color(0.26f, 0.28f, 0.34f));
            strokeWidth(1.0f);
            stroke();

            fontFace(NANOVG_DEJAVU_SANS_TTF);
            /* The gear needs more size than the word tabs to read at all. */
            fontSize(i == kScreenKeys ? 15.0f : 11.5f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(on ? Color(0.96f, 0.98f, 1.00f)
                         : Color(0.62f, 0.66f, 0.72f));
            text(b.x + b.w * 0.5f, b.y + b.h * 0.5f, kNames[i], nullptr);
        }
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

        /*
         * Lit if the pointer is on it, OR if the DSP says it is sounding.
         * The second is what shows notes played from a MIDI keyboard, which
         * never touch the UI's own gesture state at all.
         */
        const bool active = (fActivePosition == index && fActiveRing == ring)
                         || (fCells != nullptr && fCells->isOn(ring, index));

        beginPath();
        arc(cx, cy, rOut, start, end, NanoVG::CW);
        arc(cx, cy, rIn, end, start, NanoVG::CCW);
        closePath();

        /* Each ring gets its own hue so the three are distinguishable at a
         * glance; the diminished ring is dimmest, matching its lighter use. */
        /* A cell belonging to the highlighted key's wedge is lifted, so the
         * seven diatonic chords read as one shape - the whole point of the
         * wheel. This is the ONLY thing key lock governs: it pins the wedge so
         * the reference stays put while you play outside it. What the keyboard
         * maps to is a separate question, answered by fSelectedKey. */
        const CellRole role    = roleInKey(index, ring, highlightKey());
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
            const char* deg = degreeInKey(index, ring, highlightKey());
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

    /* Send the whole map as one encoded value - "action:value" per key, comma
     * separated. Twelve separate state keys would bloat the host's saved state
     * for no benefit, and a partial update could leave the two copies
     * disagreeing about what a key does. */
    void pushKeyMap()
    {
        char out[128] = {0};
        for (int i = 0; i < 12; ++i) {
            char one[16];
            std::snprintf(one, sizeof(one), "%s%d:%d", i ? "," : "",
                          static_cast<int>(fKeyMap[i].action),
                          fKeyMap[i].value);
            std::strncat(out, one, sizeof(out) - std::strlen(out) - 1);
        }
        setState("keyMap", out);
    }

    /* Which key's wedge is drawn as the diatonic set. Locked, it stays where
     * it was pinned; otherwise it follows the selection. */
    int highlightKey() const
    {
        return fKeyLocked ? fLockedKey : fSelectedKey;
    }

    /*
     * Choose the key, and tell the DSP.
     *
     * Key lock and the keyboard's key are two different questions, and
     * conflating them was wrong. Lock governs only which wedge stays
     * HIGHLIGHTED - a reading aid, so the diatonic set can stay put while you
     * play a chord outside it. The keyboard's mapping is a different matter:
     * whatever cell is selected and in focus is the key you are playing in, so
     * pressing C should sound that key's I whether or not the highlight is
     * pinned. Gating this on the lock meant a locked wheel left the keyboard
     * stuck in whatever key was last unlocked.
     *
     * fSelectedKey therefore always tracks the selection; only the wedge
     * drawing consults fKeyLocked.
     */
    void selectKey(int keyIndex)
    {
        const int k = ((keyIndex % 12) + 12) % 12;
        if (k == fSelectedKey)
            return;

        fSelectedKey = k;

        /* The DSP resolves an incoming MIDI note to a cell by degree, so it
         * cannot map anything without knowing the key. */
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", fSelectedKey);
        setState("selectedKey", buf);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.press) {
            if (handleControlClick(ev.pos.getX(), ev.pos.getY()))
                return true;

            if (fScreen == kScreenSlide)
                return slidePress(ev.pos.getX(), ev.pos.getY());

            Ring ring;
            const int pos = hitTest(ev.pos.getX(), ev.pos.getY(), ring);
            if (pos < 0)
                return false;

            /* Clicking the key ring picks the key. Key lock governs only the
             * highlighted wedge - see selectKey() for why the keyboard follows
             * the selection regardless. */
            if (ring == kRingKey)
                selectKey(pos);

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
            if (! fLatchEnabled) {
                fActivePosition = -1;
                fActiveSlide    = -1;
            }

            repaint();
            return true;
        }
        return false;
    }

    /*
     * Scrolling a strip reaches the variations that do not fit.
     *
     * Four rows are shown at a time so they stay big enough to hit, but the
     * engine has seven extensions; scrolling is what makes the other three
     * reachable without thinning every row.
     */
    bool onScroll(const ScrollEvent& ev) override
    {
        if (fScreen != kScreenSlide || fSectionMode != kSectionVariation)
            return false;

        int section;
        if (hitSlide(ev.pos.getX(), ev.pos.getY(), section) < 0)
            return false;

        const int maxScroll = static_cast<int>(kExtCount) - kVariationRows;
        int next = fVariationScroll - static_cast<int>(ev.delta.getY());
        if (next < 0)         next = 0;
        if (next > maxScroll) next = maxScroll;

        if (next == fVariationScroll)
            return true;

        fVariationScroll = next;
        repaint();
        return true;
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
            sliderTo(ev.pos.getY());
            return true;
        }

        if (! fDragging)
            return false;

        if (fScreen == kScreenSlide)
            return slideMotion(ev.pos.getX(), ev.pos.getY());

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

    /* Height of the piano-roll body when open. */
    static constexpr float kRollH = 54.0f;

    /*
     * Two independent panels stacked at the bottom, each with its own header
     * and its own collapsed state: the keyboard is useful on its own, and
     * having to open the text stream to see it would defeat the point.
     *
     *   [ log body    ]  optional
     *   [ MIDI Monitor ] header
     *   [ roll body    ]  optional
     *   [ Notes        ] header
     */
    float chromeBottom() const
    {
        return kHeaderH * 2.0f
             + (fMonitorOpen ? kLogLines * 14.0f + 10.0f : 0.0f)
             + (fRollOpen    ? kRollH : 0.0f);
    }

    /* Bottom-up stacking: the roll sits under the log, so opening one does not
     * move the other's header out from under the pointer. */
    float rollHeaderY() const { return getHeight() - kHeaderH; }
    float rollBodyY()   const { return rollHeaderY() - (fRollOpen ? kRollH : 0.0f); }
    float logHeaderY()  const { return rollBodyY() - kHeaderH; }
    float logBodyY()    const
    {
        return logHeaderY() - (fMonitorOpen ? kLogLines * 14.0f + 10.0f : 0.0f);
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

    /* (Button is declared near the top of the class, since the tab bar and the
     * wheel geometry both need it before this point.) */

    /* Row 1: mode toggles. Below the tab bar, which owns the top strip. */
    static constexpr float kRow1Y = kTabH + 8.0f;

    Button latchButton() const { return { 10.0f,  kRow1Y,  78.0f, 22.0f }; }
    Button glideButton() const { return { 94.0f,  kRow1Y,  92.0f, 22.0f }; }
    Button keyLockButton() const { return { 192.0f, kRow1Y, 108.0f, 22.0f }; }
    /* Bypass chord generation: the wheel becomes a note selector. */
    Button singleNoteButton() const { return { 306.0f, kRow1Y, 104.0f, 22.0f }; }
    /* Settle each chord near the last instead of stacking from its own root. */
    Button voiceLeadButton() const { return { 416.0f, kRow1Y, 126.0f, 22.0f }; }

    /*
     * Row 2: one dropdown pair per ring. Extensions and voicings are per-ring,
     * never per cell - that uniformity is what keeps every chord in a ring the
     * same shape, which is the precondition for single-bend glide.
     */
    static constexpr float kDropY = kTabH + 36.0f;
    static constexpr float kDropH = 22.0f;

    /*
     * Slide Mode's own second row, replacing the per-ring dropdowns that only
     * make sense next to the wheel. Same y, so the screens stay aligned.
     */
    Button sectionModeButton() const { return { 10.0f,  kDropY, 150.0f, kDropH }; }
    Button scaleButton()       const { return { 166.0f, kDropY, 150.0f, kDropH }; }
    Button slideKeyButton()    const { return { 322.0f, kDropY, 104.0f, kDropH }; }

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
        /* In octave-sections mode the slider is the variation control, so it
         * must show variations - a column of octave numbers would be lying
         * about what it does. */
        if (fScreen == kScreenSlide && fSectionMode == kSectionOctave) {
            drawVariationSlider();
            return;
        }

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

    /*
     * The slider takes whatever the sections are not doing.
     *
     * On the wheel, and whenever the strips are varying the chord, it is the
     * octave. When the strips are doing octaves it selects the variation
     * instead - so both dimensions stay reachable without leaving the screen,
     * which is the whole point of the swap.
     */
    void sliderTo(double py)
    {
        if (fScreen == kScreenSlide && fSectionMode == kSectionOctave) {
            const Button s = slideAreaSlider();
            float t = static_cast<float>(py - s.y) / s.h;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            int v = static_cast<int>(t * (kExtCount - 1) + 0.5f);
            if (v < 0) v = 0;
            if (v > static_cast<int>(kExtCount) - 1)
                v = static_cast<int>(kExtCount) - 1;

            if (v != fSliderVariation) {
                fSliderVariation = v;
                repaint();
            }
            return;
        }

        setOctave(octaveAtY(py));
    }

    /* The slider's own rect, named apart from octaveSlider() because in
     * variation mode it is not an octave control at all. */
    Button slideAreaSlider() const { return octaveSlider(); }

    /* The slider rendered as a variation picker, for when the sections have
     * taken over the octaves. */
    void drawVariationSlider()
    {
        const Button s = octaveSlider();

        beginPath();
        roundedRect(s.x, s.y, s.w, s.h, 6.0f);
        fillColor(Color(0.12f, 0.13f, 0.17f));
        fill();
        strokeColor(Color(0.26f, 0.28f, 0.34f));
        strokeWidth(1.0f);
        stroke();

        const int   rows = static_cast<int>(kExtCount);
        const float rowH = s.h / rows;

        /* Short names: the column is narrow, and the full ones ("None
         * (triad)") would not fit. */
        static const char* const kShort[kExtCount] = {
            "—", "6", "7", "9", "a9", "s2", "s4"
        };

        for (int i = 0; i < rows; ++i) {
            const float y  = s.y + i * rowH;
            const bool  on = (i == fSliderVariation);

            if (on) {
                beginPath();
                roundedRect(s.x + 3.0f, y + 2.0f, s.w - 6.0f, rowH - 4.0f, 4.0f);
                fillColor(Color(0.30f, 0.62f, 0.45f));
                fill();
            }

            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(13.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(on ? Color(0.96f, 0.98f, 0.96f)
                         : Color(0.55f, 0.59f, 0.66f));
            text(s.x + s.w * 0.5f, y + rowH * 0.5f, kShort[i], nullptr);
        }

        fontSize(9.5f);
        fillColor(Color(0.45f, 0.48f, 0.55f));
        text(s.x + s.w * 0.5f, s.y + s.h + 1.0f, "VAR", nullptr);
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


    /* ---- slide mode --------------------------------------------------------
     *
     * Vertical chord strips, one per scale degree, in the order a chord chart
     * reads. Each strip is divided into sections, and a toggle decides what a
     * section means:
     *
     *   octave mode     every section is the same chord at a different octave,
     *                   and the LEFT SLIDER selects the chord variation.
     *   variation mode  the top section is the plain chord and the ones below
     *                   are its extensions, with the slider selecting octave.
     *
     * The slider always takes whatever the sections are not doing, so both
     * dimensions stay reachable without leaving the screen.
     */

    enum SectionMode { kSectionOctave = 0, kSectionVariation };

    /* Five octave rows: +2 +1 0 -1 -2 around the slider's octave. */
    static constexpr int kOctaveRows = 5;

    /* Four variations visible at a time; the strip scrolls to reach the rest,
     * so every extension is available without thinning the rows. */
    static constexpr int kVariationRows = 4;

    int sectionCount() const
    {
        return (fSectionMode == kSectionOctave) ? kOctaveRows : kVariationRows;
    }

    /* The area the strips occupy - the same space the wheel would use. */
    Button slideArea() const
    {
        const float top = chromeTop();
        return { chromeLeft(), top,
                 getWidth() - chromeLeft() - 10.0f,
                 getHeight() - top - chromeBottom() - 8.0f };
    }

    /*
     * Strips ABUT rather than sit apart: a gap between them would break a
     * glide drag, since the finger would leave every strip on the way across
     * and the gesture would end. Shared borders keep the drag continuous, so
     * sliding across the strips plays a run the way dragging the wheel does.
     *
     * The octave slider on the left stays visually separate - it is a
     * different control, not another chord, and a drag must not wander into
     * it by accident.
     */
    Button slideRect(int index) const
    {
        const Button a = slideArea();
        const int    n = slideCountForScale(fScale);
        const float  w = a.w / n;
        return { a.x + index * w, a.y, w, a.h };
    }

    Button sectionRect(int slide, int section) const
    {
        const Button s = slideRect(slide);
        const int    n = sectionCount();
        const float  h = s.h / n;
        return { s.x, s.y + section * h, s.w, h };
    }

    /*
     * What a section plays.
     *
     * Returns the extension and the octave offset, which between them are the
     * whole of what a section varies - the chord itself comes from the slide's
     * degree. In octave mode the scroll offset picks the variation for the
     * whole screen; in variation mode it picks which four of the seven
     * extensions are on show.
     */
    void sectionSettings(int section, Extension& outExt, int& outOctave) const
    {
        if (fSectionMode == kSectionOctave) {
            /* Top row is the highest octave, as on a keyboard. */
            outOctave = 2 - section;
            outExt    = static_cast<Extension>(
                ((fSliderVariation % kExtCount) + kExtCount) % kExtCount);
        } else {
            outOctave = 0;
            const int idx = section + fVariationScroll;
            outExt = static_cast<Extension>(
                ((idx % kExtCount) + kExtCount) % kExtCount);
        }
    }

    /* Chord label for a section, e.g. "Cmaj7". */
    void sectionLabel(int slide, int section, char* out, size_t outSize) const
    {
        const SlideDef* defs = slidesForScale(fScale);
        int  pos;
        Ring ring;
        cellForDegree(defs[slide].degree, fSelectedKey, pos, ring);

        Extension ext;
        int       oct;
        sectionSettings(section, ext, oct);

        const ChordType base = fSingleNotes
            ? kChordSingleNote
            : extendChord(defaultChordForRing(ring), ext);

        /* The cell's own label already carries the minor 'm' and the dim sign,
         * so append only what the extension adds beyond the triad. */
        const char* root = labelForPosition(pos, ring);

        if (fSingleNotes || ext == kExtNone) {
            std::snprintf(out, outSize, "%s", root);
            return;
        }

        /* Strip the triad's own suffix off the root label so it is not
         * doubled: the extension's suffix already implies the quality. */
        char stem[16];
        std::snprintf(stem, sizeof(stem), "%s", root);
        const size_t len = std::strlen(stem);
        if (len > 1 && (stem[len - 1] == 'm'))
            stem[len - 1] = '\0';

        std::snprintf(out, outSize, "%s%s", stem, kChordShape[base].suffix);
    }

    void drawSlides()
    {
        const int n = slideCountForScale(fScale);
        const SlideDef* defs = slidesForScale(fScale);

        /* One rounded panel behind the whole bank, so the strips read as a
         * continuous surface with dividers rather than as separate buttons. */
        const Button area = slideArea();
        beginPath();
        roundedRect(area.x, area.y, area.w, area.h, 5.0f);
        fillColor(Color(0.12f, 0.13f, 0.17f));
        fill();

        for (int i = 0; i < n; ++i) {
            const Button s = slideRect(i);

            /*
             * A cell sounding from the KEYBOARD lights the whole strip, since
             * a played note picks a degree and not a section. A cell sounding
             * from the pointer lights only the section that was pressed.
             *
             * Distinguishing them matters: lighting the strip in both cases
             * lit every section of a pressed column, which read as the whole
             * stack coming on at once. fActiveSlide is what tells them apart -
             * the pointer sets it, the keyboard never does.
             */
            int  cellPos;
            Ring cellRing;
            cellForDegree(defs[i].degree, fSelectedKey, cellPos, cellRing);
            const bool cellLit =
                (fCells != nullptr && fCells->isOn(cellRing, cellPos));

            /*
             * "From the keyboard" means the pointer never touched this strip -
             * not merely that it is not touching it NOW. Releasing clears
             * fActiveSlide while the DSP's note-offs are still in flight, and
             * for those frames the old test saw a lit cell with no active
             * slide and lit the entire stack. fPointerSlide remembers which
             * strip the pointer last used, so that window cannot open.
             */
            const bool fromKeyboard = cellLit && (fPointerSlide != i);

            for (int sec = 0; sec < sectionCount(); ++sec) {
                const Button r = sectionRect(i, sec);
                /* Lit by the pointer: either mid-drag, or still sounding from
                 * the section the pointer last used (latch, or a release
                 * whose note-offs have not landed yet). */
                const bool pressed =
                    (fActiveSlide == i && fActiveSection == sec) ||
                    (fPointerSlide == i && fPointerSection == sec && cellLit);

                const bool active = pressed || fromKeyboard;

                /* Filled edge to edge - cells touch, so a drag never leaves
                 * the bank between them. */
                beginPath();
                rect(r.x, r.y, r.w, r.h);

                if (active) {
                    fillColor(Color(0.98f, 0.72f, 0.24f));
                } else if (fSectionMode == kSectionVariation && sec == 0) {
                    /* The plain chord is the anchor of the strip; give it
                     * more weight than its variations. */
                    fillColor(Color(0.28f, 0.31f, 0.40f));
                } else {
                    fillColor(Color(0.18f, 0.20f, 0.26f));
                }
                fill();

                /* Hairline dividers: enough to read the grid, not enough to
                 * break the surface into islands. */
                beginPath();
                moveTo(r.x + r.w, r.y);
                lineTo(r.x + r.w, r.y + r.h);
                moveTo(r.x, r.y + r.h);
                lineTo(r.x + r.w, r.y + r.h);
                strokeColor(Color(0.09f, 0.10f, 0.13f));
                strokeWidth(1.0f);
                stroke();

                char label[32];
                sectionLabel(i, sec, label, sizeof(label));

                fontFace(NANOVG_DEJAVU_SANS_TTF);
                fontSize(s.w > 62.0f ? 14.0f : 11.0f);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                fillColor(active ? Color(0.08f, 0.09f, 0.12f)
                                 : Color(0.92f, 0.94f, 0.98f));
                text(r.x + r.w * 0.5f, r.y + r.h * 0.5f - 5.0f, label, nullptr);

                /* Second line: what this section varies. In octave mode that
                 * is the octave; in variation mode, the extension's name. */
                Extension ext;
                int       oct;
                sectionSettings(sec, ext, oct);

                char sub[24];
                if (fSectionMode == kSectionOctave)
                    std::snprintf(sub, sizeof(sub), "%+d", oct);
                else
                    std::snprintf(sub, sizeof(sub), "%s",
                                  ext == kExtNone ? "triad" : kExtensionName[ext]);

                fontSize(9.0f);
                fillColor(active ? Color(0.20f, 0.18f, 0.10f)
                                 : Color(0.55f, 0.60f, 0.68f));
                text(r.x + r.w * 0.5f, r.y + r.h * 0.5f + 8.0f, sub, nullptr);
            }

            /* Degree numeral under the strip, so the progression can be read
             * off the screen the way it can off the wheel. */
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(11.0f);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            fillColor(Color(0.55f, 0.78f, 0.98f));
            text(s.x + s.w * 0.5f, s.y + s.h + 2.0f,
                 kDegreeCell[defs[i].degree].numeral, nullptr);
        }
    }

    /* ---- keyboard setup ----------------------------------------------------
     *
     * One octave of piano, drawn to scale, with every key clickable. Bindings
     * repeat in every octave, so editing one key here changes it everywhere on
     * the controller - which is what makes the scheme playable with one hand
     * wherever it happens to be.
     */

    /* White-key index for each pitch class, and -1 for the black keys. */
    static int whiteIndexFor(int pc)
    {
        static const int kIdx[12] = { 0,-1, 1,-1, 2, 3,-1, 4,-1, 5,-1, 6 };
        return kIdx[((pc % 12) + 12) % 12];
    }

    Button keyboardArea() const
    {
        const float top = chromeTop();
        return { chromeLeft(), top + 10.0f,
                 getWidth() - chromeLeft() - 16.0f, 190.0f };
    }

    Button whiteKeyRect(int pc) const
    {
        const Button a = keyboardArea();
        const float  w = a.w / 7.0f;
        return { a.x + whiteIndexFor(pc) * w, a.y, w - 2.0f, a.h };
    }

    /*
     * Black keys sit between their neighbours, narrower and shorter, as on a
     * real keyboard. Drawn and hit-tested from the same rect so a click can
     * never land on a key other than the one under the cursor.
     */
    Button blackKeyRect(int pc) const
    {
        const Button a = keyboardArea();
        const float  w = a.w / 7.0f;

        /* Which white key each black one sits after. */
        int after;
        switch (((pc % 12) + 12) % 12) {
            case 1:  after = 0; break;   /* C# */
            case 3:  after = 1; break;   /* D# */
            case 6:  after = 3; break;   /* F# */
            case 8:  after = 4; break;   /* G# */
            default: after = 5; break;   /* A# */
        }

        const float bw = w * 0.62f;
        return { a.x + (after + 1) * w - bw * 0.5f, a.y, bw, a.h * 0.62f };
    }

    Button keyRect(int pc) const
    {
        return isBlackKey(pc) ? blackKeyRect(pc) : whiteKeyRect(pc);
    }

    /* Black keys first: they overlap the whites, so they must win a hit. */
    int hitKey(double px, double py) const
    {
        for (int pc = 0; pc < 12; ++pc)
            if (isBlackKey(pc) && hit(blackKeyRect(pc), px, py))
                return pc;
        for (int pc = 0; pc < 12; ++pc)
            if (! isBlackKey(pc) && hit(whiteKeyRect(pc), px, py))
                return pc;
        return -1;
    }

    /* Short label for what a key does, sized for the key it sits on. */
    void keyBindingLabel(int pc, char* out, size_t outSize) const
    {
        const KeyMapEntry& e = fKeyMap[pc];
        switch (e.action) {
            case kKeyDegree:
                std::snprintf(out, outSize, "%s",
                              kDegreeCell[e.value % kDegreeCount].numeral);
                break;
            case kKeyExtension: {
                static const char* const kShort[kExtCount] = {
                    "triad", "6th", "7th", "9th", "add9", "sus2", "sus4"
                };
                std::snprintf(out, outSize, "%s",
                              kShort[((e.value % kExtCount) + kExtCount) % kExtCount]);
                break;
            }
            case kKeyGlideToggle:  std::snprintf(out, outSize, "glide");  break;
            case kKeyLatchToggle:  std::snprintf(out, outSize, "latch");  break;
            case kKeySingleToggle: std::snprintf(out, outSize, "single"); break;
            case kKeyPanic:        std::snprintf(out, outSize, "panic");  break;
            default:               std::snprintf(out, outSize, "—");      break;
        }
    }

    void drawKeyboardSetup()
    {
        const Button a = keyboardArea();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(11.5f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(0.62f, 0.66f, 0.72f));
        text(a.x, a.y - 14.0f,
             "Click a key to change what it does. Bindings repeat in every octave.",
             nullptr);

        /* Whites first, then blacks on top - the same order the hit test uses
         * in reverse, so what is drawn and what is clickable agree. */
        for (int pc = 0; pc < 12; ++pc) {
            if (isBlackKey(pc))
                continue;
            drawOneKey(pc, whiteKeyRect(pc), false);
        }
        for (int pc = 0; pc < 12; ++pc) {
            if (! isBlackKey(pc))
                continue;
            drawOneKey(pc, blackKeyRect(pc), true);
        }

        drawPedalRow(a.y + a.h + 24.0f);
    }

    void drawOneKey(int pc, const Button& r, bool black)
    {
        const bool sel = (fEditKey == pc);
        const KeyMapEntry& e = fKeyMap[pc];

        /* Light a key whose degree is currently sounding, so a binding can be
         * confirmed by playing it rather than by trusting the label. */
        bool playing = false;
        if (fCells != nullptr && e.action == kKeyDegree) {
            int  p;
            Ring rg;
            cellForDegree(static_cast<Degree>(e.value), fSelectedKey, p, rg);
            playing = fCells->isOn(rg, p);
        }

        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 3.0f);

        if (playing)                         fillColor(Color(0.42f, 0.78f, 0.95f));
        else if (sel)                        fillColor(Color(0.98f, 0.72f, 0.24f));
        else if (black)                      fillColor(Color(0.13f, 0.14f, 0.18f));
        else if (e.action == kKeyDegree)     fillColor(Color(0.90f, 0.92f, 0.95f));
        else                                 fillColor(Color(0.72f, 0.75f, 0.80f));
        fill();

        strokeColor(Color(0.07f, 0.08f, 0.10f));
        strokeWidth(1.0f);
        stroke();

        /* An unbound key reads as silent, which is a real state worth seeing. */
        const bool silent = (e.action == kKeyNone);

        char lbl[24];
        keyBindingLabel(pc, lbl, sizeof(lbl));

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);

        const bool lit = (sel || playing);

        fontSize(black ? 9.5f : 11.0f);
        if (lit)          fillColor(Color(0.10f, 0.09f, 0.06f));
        else if (silent)  fillColor(Color(0.45f, 0.30f, 0.30f));
        else if (black)   fillColor(Color(0.80f, 0.84f, 0.90f));
        else              fillColor(Color(0.15f, 0.17f, 0.22f));
        text(r.x + r.w * 0.5f, r.y + r.h - 8.0f, lbl, nullptr);

        /* Note name underneath, so the key is identifiable at a glance. */
        fontSize(black ? 8.5f : 10.0f);
        if (lit)        fillColor(Color(0.30f, 0.26f, 0.14f));
        else if (black) fillColor(Color(0.48f, 0.52f, 0.60f));
        else            fillColor(Color(0.45f, 0.49f, 0.56f));
        text(r.x + r.w * 0.5f, r.y + r.h - 22.0f, kPitchName[pc], nullptr);
    }

    Button pedalButton() const
    {
        const Button a = keyboardArea();
        return { a.x, a.y + a.h + 34.0f, 210.0f, 24.0f };
    }

    void drawPedalRow(float labelY)
    {
        const Button a = keyboardArea();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(11.5f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(0.62f, 0.66f, 0.72f));
        text(a.x, labelY, "Sustain pedal (CC 64):", nullptr);

        char buf[48];
        std::snprintf(buf, sizeof(buf), "%s", kPedalActionName[fPedalAction]);
        drawDropdown(pedalButton(), buf, fOpenMenu == kMenuPedal);

        /* Say why rebinding the pedal is worth doing. */
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(0.45f, 0.48f, 0.55f));
        text(a.x + 220.0f, pedalButton().y + 12.0f,
             "Rebinding the pedal frees a black key for something else.",
             nullptr);
    }

    /* ---- progressions ------------------------------------------------------
     *
     * A step sequencer for chords: each row is a section, each cell one beat,
     * and the sections chain A -> B -> C -> D and back. Built in the next
     * commit; this placeholder keeps the tab honest about being empty rather
     * than showing a broken grid.
     */
    void drawProgressions()
    {
        const Button a = slideArea();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(0.50f, 0.55f, 0.63f));
        text(a.x + a.w * 0.5f, a.y + a.h * 0.5f - 10.0f,
             "Progression sequencer - not built yet.", nullptr);

        fontSize(11.0f);
        fillColor(Color(0.38f, 0.42f, 0.50f));
        text(a.x + a.w * 0.5f, a.y + a.h * 0.5f + 12.0f,
             "Sections chain A to D, one cell per beat, synced to host tempo.",
             nullptr);
    }

    /*
     * A slide press is a wheel press.
     *
     * The DSP has no idea which screen is showing: a strip resolves to a cell
     * and sends the same "gesture" message the wheel does, so latch, glide,
     * the pedal, voice leading and the monitor all work here without a second
     * code path.
     *
     * What the section chooses - extension and octave - is pushed first, as
     * ordinary settings, so the gesture is built from them (spec section 5:
     * settings are read at the instant of the gesture and baked into MIDI).
     */
    bool slidePress(double px, double py)
    {
        int section;
        const int slide = hitSlide(px, py, section);
        if (slide < 0)
            return false;

        applySection(slide, section);

        const SlideDef* defs = slidesForScale(fScale);
        int  pos;
        Ring ring;
        cellForDegree(defs[slide].degree, fSelectedKey, pos, ring);

        const bool same = (fLatchEnabled &&
                           slide == fActiveSlide && section == fActiveSection);

        sendGesture("press", pos, ring);

        if (same) {
            fActiveSlide    = -1;
            fPointerSlide   = -1;
        } else {
            fActiveSlide    = slide;
            fActiveSection  = section;
            /* Remembered past the release, so the highlight knows this strip
             * was played by the pointer rather than by the keyboard. */
            fPointerSlide   = slide;
            fPointerSection = section;
        }

        fDragging       = true;
        fActivePosition = pos;
        fActiveRing     = ring;
        repaint();
        return true;
    }

    /* Crossing into another strip mid-drag is the glide gesture, exactly as on
     * the wheel - which is why the strips share borders rather than sitting
     * apart. */
    bool slideMotion(double px, double py)
    {
        int section;
        const int slide = hitSlide(px, py, section);
        if (slide < 0)
            return false;
        if (slide == fActiveSlide && section == fActiveSection)
            return false;

        /*
         * Moving WITHIN a column lands on the same cell, so a "move" gesture
         * would tell the DSP nothing changed and the chord would not
         * retrigger - even though the section genuinely changed it, to another
         * octave or another extension.
         *
         * So a same-column move is a fresh press: release what is sounding,
         * then start the new chord. A different column keeps "move", which is
         * what lets it glide.
         */
        const bool sameColumn = (slide == fActiveSlide);

        applySection(slide, section);

        const SlideDef* defs = slidesForScale(fScale);
        int  pos;
        Ring ring;
        cellForDegree(defs[slide].degree, fSelectedKey, pos, ring);

        fActiveSlide    = slide;
        fActiveSection  = section;
        fActivePosition = pos;
        fActiveRing     = ring;
        fPointerSlide   = slide;
        fPointerSection = section;

        /* One gesture, not a release followed by a press: the handoff to the
         * audio thread holds a single slot, so the second would overwrite the
         * first and the old chord would never stop. */
        sendGesture(sameColumn ? "retrigger" : "move", pos, ring);

        repaint();
        return true;
    }

    /*
     * Push the settings a section implies, before the gesture that reads them.
     *
     * The extension is per-ring in the engine, so the ring this slide lands on
     * is the one to set - which is also what keeps a strip's variation from
     * silently changing chords on another ring.
     */
    void applySection(int slide, int section)
    {
        const SlideDef* defs = slidesForScale(fScale);
        int  pos;
        Ring ring;
        cellForDegree(defs[slide].degree, fSelectedKey, pos, ring);

        Extension ext;
        int       octShift;
        sectionSettings(section, ext, octShift);

        char buf[16];
        char key[8];

        /*
         * Slide Mode keeps its OWN extensions and voicings.
         *
         * Writing the wheel's ext0/1/2 here was wrong: it silently rewrote
         * Circle Mode's per-ring settings, so the wheel's dropdowns went on
         * reading "None (triad)" while the DSP built sevenths - and a chord
         * started with one voice count and released with another, stranding
         * notes. The two screens are different instruments over one engine;
         * their settings must not leak into each other.
         *
         * The DSP still has one set of per-ring values, so whichever screen is
         * playing pushes its own before the gesture that reads them. That is
         * enough because only one screen can be played at a time, and it is
         * what keeps the DSP from needing to know screens exist at all.
         */
        if (fSlideRingExt[ring] != ext ||
            fPushedExtRing != static_cast<int>(ring) || fPushedExt != ext) {
            fSlideRingExt[ring] = ext;
            fPushedExtRing = static_cast<int>(ring);
            fPushedExt     = ext;
            std::snprintf(key, sizeof(key), "ext%d", static_cast<int>(ring));
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(ext));
            setState(key, buf);
        }

        /* Voicing likewise: Slide Mode has no voicing control, so it must
         * assert Regular rather than inherit whatever the wheel was set to.
         * Otherwise a wheel inversion would quietly re-voice the strips and
         * put the wrong note in the bass. */
        if (fPushedVoiceRing != static_cast<int>(ring)) {
            fPushedVoiceRing = static_cast<int>(ring);
            std::snprintf(key, sizeof(key), "voice%d", static_cast<int>(ring));
            setState(key, "0");
        }

        /* The slide's own octave shift (the last strip is the tonic an octave
         * up) plus whatever the section asks for. */
        const int oct = fOctave + defs[slide].octaveShift + octShift;
        if (oct != fPushedOctave) {
            fPushedOctave = oct;
            std::snprintf(buf, sizeof(buf), "%d", oct);
            setState("octave", buf);
        }
    }

    /*
     * Re-assert this screen's settings when it becomes the one being played.
     *
     * The DSP holds a single set of per-ring values, so the screen in front of
     * the user has to own them. Without this, switching tabs leaves the other
     * screen's extensions and voicings in force while this screen's controls
     * claim otherwise - which is exactly how the wheel came to build sevenths
     * while its dropdowns read "triad".
     */
    void assertScreenSettings()
    {
        char key[8];
        char buf[16];

        for (int r = 0; r < kRingCount; ++r) {
            const int ext = (fScreen == kScreenSlide)
                ? 0                                    /* strips re-push per press */
                : static_cast<int>(fRingExt[r]);
            const int voi = (fScreen == kScreenSlide)
                ? 0                                    /* strips are always Regular */
                : static_cast<int>(fRingVoice[r]);

            std::snprintf(key, sizeof(key), "ext%d", r);
            std::snprintf(buf, sizeof(buf), "%d", ext);
            setState(key, buf);

            std::snprintf(key, sizeof(key), "voice%d", r);
            std::snprintf(buf, sizeof(buf), "%d", voi);
            setState(key, buf);
        }

        /* Force the next slide press to re-push, since the values above just
         * changed underneath it. */
        fPushedExtRing   = -1;
        fPushedVoiceRing = -1;
        fPushedOctave    = -999;

        std::snprintf(buf, sizeof(buf), "%d", fOctave);
        setState("octave", buf);
    }

    /* Which slide and section a point falls in; slide -1 when outside. */
    int hitSlide(double px, double py, int& outSection) const
    {
        const int n = slideCountForScale(fScale);
        for (int i = 0; i < n; ++i) {
            for (int sec = 0; sec < sectionCount(); ++sec) {
                if (hit(sectionRect(i, sec), px, py)) {
                    outSection = sec;
                    return i;
                }
            }
        }
        return -1;
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
        /* "Legato" describes what it does - a selection sounds on until the
         * next one replaces it. The state key stays "latch": renaming it
         * would break every saved session for a label change. */
        drawButton(latchButton(),
                   fLatchEnabled ? "LEGATO: ON" : "LEGATO: OFF",
                   fLatchEnabled);
        drawButton(glideButton(), kGlideModeName[fGlideMode],
                   fGlideMode != kGlideOff);
        /* "Wedge", not "Key": the key itself always follows the selection now,
         * and only the highlight is pinned. Saying "Key: locked" would suggest
         * the keyboard was frozen too. */
        drawButton(keyLockButton(),
                   fKeyLocked ? "WEDGE: PINNED" : "WEDGE: FOLLOWS",
                   fKeyLocked);
        drawButton(singleNoteButton(),
                   fSingleNotes ? "SINGLE NOTES" : "CHORDS",
                   fSingleNotes);
        /*
         * Which chord tone is in the bass.
         *
         * The label reports the SETTING, not whether it happens to be acting
         * right now. Plain glide suspends voice leading for the duration - a
         * re-inversion cannot ride a single bend - but the setting is still
         * auto, and it resumes the moment glide goes off or to MPE. Labelling
         * that suspension as "FIRST" was a lie that cost real debugging time:
         * the button read FIRST while leading was quietly producing B-E-G for
         * an Em, and clicking it jumped to SECOND because the handler believed
         * its own label and skipped the state it claimed to already be in.
         *
         * "(HELD)" marks the suspension without pretending the setting changed.
         */
        drawButton(voiceLeadButton(),
                   fVoiceLeading
                       ? (fGlideMode == kGlideOn ? "ROOT: AUTO (HELD)"
                                                 : "ROOT: AUTO")
                       : kBassNoteName[fBassNote],
                   fVoiceLeading);

        /*
         * Row 2 differs by screen: the wheel wants per-ring extensions and
         * voicings, while the strips carry their variations in the sections
         * themselves and need the scale and key instead.
         */
        if (fScreen == kScreenSlide) {
            char buf[48];

            std::snprintf(buf, sizeof(buf), "SECTIONS: %s",
                          fSectionMode == kSectionOctave ? "OCTAVE"
                                                         : "VARIATION");
            drawDropdown(sectionModeButton(), buf, false);

            std::snprintf(buf, sizeof(buf), "%s", kScaleName[fScale]);
            drawDropdown(scaleButton(), buf,
                         fOpenMenu == kMenuScale);

            std::snprintf(buf, sizeof(buf), "KEY: %s",
                          kMajorLabel[fSelectedKey]);
            drawDropdown(slideKeyButton(), buf, fOpenMenu == kMenuKey);

            /* Say what the slider is doing, since it swaps with the sections. */
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(9.5f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(Color(0.50f, 0.55f, 0.63f));
            text(434.0f, kDropY + kDropH * 0.5f,
                 fSectionMode == kSectionOctave ? "slider: variation"
                                                : "slider: octave",
                 nullptr);
            return;
        }

        /* Per-ring dropdowns, labelled by ring so the mapping is unambiguous. */
        static const char* const kRingTag[kRingCount] = { "MAJ", "MIN", "DIM" };

        for (int r = 0; r < kRingCount; ++r) {
            char ext[48], voi[48];
            /* Short form: "None (triad)" did not fit once the tag went
             * uppercase, and a clipped label is worse than a terse one. */
            static const char* const kExtShort[kExtCount] = {
                "TRIAD", "6TH", "7TH", "9TH", "ADD9", "SUS2", "SUS4"
            };
            std::snprintf(ext, sizeof(ext), "%s: %s",
                          kRingTag[r], kExtShort[fRingExt[r]]);
            /* Uppercased to match the row; the menu keeps the full names,
             * where there is room for them. */
            std::snprintf(voi, sizeof(voi), "%s", kVoicingName[fRingVoice[r]]);
            for (char* p = voi; *p != '\0'; ++p)
                if (*p >= 'a' && *p <= 'z') *p = static_cast<char>(*p - 32);

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

        int    rows;
        Button anchor;
        int    cur;
        menuShape(rows, anchor, cur);

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
            text(row.x + 7.0f, row.y + row.h * 0.5f, menuRowLabel(i), nullptr);
        }
    }

    /* Rows, anchor and current selection for whichever menu is open - one
     * place, so drawing and hit-testing cannot disagree about the shape. */
    void menuShape(int& rows, Button& anchor, int& cur) const
    {
        switch (fOpenMenu) {
            case kMenuVoicing:
                rows   = static_cast<int>(kVoicingCount);
                anchor = voiceButton(fOpenMenuRing);
                cur    = static_cast<int>(fRingVoice[fOpenMenuRing]);
                break;
            case kMenuScale:
                rows   = static_cast<int>(kScaleCount);
                anchor = scaleButton();
                cur    = static_cast<int>(fScale);
                break;
            case kMenuKey:
                rows   = 12;
                anchor = slideKeyButton();
                cur    = fSelectedKey;
                break;
            case kMenuBinding:
                rows   = kBindingRows;
                /* Hangs off the key being edited, so the list points at what
                 * it will change. */
                anchor = keyRect(fEditKey);
                cur    = bindingRowFor(fEditKey);
                break;
            case kMenuPedal:
                rows   = static_cast<int>(kPedalActionCount);
                anchor = pedalButton();
                cur    = static_cast<int>(fPedalAction);
                break;
            default:   /* kMenuExt */
                rows   = static_cast<int>(kExtCount);
                anchor = extButton(fOpenMenuRing);
                cur    = static_cast<int>(fRingExt[fOpenMenuRing]);
                break;
        }
    }

    const char* menuRowLabel(int i) const
    {
        switch (fOpenMenu) {
            case kMenuVoicing: return kVoicingName[i];
            case kMenuScale:   return kScaleName[i];
            case kMenuKey:     return kMajorLabel[i];
            case kMenuBinding: return bindingRowLabel(i);
            case kMenuPedal:   return kPedalActionName[i];
            default:           return kExtensionName[i];
        }
    }

    /* Returns true when a control consumed the click. */
    bool handleControlClick(double px, double py)
    {
        char buf[16];

        /* An open menu swallows clicks first, so a row cannot fall through to
         * the wheel underneath it. */
        if (fOpenMenu != kMenuNone) {
            int    rows;
            Button anchor;
            int    cur;
            menuShape(rows, anchor, cur);

            for (int i = 0; i < rows; ++i) {
                if (hit(menuRow(anchor, i), px, py)) {
                    std::snprintf(buf, sizeof(buf), "%d", i);

                    switch (fOpenMenu) {
                        case kMenuVoicing: {
                            fRingVoice[fOpenMenuRing] = static_cast<Voicing>(i);
                            char key[8];
                            std::snprintf(key, sizeof(key), "voice%d",
                                          fOpenMenuRing);
                            setState(key, buf);
                            break;
                        }
                        case kMenuScale:
                            fScale = static_cast<Scale>(i);
                            /* Fewer slides may leave the highlight past the
                             * end of the bank. */
                            fActiveSlide  = -1;
                            fPointerSlide = -1;
                            break;
                        case kMenuKey:
                            selectKey(i);
                            break;
                        case kMenuBinding: {
                            KeyAction a;
                            int       v;
                            bindingForRow(i, a, v);
                            fKeyMap[fEditKey].action = a;
                            fKeyMap[fEditKey].value  = v;
                            pushKeyMap();
                            break;
                        }
                        case kMenuPedal:
                            fPedalAction = static_cast<PedalAction>(i);
                            setState("pedalAction", buf);
                            break;
                        default: {   /* kMenuExt */
                            fRingExt[fOpenMenuRing] = static_cast<Extension>(i);
                            char key[8];
                            std::snprintf(key, sizeof(key), "ext%d",
                                          fOpenMenuRing);
                            setState(key, buf);
                            break;
                        }
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

        /* Tabs first: they sit above every other control. */
        for (int i = 0; i < 2; ++i) {
            if (hit(tabButton(i), px, py)) {
                if (static_cast<int>(fScreen) == i) { repaint(); return true; }

                /* Anything still sounding was built with the OTHER screen's
                 * settings; leaving it running while those change is how a
                 * chord ends up released with the wrong voice count. */
                setState("panic", "1");

                fScreen = static_cast<Screen>(i);
                /* A highlight from the other screen would be a lie here. */
                fActivePosition = -1;
                fActiveSlide    = -1;
                fPointerSlide   = -1;
                fOpenMenu       = kMenuNone;
                fLastChord.clear();

                assertScreenSettings();
                repaint();
                return true;
            }
        }

        if (fScreen == kScreenKeys) {
            if (hit(pedalButton(), px, py)) {
                fOpenMenu = kMenuPedal;
                repaint();
                return true;
            }
            const int pc = hitKey(px, py);
            if (pc >= 0) {
                fEditKey  = pc;
                fOpenMenu = kMenuBinding;
                repaint();
                return true;
            }
        }

        if (fScreen == kScreenSlide) {
            if (hit(sectionModeButton(), px, py)) {
                fSectionMode = (fSectionMode == kSectionOctave)
                    ? kSectionVariation : kSectionOctave;
                fActiveSlide  = -1;
                fPointerSlide = -1;
                repaint();
                return true;
            }
            if (hit(scaleButton(), px, py)) {
                fOpenMenu = kMenuScale;
                repaint();
                return true;
            }
            if (hit(slideKeyButton(), px, py)) {
                fOpenMenu = kMenuKey;
                repaint();
                return true;
            }
        }

        /* Octave slider: takes the press and keeps receiving motion, so it can
         * be dragged through the octaves while notes sound. */
        if (fScreen != kScreenKeys && hit(octaveSlider(), px, py)) {
            fSliderDrag = true;
            sliderTo(py);
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

        if (hit(rollHeader(), px, py)) {
            fRollOpen = ! fRollOpen;
            repaint();
            return true;
        }

        /* Row 1 is not drawn on the setup screen, so it must not be clickable
         * there either - the monitor and panic above stay live, since those
         * are useful from any screen. */
        if (fScreen == kScreenKeys)
            return false;

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

        /* Key lock pins the highlighted wedge, and nothing else. Engaging it
         * captures whatever key is selected now, so the wedge freezes where the
         * user is looking rather than at some earlier key. The keyboard keeps
         * following the selection either way. */
        if (hit(keyLockButton(), px, py)) {
            if (! fKeyLocked)
                fLockedKey = fSelectedKey;
            fKeyLocked = ! fKeyLocked;
            repaint();
            return true;
        }

        if (hit(singleNoteButton(), px, py)) {
            fSingleNotes = ! fSingleNotes;
            std::snprintf(buf, sizeof(buf), "%d", fSingleNotes ? 1 : 0);
            setState("singleNotes", buf);
            /* The next log line is formatted differently now, so the cached
             * one must not suppress it as a duplicate. */
            fLastChord.clear();
            repaint();
            return true;
        }

        if (hit(voiceLeadButton(), px, py)) {
            /*
             * Cycles auto -> first -> second -> third -> auto.
             *
             * "auto" is voice leading choosing the inversion itself. Taking
             * manual control has to switch it off, or the chosen bass would be
             * silently ignored - which is exactly how the old toggle appeared
             * to do nothing while glide had leading suppressed anyway.
             */
            /*
             * Cycle on the SETTING, not on whether leading is currently in
             * force. Keying off the latter meant that with glide on - where
             * leading is suspended - a button reading "FIRST" would advance to
             * SECOND, so FIRST was unreachable by clicking.
             */
            if (fVoiceLeading) {
                fVoiceLeading = false;
                fBassNote     = kBassFirst;
            } else if (fBassNote == kBassNoteCount - 1) {
                fVoiceLeading = true;          /* wrap back to auto */
                fBassNote     = kBassFirst;
            } else {
                fVoiceLeading = false;
                fBassNote     = static_cast<BassNote>(fBassNote + 1);
            }

            std::snprintf(buf, sizeof(buf), "%d", fVoiceLeading ? 1 : 0);
            setState("voiceLeading", buf);
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(fBassNote));
            setState("bassNote", buf);
            repaint();
            return true;
        }

        /* Only on the wheel screen: these buttons are not drawn in Slide Mode,
         * and an invisible hit box would swallow clicks meant for the strips. */
        if (fScreen == kScreenCircle) {
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
                /*
                 * A note-on with velocity 0 IS a note-off - the running-status
                 * convention every MIDI device uses. Counting it as a note-on
                 * left the pitch marked sounding forever, so the chord view
                 * reported voices that had already stopped: the phantom
                 * "(2 notes)" after a chord was released. The notes were never
                 * stuck; the monitor's bookkeeping was.
                 *
                 * The DSP's own input handler has always decoded it this way;
                 * only the monitor disagreed.
                 */
                if (c == 0) {
                    noteOffForChord(b);
                    std::snprintf(line, sizeof(line), "NoteOff  %-4s(%3d) (vel 0)",
                                  noteName(b), b);
                } else {
                    noteOnForChord(b);
                    std::snprintf(line, sizeof(line), "NoteOn   %-4s(%3d) vel %d",
                                  noteName(b), b, c);
                }
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

        char line[128];

        /*
         * In single-note mode the plugin emits one note per press, so there is
         * no chord to name. Naming one anyway would invent harmony the user
         * did not play: two single notes held together are still two notes,
         * and calling them "C5" misreports what the wheel actually did.
         *
         * The notes are still listed - the monitor's job is to say what is
         * sounding, and several single notes genuinely can sound at once.
         */
        if (fSingleNotes) {
            std::snprintf(line, sizeof(line), "NOTES    %-9s %s",
                          fSoundingCount == 1 ? "" : "(held)", notes);
        } else {
            char name[32] = {0};
            nameChord(fSounding, name, sizeof(name));
            std::snprintf(line, sizeof(line), "CHORD    %-9s %s", name, notes);
        }

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

    /* ---- piano roll --------------------------------------------------------
     *
     * What the chord algorithms actually PRODUCED, laid out on a keyboard, as
     * opposed to the log's stream of individual events. Reading five note-ons
     * and assembling them in your head is exactly the work the monitor should
     * be doing - and seeing the notes in pitch order is what makes a voicing
     * or an inversion obvious at a glance.
     *
     * It reads fSounding, the same set the chord namer uses, so the roll and
     * the CHORD line can never disagree about what is playing.
     */

    /* C1 to C7 - six octaves, which covers everything the octave slider and a
     * controller keyboard can reach without wasting width on the extremes. */
    static constexpr int kRollLow  = 24;    /* C1 */
    static constexpr int kRollHigh = 96;    /* C7 */

    static int rollWhiteCount()
    {
        int n = 0;
        for (int m = kRollLow; m <= kRollHigh; ++m)
            if (! isBlackKey(m % 12)) ++n;
        return n;
    }

    /* White-key ordinal of a note within the displayed range. */
    static int rollWhiteIndex(int midi)
    {
        int n = 0;
        for (int m = kRollLow; m < midi; ++m)
            if (! isBlackKey(m % 12)) ++n;
        return n;
    }

    Button rollArea() const
    {
        return { 0.0f, rollBodyY(), static_cast<float>(getWidth()), kRollH };
    }

    void drawPianoRoll()
    {
        if (! fRollOpen)
            return;

        const Button a = rollArea();

        beginPath();
        rect(a.x, a.y, a.w, a.h);
        fillColor(Color(0.07f, 0.08f, 0.10f));
        fill();

        const float pad = 8.0f;
        const float kw  = (a.w - pad * 2.0f) / rollWhiteCount();
        const float top = a.y + 5.0f;
        /* Leaves room under the keys for the octave labels. */
        const float kh  = a.h - 22.0f;

        /* Whites first, then blacks over them, as on a real keyboard. */
        for (int m = kRollLow; m <= kRollHigh; ++m) {
            if (isBlackKey(m % 12))
                continue;

            const float x = a.x + pad + rollWhiteIndex(m) * kw;
            const bool  on = fSounding[m];

            beginPath();
            rect(x, top, kw - 1.0f, kh);
            fillColor(on ? Color(0.42f, 0.86f, 0.55f)
                         : Color(0.82f, 0.85f, 0.90f));
            fill();
            strokeColor(Color(0.10f, 0.11f, 0.14f));
            strokeWidth(1.0f);
            stroke();

            /* Label the Cs, so the octave is readable without counting. Drawn
             * under the keys rather than on them: a white key is about 9px
             * wide here, which is narrower than the text. */
            if (m % 12 == 0) {
                char lbl[8];
                std::snprintf(lbl, sizeof(lbl), "C%d", (m / 12) - 1);
                fontFace(NANOVG_DEJAVU_SANS_TTF);
                fontSize(8.5f);
                textAlign(ALIGN_CENTER | ALIGN_TOP);
                fillColor(Color(0.45f, 0.49f, 0.56f));
                text(x + kw * 0.5f, top + kh + 1.0f, lbl, nullptr);
            }
        }

        for (int m = kRollLow; m <= kRollHigh; ++m) {
            if (! isBlackKey(m % 12))
                continue;

            /* Sits over the boundary between its neighbouring whites. */
            const float x = a.x + pad + rollWhiteIndex(m) * kw - kw * 0.30f;
            const bool  on = fSounding[m];

            beginPath();
            rect(x, top, kw * 0.60f, kh * 0.62f);
            fillColor(on ? Color(0.30f, 0.72f, 0.42f)
                         : Color(0.14f, 0.15f, 0.19f));
            fill();
            strokeColor(Color(0.07f, 0.08f, 0.10f));
            strokeWidth(1.0f);
            stroke();
        }

        /* Count on the right, so "how many voices" needs no counting either -
         * it is the quickest check that a chord has the size it should. */
        if (fSoundingCount > 0) {
            char n[24];
            std::snprintf(n, sizeof(n), "%d note%s", fSoundingCount,
                          fSoundingCount == 1 ? "" : "s");
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(9.5f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(Color(0.50f, 0.78f, 0.58f));
            text(a.x + a.w - 8.0f, a.y - 1.0f, n, nullptr);
        }
    }

    /* The header bar is always present; the log below it only when expanded. */
    Button monitorHeader() const
    {
        return { 0.0f, logHeaderY(), static_cast<float>(getWidth()), kHeaderH };
    }

    Button rollHeader() const
    {
        return { 0.0f, rollHeaderY(), static_cast<float>(getWidth()), kHeaderH };
    }

    /* Panic lives on the bottom-most header, so it stays in one place
     * regardless of which panels happen to be open. */
    Button panicButton() const
    {
        return { getWidth() - 62.0f, rollHeaderY() + 3.0f, 54.0f,
                 kHeaderH - 6.0f };
    }

    /* One collapsible header bar. Both panels use it, so they cannot drift
     * apart visually. */
    void drawPanelHeader(const Button& hdr, const char* title, bool open,
                         const char* hint)
    {
        beginPath();
        rect(hdr.x, hdr.y, hdr.w, hdr.h);
        fillColor(Color(0.12f, 0.13f, 0.17f));
        fill();

        beginPath();
        moveTo(hdr.x, hdr.y);
        lineTo(hdr.x + hdr.w, hdr.y);
        strokeColor(Color(0.26f, 0.28f, 0.34f));
        strokeWidth(1.0f);
        stroke();

        char text_[96];
        std::snprintf(text_, sizeof(text_), "%s  %s%s",
                      open ? "▼" : "▶", title, open ? "" : hint);

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(0.72f, 0.76f, 0.83f));
        text(10.0f, hdr.y + hdr.h * 0.5f, text_, nullptr);
    }

    void drawMonitor()
    {
        const float w = getWidth();
        const float logH = fMonitorOpen ? kLogLines * 14.0f + 10.0f : 0.0f;
        const float top  = logBodyY();

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

        drawPanelHeader(monitorHeader(), "Event log", fMonitorOpen,
                        "  (click to expand)");

        /* The keyboard sits below the log, and opens independently - seeing
         * what the chords produced should not require the text stream. */
        drawPianoRoll();
        drawPanelHeader(rollHeader(), "Notes", fRollOpen,
                        "  (click to expand)");

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
            /* The plugin registers both at construction; look them up once. */
            void* inst = getPluginInstancePointer();
            fRing  = monitorRingFor(inst);
            fCells = activeCellsFor(inst);
            if (fRing == nullptr)
                return;
        }

        /*
         * Poll the highlight regardless of the monitor, and BEFORE the early
         * return below - notes played from a MIDI keyboard must light the UI
         * whether or not the log panel happens to be expanded.
         *
         * Only repaint when the set actually changes, so an idle plugin costs
         * nothing: uiIdle runs at frame rate, and repainting every tick would
         * burn a core for no reason.
         */
        if (fCells != nullptr) {
            /* Compared word for word rather than hashed: there are only three,
             * and a hash collision would silently drop a highlight change. */
            bool changed = false;
            for (int r = 0; r < kRingCount; ++r) {
                const uint32_t now =
                    fCells->ring[r].load(std::memory_order_acquire);
                if (now != fCellsSeen[r]) {
                    fCellsSeen[r] = now;
                    changed = true;
                }
            }
            if (changed)
                repaint();
        }

        /*
         * Drain while EITHER panel is open. The piano roll is built from the
         * same note stream as the log, so gating on the log alone would leave
         * the keyboard frozen whenever the text stream was collapsed - which
         * is exactly the combination the roll exists to support.
         *
         * With both closed nothing is drained, so a collapsed monitor still
         * cannot grow an unbounded backlog. The ring drops its own overflow.
         */
        if (! fMonitorOpen && ! fRollOpen)
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
    /* The keyboard opens by default: it answers "what did the chord produce?"
     * at a glance, which is the question the monitor is usually opened for. */
    bool         fRollOpen    = true;
    MonitorRing* fRing        = nullptr;

    /* What the DSP is sounding, for the highlight, plus the last state seen so
     * an idle plugin does not repaint every frame. */
    ActiveCells* fCells = nullptr;
    uint32_t     fCellsSeen[kRingCount] = { 0, 0, 0 };

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
    enum OpenMenu {
        kMenuNone = 0, kMenuExt, kMenuVoicing, kMenuScale, kMenuKey,
        kMenuBinding,   /* what a keyboard key does */
        kMenuPedal
    };

    /*
     * The binding menu is one flat list covering every action a key can take,
     * because a player thinks "what should this key do?" rather than "which
     * category, then which member". Degrees and extensions are enumerated
     * inline; the toggles need no argument.
     */
    static constexpr int kBindingRows =
        1 +                                  /* silent */
        static_cast<int>(kDegreeCount) +     /* I..vii */
        static_cast<int>(kExtCount) +        /* triad..sus4 */
        4;                                   /* glide, latch, single, panic */

    /* Decode a row into the binding it sets. */
    static void bindingForRow(int row, KeyAction& outAction, int& outValue)
    {
        if (row == 0) { outAction = kKeyNone; outValue = 0; return; }
        --row;

        if (row < static_cast<int>(kDegreeCount)) {
            outAction = kKeyDegree; outValue = row; return;
        }
        row -= static_cast<int>(kDegreeCount);

        if (row < static_cast<int>(kExtCount)) {
            outAction = kKeyExtension; outValue = row; return;
        }
        row -= static_cast<int>(kExtCount);

        switch (row) {
            case 0:  outAction = kKeyGlideToggle;  break;
            case 1:  outAction = kKeyLatchToggle;  break;
            case 2:  outAction = kKeySingleToggle; break;
            default: outAction = kKeyPanic;        break;
        }
        outValue = 0;
    }

    static const char* bindingRowLabel(int row)
    {
        static char buf[48];
        KeyAction a;
        int       v;
        bindingForRow(row, a, v);

        switch (a) {
            case kKeyDegree:
                std::snprintf(buf, sizeof(buf), "Degree  %s",
                              kDegreeCell[v].numeral);
                break;
            case kKeyExtension:
                std::snprintf(buf, sizeof(buf), "Chord   %s",
                              kExtensionName[v]);
                break;
            default:
                std::snprintf(buf, sizeof(buf), "%s", kKeyActionName[a]);
                break;
        }
        return buf;
    }

    /* Which row a key's current binding corresponds to, for the highlight. */
    int bindingRowFor(int pc) const
    {
        const KeyMapEntry& e = fKeyMap[pc];
        for (int r = 0; r < kBindingRows; ++r) {
            KeyAction a;
            int       v;
            bindingForRow(r, a, v);
            if (a != e.action)
                continue;
            if (a == kKeyDegree || a == kKeyExtension) {
                if (v == e.value) return r;
            } else {
                return r;
            }
        }
        return 0;
    }
    OpenMenu fOpenMenu     = kMenuNone;
    int      fOpenMenuRing = 0;

    /* When locked, the highlighted wedge stays where it was pinned - the
     * chord still sounds and the keyboard still follows the selection. */
    bool fKeyLocked = false;
    int  fLockedKey = 0;

    /* Bypass chord generation and sound the root alone. */
    bool fSingleNotes = false;

    /* On by default: without it a progression leaps about, because every chord
     * stacks upward from its own root. */
    bool     fVoiceLeading = true;
    BassNote fBassNote     = kBassFirst;

    /* Octave for pointer and touch input. Mirrors the DSP's setting; a played
     * MIDI note carries its own octave instead. */
    int  fOctave = 4;

    /* A drag that started on the octave slider, kept separate from the wheel's
     * drag so the two gestures cannot interfere. */
    bool fSliderDrag = false;

    /* ---- slide mode state -------------------------------------------------- */

    Screen      fScreen      = kScreenCircle;
    Scale       fScale       = kScaleDiatonic;
    SectionMode fSectionMode = kSectionOctave;

    int fActiveSlide   = -1;
    int fActiveSection = 0;

    /* The strip and section the POINTER last played, kept past the release.
     * fActiveSlide is cleared as soon as the drag ends, which leaves a window
     * where the DSP still reports the cell sounding and the highlight cannot
     * tell a pointer press from a keyboard note - that window is what lit the
     * whole stack. */
    int fPointerSlide   = -1;
    int fPointerSection = 0;

    /* First visible variation row, so all seven extensions are reachable
     * while only four are on screen. */
    int fVariationScroll = 0;

    /* The variation the left slider selects, when the sections are doing
     * octaves instead. */
    int fSliderVariation = 0;

    /* Last octave pushed to the DSP, so a drag across strips does not resend
     * an unchanged value on every cell. */
    int fPushedOctave = 4;

    /* Slide Mode's own extensions, kept apart from the wheel's fRingExt so
     * neither screen can rewrite the other's settings behind its back. */
    Extension fSlideRingExt[kRingCount] = { kExtNone, kExtNone, kExtNone };

    /* What was last pushed for the ring currently being played, so a drag does
     * not resend unchanged values - and so a tab switch can force a resend by
     * clearing them. */
    int fPushedExtRing   = -1;
    int fPushedVoiceRing = -1;
    Extension fPushedExt = kExtNone;

    /* ---- keyboard setup state ---------------------------------------------- */

    /* The UI's copy of the bindings. The DSP holds its own, kept in step by
     * pushKeyMap(); this one exists so the editor can draw without asking. */
    KeyMapEntry fKeyMap[12];
    PedalAction fPedalAction = kPedalSustain;

    /* Which key the binding menu is editing. */
    int fEditKey = 1;   /* C# */

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FortyFifthUI)
};

UI* createUI()
{
    return new FortyFifthUI();
}

END_NAMESPACE_DISTRHO
