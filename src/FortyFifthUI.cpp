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

#if FORTYFIFTH_MIDI_MONITOR
        drawMonitor();
#endif
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
            Ring ring;
            const int pos = hitTest(ev.pos.getX(), ev.pos.getY(), ring);
            if (pos < 0)
                return false;

            fActivePosition = pos;
            fActiveRing     = ring;
            fDragging       = true;
            sendGesture("press", pos, ring);
            repaint();
            return true;
        }

        if (fDragging) {
            fDragging = false;
            sendGesture("release", fActivePosition, fActiveRing);
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

    static constexpr size_t kLogLines = 14;
    std::deque<std::string> fLog;
#endif /* FORTYFIFTH_MIDI_MONITOR */

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
