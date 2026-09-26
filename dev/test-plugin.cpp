/*
 * The real plugin, driven the way a host drives it.
 *
 * This is the suite the refactor plan said did not exist. Section 5 of
 * docs/glide-refactor-plan.md listed six behaviours with "no automated guard,
 * by ear only" - the three glide modes, the four glide callers, snap-and-reset,
 * ownership across the snap, refused-note-off recovery, and the
 * latch/pedal/panic/merge interactions. All six are asserted here.
 *
 * The difference from every other suite: nothing is transcribed. dev/harness.hpp
 * supplies the DPF base-class bodies, FortyFifthPlugin.cpp is compiled in
 * whole, and the tests poke it through setState() and run() exactly as a DAW
 * would - then read back the MIDI it actually emitted. When a test here fails,
 * the plugin is wrong; there is no model that could be wrong instead.
 *
 * That matters most for the refactor now under way. Steps 3-6 move glide state
 * into its own object and change how a gliding group is identified. A
 * transcribed model would keep passing throughout, because it transcribes the
 * OLD structure. These tests do not care about structure at all - only about
 * what reaches the wire, which is the one thing that must not change.
 */
#include "harness.hpp"

/* The harness's storage. Defined once, here. */
namespace harness {
std::vector<Sent> sent;
int    refuseAfter = -1;
int    refused     = 0;
double sampleRate  = 48000.0;
}

/* The plugin itself, compiled in. */
#include "../src/FortyFifthPlugin.cpp"

#include <cstdio>
#include <cstring>

using namespace DISTRHO;
using namespace fortyfifth;

static int failures = 0;

static void ok(const char* what, bool cond, const char* detail = "")
{
    if (cond) {
        std::printf("  ok    %-54s %s\n", what, detail);
    } else {
        std::printf("  FAIL  %-54s %s\n", what, detail);
        ++failures;
    }
}

static void eq(const char* what, int got, int want)
{
    const bool good = (got == want);
    if (! good) ++failures;
    std::printf("  %s  %-54s got %-4d want %d\n",
                good ? "ok  " : "FAIL", what, got, want);
}

/* ------------------------------------------------------------------------ */
/*
 * A host, near enough.
 *
 * Subclasses the plugin so run() and setState() - protected, as DPF declares
 * them - can be called. Nothing is reimplemented: every call goes straight into
 * the shipping code.
 */
class Host : public FortyFifthPlugin
{
public:
    Host()
    {
        activate();
        harness::reset();
    }

    /* One processing block, with optional incoming MIDI. */
    void block(uint32_t frames = 64,
               const MidiEvent* events = nullptr, uint32_t count = 0)
    {
        run(nullptr, nullptr, frames, events, count);
    }

    void set(const char* key, const char* value) { setState(key, value); }

    /* What the host would SAVE for this key - protected in DPF, so surfaced here
     * the same way run() and setState() are. */
    String get(const char* key) const { return getState(key); }

    void set(const char* key, int value)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d", value);
        setState(key, buf);
    }

    /*
     * Give ONE cell its own chord type, the way the editor does.
     *
     * This is the interface that matters: the editor sends the whole per-cell
     * table, and every source - pointer, keyboard, slide, sequencer - resolves a
     * cell from it. Setting `ext<N>` instead would exercise the ring-wide
     * default, which is the old path and is precisely what the reported bug was.
     */
    void setCellExt(Ring ring, int position, Extension e)
    {
        fEdit.setExt(ring, position, static_cast<int8_t>(e));
        pushCells();
    }

    void setCellVoicing(Ring ring, int position, Voicing v)
    {
        fEdit.setVoicing(ring, position, static_cast<int8_t>(v));
        pushCells();
    }

    void pushCells()
    {
        char out[kCellSettingsStringMax];
        encodeCellSettings(fEdit, out, sizeof out);
        setState("cellSettings", out);
    }

    /* The editor's copy of the table, mirrored so a test can build one up. */
    CellSettings fEdit;

    /* A gesture, in the plugin's own wire format. */
    void gesture(const char* verb, int position, int ring, int octShift = 0)
    {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s:%d:%d:%d",
                      verb, position, ring, octShift);
        setState("gesture", buf);
    }

    /* A key down / up on the MIDI input, which is a real event, not a gesture. */
    void keyDown(uint8_t note, uint8_t vel = 100) { midi(0x90, note, vel); }
    void keyUp(uint8_t note)                      { midi(0x80, note, 0); }
    void pedal(bool down)                         { midi(0xB0, 64, down ? 127 : 0); }

    void midi(uint8_t status, uint8_t d1, uint8_t d2, uint32_t frames = 64)
    {
        MidiEvent ev;
        ev.frame   = 0;
        ev.size    = 3;
        ev.data[0] = status;
        ev.data[1] = d1;
        ev.data[2] = d2;
        ev.data[3] = 0;
        ev.dataExt = nullptr;
        block(frames, &ev, 1);
    }

    /* Let a glide run to completion. The default glide time is 120ms, so a
     * second of blocks is comfortably past the snap. */
    void settle(int blocks = 20) { for (int i = 0; i < blocks; ++i) block(4096); }

    /* How many groups are still alive, and how many pitches the refcount thinks
     * are held. Reaching into the plugin's own state is the point: these are the
     * invariants that the emitted stream cannot show directly. */
    int activeGroups() const
    {
        int n = 0;
        for (int i = 0; i < kMaxGroups; ++i) if (fGroup[i].active) ++n;
        return n;
    }

    int heldRefs() const
    {
        int n = 0;
        for (int i = 0; i < 128; ++i) n += fHeld[i];
        return n;
    }

    /*
     * How many cells are lit.
     *
     * Each ring is scanned only over ITS OWN segment count. isOn() wraps its
     * position modulo that count - deliberately, so a caller cannot index past
     * the bitfield - which means sweeping a fixed 0..31 would report the same
     * lit cell several times over. A first version of this did exactly that and
     * "exactly one cell is lit" failed while the plugin was behaving correctly.
     */
    int litCells() const
    {
        int n = 0;
        for (int r = 0; r < kRingCount; ++r) {
            const Ring ring = static_cast<Ring>(r);
            for (int p = 0; p < segmentsInRing(ring); ++p)
                if (fCells.isOn(ring, p)) ++n;
        }
        return n;
    }

    /*
     * WHICH cell is lit, packed as (ring << 8) | position, or -1 for none.
     *
     * Counting lit cells was not enough, and the gap was exactly where the
     * reported highlight bugs lived: after a glide exactly one cell is lit
     * either way - it is simply the WRONG one, the cell the phrase began on
     * rather than the one it reached. A count cannot tell those apart.
     *
     * Returns the first, which is unambiguous wherever the tests assert on it
     * because they check litCells() == 1 alongside.
     */
    int litCell() const
    {
        for (int r = 0; r < kRingCount; ++r) {
            const Ring ring = static_cast<Ring>(r);
            for (int p = 0; p < segmentsInRing(ring); ++p)
                if (fCells.isOn(ring, p))
                    return (r << 8) | p;
        }
        return -1;
    }
};

/* Set a glide mode by its state key, so the test drives it as the UI would. */
static void setGlideMode(Host& h, GlideMode m) { h.set("glideMode", static_cast<int>(m)); }

/* ------------------------------------------------------------------------ */

int main()
{
    /* ---- 1.2: all four glide callers, and 1.1: every mode ------------- */

    std::printf("=== a press sounds a chord, a release silences it ===\n");
    {
        Host h;
        h.gesture("press", 0, 0);
        h.block();
        const int on = harness::countOf(&Sent::isNoteOn);
        ok("a press emits note-ons", on >= 3, "");
        ok("and lights exactly one cell", h.litCells() == 1, "");

        h.gesture("release", 0, 0);
        h.block();
        eq("nothing is left sounding", harness::soundingCount(), 0);
        eq("no group survives", h.activeGroups(), 0);
        eq("no refcount survives", h.heldRefs(), 0);
        eq("and no cell stays lit", h.litCells(), 0);
    }

    std::printf("\n=== every glide mode sounds and stops cleanly ===\n");
    {
        const GlideMode modes[2] = { kGlideOff, kGlideMpe };
        const char* names[2] = { "off", "mpe" };

        for (int m = 0; m < 2; ++m) {
            Host h;
            setGlideMode(h, modes[m]);

            h.gesture("press", 0, 0);
            h.block();
            const int sounding = harness::soundingCount();

            h.gesture("move", 4, 0);    /* drag to another cell */
            h.settle();

            h.gesture("release", 4, 0);
            h.settle();

            char d[80];
            std::snprintf(d, sizeof d, "%s: %d sounded, %d refs after",
                          names[m], sounding, h.heldRefs());
            ok("a drag and release leaves silence",
               harness::soundingCount() == 0 && h.heldRefs() == 0 &&
               h.activeGroups() == 0, d);
        }
    }

    /* ---- 1.4 and 1.5: the snap, and the cell it lands on -------------- */

    std::printf("\n=== the snap leaves real pitches and the landed cell lit ===\n");
    {
        Host h;
        setGlideMode(h, kGlideMpe);

        h.gesture("press", 0, 0);
        h.block();

        harness::reset();
        h.gesture("move", 4, 0);
        h.settle();

        /* Snap-and-reset: the bend must return to centre, or a recorded clip
         * holds permanently bent pitches. */
        bool bendCentred = false;
        for (size_t i = harness::sent.size(); i-- > 0; ) {
            if (harness::sent[i].isBend()) {
                bendCentred = (harness::sent[i].d1 == 0x00 &&
                               harness::sent[i].d2 == 0x40);
                break;
            }
        }
        ok("the bend is zeroed after landing", bendCentred, "");
        ok("exactly one cell is lit after the glide", h.litCells() == 1, "");

        /*
         * And it is the cell the glide REACHED, not the one it left.
         *
         * This is the assertion that was missing, and the gap it left is exactly
         * where the reported highlight faults lived - "errant highlighting", then
         * "still doesn't work right" after a fix that looked correct. A count
         * cannot tell the two apart, because one cell is lit either way.
         *
         * The mechanism is in landGlide(): startGroup() derives the lit cell from
         * the SOURCE it is handed, which is still the one the phrase began on - so
         * the rebuild relights the starting cell moments after the glide moved the
         * highlight, undoing it. The landed cell has to be carried across
         * deliberately.
         */
        const int reached = (static_cast<int>(kRingKey) << 8) | 4;
        char cd[80];
        std::snprintf(cd, sizeof cd, "lit 0x%03X, dragged to 0x%03X",
                      h.litCell(), reached);
        ok("  and it is the cell the drag REACHED", h.litCell() == reached, cd);

        h.gesture("release", 4, 0);
        h.settle();
        eq("and the release still silences everything",
           harness::soundingCount(), 0);
        eq("  with no lit cell left", h.litCells(), 0);
    }

    /*
     * A glide lands when its time is up - no sooner, and no later.
     *
     * Nothing asserted the TIMING before, only the destination, and that gap let
     * a real fault through: while migrating the three start sequences into
     * Glide::begin(), a substitution left a SECOND begin() call after the MPE
     * targets were computed. It reset fElapsed, so every octave glide restarted
     * its ramp and took twice as long to arrive. Correct destination, correct
     * pitches, no compiler complaint - and the whole suite passed.
     *
     * The bend is the observable: it ramps while travelling and returns to centre
     * once the snap has happened. So "has it landed?" is answerable from the wire
     * alone, which is the only place this suite looks.
     */
    std::printf("\n=== a glide lands on time, not late ===\n");
    {
        /* 120ms at 48k is 5760 frames. Blocks of 1024 put the landing between
         * the fifth and sixth, comfortably clear of the boundary either side. */
        const uint32_t kBlock = 1024;

        Host h;
        setGlideMode(h, kGlideMpe);
        h.set("glideTimeMs", 120);   /* the state key's real name, not glideTime */

        h.gesture("press", 0, 0);
        h.block(kBlock);

        harness::reset();
        h.gesture("move", 4, 0);

        /* Step until the bend comes back to centre, which is the snap. */
        int blocksToLand = -1;
        for (int i = 1; i <= 40 && blocksToLand < 0; ++i) {
            h.block(kBlock);
            for (size_t e = 0; e < harness::sent.size(); ++e) {
                if (harness::sent[e].isBend() &&
                    harness::sent[e].d1 == 0x00 && harness::sent[e].d2 == 0x40) {
                    blocksToLand = i;
                    break;
                }
            }
        }

        char d[96];
        std::snprintf(d, sizeof d, "landed after %d blocks of %u frames",
                      blocksToLand, kBlock);
        ok("the glide lands at all", blocksToLand > 0, d);

        /*
         * 5760 frames / 1024 = 5.6, so the snap is due on the sixth block. A
         * generous ceiling of 8 still catches a doubled ramp, which would need
         * eleven.
         */
        ok("  and within the time it was given",
           blocksToLand > 0 && blocksToLand <= 8, d);

        /*
         * Half a ramp must be a partial bend, not silence and not the full
         * distance. Without this, a glide that never moved and then jumped at the
         * end would satisfy everything above - which is what "is it actually
         * gliding, or just a retrigger?" was asking, and what could not be
         * answered before there was a way to read the output.
         */
        Host h2;
        setGlideMode(h2, kGlideMpe);
        h2.set("glideTimeMs", 120);
        h2.gesture("press", 0, 0);
        h2.block(kBlock);

        harness::reset();
        h2.gesture("move", 4, 0);
        h2.block(2048);          /* ~36% of the way */

        int bends = 0, centred = 0;
        for (size_t e = 0; e < harness::sent.size(); ++e) {
            if (! harness::sent[e].isBend()) continue;
            ++bends;
            if (harness::sent[e].d1 == 0x00 && harness::sent[e].d2 == 0x40)
                ++centred;
        }

        char d2[96];
        std::snprintf(d2, sizeof d2, "%d bends mid-flight, %d of them centred",
                      bends, centred);
        ok("the pitch is bending part way through", bends > 0, d2);
        ok("  and has not snapped back yet", centred == 0, d2);
    }

    /* ---- 1.7 and 1.8: keyboard handover and ownership ----------------- */

    std::printf("\n=== keyboard: last-note priority, both directions ===\n");
    {
        for (int m = 0; m < 2; ++m) {
            const GlideMode modes[2] = { kGlideOff, kGlideMpe };
            const char* names[2] = { "off", "mpe" };

            Host h;
            setGlideMode(h, modes[m]);

            h.keyDown(60);            /* C */
            h.keyDown(62);            /* D, overlapping */
            h.settle(3);
            const int bothDown = harness::soundingCount();

            h.keyUp(62);              /* fall back to C, which is still held */
            h.settle();

            char d[80];
            std::snprintf(d, sizeof d, "%s: %d sounding with both down, %d after",
                          names[m], bothDown, harness::soundingCount());
            ok("releasing the newer key leaves the older sounding",
               harness::soundingCount() > 0, d);

            h.keyUp(60);
            h.settle();
            eq("  and the last release silences all", harness::soundingCount(), 0);
            eq("  leaving no refcount", h.heldRefs(), 0);
            eq("  and no group", h.activeGroups(), 0);
        }
    }

    /* ---- 1.9: the reported stuck note, against the real plugin -------- */

    std::printf("\n=== the reported gesture: rapid alternation ===\n");
    {
        const GlideMode modes[2] = { kGlideOff, kGlideMpe };
        const char* names[2] = { "off", "mpe" };

        for (int m = 0; m < 2; ++m) {
            Host h;
            setGlideMode(h, modes[m]);

            /*
             * Two keys alternated fast, overlapping, twenty times - the gesture
             * that was reported. Deliberately WITHOUT settling between events:
             * "rapid" is the whole point, and letting each glide land would be
             * a different gesture entirely.
             */
            h.keyDown(60);
            for (int i = 0; i < 20; ++i) {
                h.keyDown(62);
                h.keyUp(60);
                h.keyDown(60);
                h.keyUp(62);
            }
            h.keyUp(60);
            h.settle();

            char d[96];
            std::snprintf(d, sizeof d, "%s: %d sounding, %d refs, %d groups",
                          names[m], harness::soundingCount(), h.heldRefs(),
                          h.activeGroups());
            ok("twenty alternations leave nothing behind",
               harness::soundingCount() == 0 && h.heldRefs() == 0 &&
               h.activeGroups() == 0, d);
        }
    }

    /* Three keys rolled, where ownership is easiest to drop. */
    std::printf("\n=== a three-key roll, released out of order ===\n");
    {
        for (int m = 0; m < 2; ++m) {
            const GlideMode modes[2] = { kGlideOff, kGlideMpe };
            Host h;
            setGlideMode(h, modes[m]);

            h.keyDown(60);
            h.keyDown(62);
            h.keyDown(64);
            h.keyUp(62);        /* from the middle */
            h.keyUp(64);
            h.keyUp(60);
            h.settle();

            eq("every key up leaves silence", harness::soundingCount(), 0);
            eq("  and no refcount", h.heldRefs(), 0);
        }
    }

    /* ---- 1.11: pedal, panic, latch ----------------------------------- */

    std::printf("\n=== the pedal defers releases, and lets go on lift ===\n");
    {
        Host h;
        h.pedal(true);
        h.keyDown(60);
        h.settle(2);
        const int sounding = harness::soundingCount();
        ok("a key sounds with the pedal down", sounding > 0, "");

        h.keyUp(60);
        h.settle(2);
        ok("releasing the key does NOT silence it", harness::soundingCount() > 0,
           "deferred");

        h.pedal(false);
        h.settle();
        eq("lifting the pedal silences it", harness::soundingCount(), 0);
        eq("  and clears the refcount", h.heldRefs(), 0);
    }

    std::printf("\n=== panic silences everything, whatever the state ===\n");
    {
        Host h;
        setGlideMode(h, kGlideMpe);
        h.keyDown(60);
        h.keyDown(64);
        h.gesture("press", 2, 1);
        h.block();
        ok("several things are sounding", harness::soundingCount() > 0, "");

        h.set("panic", 1);
        h.block();
        eq("panic leaves nothing sounding", harness::soundingCount(), 0);
        eq("  no refcount", h.heldRefs(), 0);
        eq("  no group", h.activeGroups(), 0);
        eq("  and no lit cell", h.litCells(), 0);
    }

    /* ---- 1.10: a host that refuses events ---------------------------- */

    std::printf("\n=== a refused note-off is recovered, not forgotten ===\n");
    {
        Host h;
        h.gesture("press", 0, 0);
        h.block();
        const int before = harness::soundingCount();
        ok("a chord is sounding", before > 0, "");

        /*
         * Now refuse everything. The release cannot get its note-offs out, which
         * is the exact condition that leaves an instrument holding notes nothing
         * will release - and the plugin's answer is to latch fStuckNotes and
         * retry an all-notes-off on the next block.
         */
        harness::refuseAfter = static_cast<int>(harness::sent.size());
        h.gesture("release", 0, 0);
        h.block();
        ok("the host refused the note-offs", harness::refused > 0, "");

        /* Buffer clear again: the retry must happen and must silence everything. */
        harness::refuseAfter = -1;
        h.block();
        h.block();
        eq("the next block recovers to silence", harness::soundingCount(), 0);
        eq("  and the refcount is cleared", h.heldRefs(), 0);
    }

    /*
     * THE REPORTED BUG, reproduced.
     *
     * "I did a very rapid two key alternation like 20 times... I shut the plugin
     * off in ardour and midi still kept going. Turning [Kontakt] off stopped the
     * midi and then turning it back on worked."
     *
     * Two conditions had to be right before it showed, and neither was in the
     * first attempt at this test:
     *
     *  1. SEVERAL EVENTS IN ONE BLOCK. A host delivers everything that happened
     *     during a buffer together. At 48k/64 frames that is about 1.3ms, so a
     *     genuinely fast trill puts note-on and note-off in the SAME run() call.
     *     Feeding one event per block, as the first version did, is a slow trill
     *     however many times it repeats - and it passes.
     *
     *  2. A HOST THAT REFUSES EVENTS. Rapid alternation in MPE emits note-offs,
     *     note-ons and a bend per voice per block. A host with a modest queue
     *     refuses some, which is the whole reason fOutputFull and fStuckNotes
     *     exist.
     *
     * With both, the plugin ended up with notes SOUNDING while believing it was
     * idle - no groups, no refcounts, no keys held - so nothing on this side
     * would ever send another note-off. That is exactly why bypass changed
     * nothing and only retriggering the instrument helped.
     */
    std::printf("\n=== a fast trill against a host that refuses events ===\n");
    {
        const GlideMode modes[2] = { kGlideOff, kGlideMpe };
        const char* names[2] = { "off", "mpe" };
        const int caps[5] = { 4, 8, 16, 32, 64 };

        for (int m = 0; m < 2; ++m) {
            for (int c = 0; c < 5; ++c) {
                Host h;
                setGlideMode(h, modes[m]);
                harness::refuseAfter = -1;

                MidiEvent first;
                first.frame = 0; first.size = 3;
                first.data[0] = 0x90; first.data[1] = 60; first.data[2] = 100;
                first.data[3] = 0; first.dataExt = nullptr;
                h.block(64, &first, 1);

                /* Now the host's queue is small. */
                harness::refuseAfter = caps[c];

                for (int i = 0; i < 20; ++i) {
                    /* Four key movements inside one buffer - a real trill. */
                    MidiEvent evs[4];
                    const uint8_t spec[4][3] = {
                        { 0x90, 62, 100 }, { 0x80, 60, 0 },
                        { 0x90, 60, 100 }, { 0x80, 62, 0 },
                    };
                    for (int e = 0; e < 4; ++e) {
                        evs[e].frame   = e * 8;
                        evs[e].size    = 3;
                        evs[e].data[0] = spec[e][0];
                        evs[e].data[1] = spec[e][1];
                        evs[e].data[2] = spec[e][2];
                        evs[e].data[3] = 0;
                        evs[e].dataExt = nullptr;
                    }
                    h.block(64, evs, 4);
                }

                MidiEvent last;
                last.frame = 0; last.size = 3;
                last.data[0] = 0x80; last.data[1] = 60; last.data[2] = 0;
                last.data[3] = 0; last.dataExt = nullptr;
                h.block(64, &last, 1);

                /* The host has room again. The plugin must use it to recover. */
                harness::refuseAfter = -1;
                h.settle(40);

                char d[112];
                std::snprintf(d, sizeof d,
                              "%s cap %-2d: %d sounding, %d refs, %d groups, %d refused",
                              names[m], caps[c], harness::soundingCount(),
                              h.heldRefs(), h.activeGroups(), harness::refused);
                ok("every key up, and the instrument is silent",
                   harness::soundingCount() == 0, d);
            }
        }
    }

    /* ---- 1.3: the octave a drag lands in ---------------------------- */

    std::printf("\n=== a drag into a shifted strip lands an octave up ===\n");
    {
        /* Slide Mode's 8th strip carries shift +1. A press there and a drag there
         * must agree - the bug fixed in f1bc2c1, now checked on real output. */
        Host clicked;
        clicked.gesture("press", 7, 0, 1);
        clicked.block();
        int clickedLow = 127;
        for (size_t i = 0; i < harness::sent.size(); ++i)
            if (harness::sent[i].isNoteOn() && harness::sent[i].d1 < clickedLow)
                clickedLow = harness::sent[i].d1;

        Host dragged;
        setGlideMode(dragged, kGlideMpe);
        dragged.gesture("press", 0, 0, 0);
        dragged.block();
        harness::reset();
        dragged.gesture("move", 7, 0, 1);
        dragged.settle();
        int draggedLow = 127;
        for (size_t i = 0; i < harness::sent.size(); ++i)
            if (harness::sent[i].isNoteOn() && harness::sent[i].d1 < draggedLow)
                draggedLow = harness::sent[i].d1;

        char d[80];
        std::snprintf(d, sizeof d, "click %d, drag %d", clickedLow, draggedLow);
        ok("a drag reaches the same octave as a click",
           clickedLow == draggedLow, d);
    }

    /*
     * ONE CHORD BUILDER, WHATEVER TRIGGERED IT.
     *
     * Reported: "in the UI per cell selections for how to build the chord are
     * followed, but in keyboard mode it seems that it adheres to the old ring
     * system. If I change one chord type then they all change in the same level
     * ring... there shouldn't be two systems for this."
     *
     * The mechanism is worse than two systems. There is ONE store - three
     * extensions, one per ring, in fRingExtension[] - and the editor overwrites
     * the ring's value immediately before each pointer gesture, to whatever the
     * clicked cell needs. Its own comment says so:
     *
     *     "The DSP holds one extension per ring, not per cell, and does not need
     *      to know cells can differ: the editor asserts the right value
     *      immediately before the gesture that reads it."
     *
     * pushCellExtension() has exactly two callers, both mouse paths. A MIDI note
     * never passes through it, so noteOnCell() resolves its chord from whatever
     * value the last CLICK left behind. Hence both symptoms: one cell's setting
     * appears to change a whole ring, and a key plays the type of whichever cell
     * was clicked last.
     *
     * These assertions compare the two paths on the SAME cell. Any difference is
     * the bug, and they are expected to fail until the sources stop sharing
     * mutable state.
     */
    std::printf("\n=== a key and a click on one cell agree ===\n");
    {
        /*
         * C on the keyboard maps to degree I, which in the key of C is ring
         * kRingKey position 0 - the same cell a click on the wheel's I selects.
         * Verified through the plugin's own mapping rather than assumed.
         */
        int  pos  = -1;
        Ring ring = kRingKey;
        const bool mapped = cellForMidiNote(kDefaultKeyMap, 60, 0, pos, ring);
        ok("middle C maps to a cell at all", mapped, "");

        /* Note 60 is octave 4 by octaveForMidiNote(), which is the default base,
         * so neither path needs a shift for them to be comparable. */
        eq("  and asks for the base octave", octaveForMidiNote(60), 4);

        /* What a POINTER press on that cell emits. */
        Host byClick;
        byClick.gesture("press", pos, static_cast<int>(ring));
        byClick.block();
        uint8_t clickNotes[16];
        int clickCount = 0;
        for (size_t i = 0; i < harness::sent.size(); ++i)
            if (harness::sent[i].isNoteOn() && clickCount < 16)
                clickNotes[clickCount++] = harness::sent[i].d1;

        /* What a KEY press on the same cell emits. */
        Host byKey;
        byKey.keyDown(60);
        uint8_t keyNotes[16];
        int keyCount = 0;
        for (size_t i = 0; i < harness::sent.size(); ++i)
            if (harness::sent[i].isNoteOn() && keyCount < 16)
                keyNotes[keyCount++] = harness::sent[i].d1;

        char d[128];
        std::snprintf(d, sizeof d, "click %d notes, key %d notes",
                      clickCount, keyCount);
        ok("both paths sound the same number of notes",
           clickCount == keyCount && clickCount > 0, d);

        bool samePitches = (clickCount == keyCount);
        for (int i = 0; i < clickCount && i < keyCount && samePitches; ++i)
            if (clickNotes[i] != keyNotes[i]) samePitches = false;

        if (! samePitches && clickCount > 0 && keyCount > 0) {
            std::snprintf(d, sizeof d, "click [%d %d %d] key [%d %d %d]",
                          clickNotes[0],
                          clickCount > 1 ? clickNotes[1] : -1,
                          clickCount > 2 ? clickNotes[2] : -1,
                          keyNotes[0],
                          keyCount > 1 ? keyNotes[1] : -1,
                          keyCount > 2 ? keyNotes[2] : -1);
        }
        ok("  and exactly the same pitches", samePitches, d);

        /*
         * The comparison above passes even with the bug present, and the reason
         * is worth stating: both paths read the SAME ring-wide value, so with
         * nothing to diverge from they agree. Agreement on a shared stale value
         * is not the property under test.
         *
         * The divergence appears once the two cells differ. Give cell 0 a
         * seventh and leave cell 4 a triad - which is precisely what the editor's
         * per-cell state expresses - then play cell 4 BOTH ways. A click passes
         * through pushCellExtension() and asserts cell 4's own triad; a key does
         * not, so it inherits cell 0's seventh.
         */
        /*
         * Two cells that DIFFER, which is the situation the per-cell editor
         * creates and the one the old mechanism could not survive: cell 0 gets a
         * seventh, the cell under a key stays a triad. Each is then played both
         * ways and the two paths must agree cell by cell.
         */
        int  keyCell = -1;
        Ring keyRing = kRingKey;
        int  keyNote = -1;
        for (int n = 61; n < 72 && keyNote < 0; ++n) {
            int p; Ring r;
            if (cellForMidiNote(kDefaultKeyMap, n, 0, p, r) &&
                ! (p == 0 && r == kRingKey) && octaveForMidiNote(n) == 4) {
                keyNote = n; keyCell = p; keyRing = r;
            }
        }

        if (keyNote < 0) {
            ok("a second mapped key exists to compare", false, "none found");
        } else {
            Host h;
            /* Cell 0 wants a seventh; the key's cell is left a plain triad. */
            h.setCellExt(kRingKey, 0, kExt7);

            /* The key's cell, clicked. */
            harness::reset();
            h.gesture("press", keyCell, static_cast<int>(keyRing));
            h.block();
            int clicked = 0;
            for (size_t i = 0; i < harness::sent.size(); ++i)
                if (harness::sent[i].isNoteOn()) ++clicked;
            h.gesture("release", keyCell, static_cast<int>(keyRing));
            h.block();

            /* The same cell, keyed - with cell 0's seventh still set. */
            harness::reset();
            h.keyDown(static_cast<uint8_t>(keyNote));
            int keyed = 0;
            for (size_t i = 0; i < harness::sent.size(); ++i)
                if (harness::sent[i].isNoteOn()) ++keyed;

            char dd[144];
            std::snprintf(dd, sizeof dd,
                          "cell %d: clicked %d notes, keyed (note %d) %d notes",
                          keyCell, clicked, keyNote, keyed);
            ok("a differing cell agrees clicked or keyed",
               clicked == keyed && clicked > 0, dd);

            /* And cell 0, which DOES want a seventh, must actually get one -
             * otherwise the two paths could agree by both being wrong. */
            harness::reset();
            h.gesture("press", 0, 0);
            h.block();
            int seventh = 0;
            for (size_t i = 0; i < harness::sent.size(); ++i)
                if (harness::sent[i].isNoteOn()) ++seventh;

            std::snprintf(dd, sizeof dd, "cell 0 with a 7th = %d notes, "
                                         "triad cell = %d", seventh, clicked);
            ok("  and the cell that asked for a 7th has more notes",
               seventh > clicked, dd);
        }
    }

    /*
     * And the symptom in its own words: one cell's chord type must not move its
     * neighbours.
     *
     * Driven entirely through the DSP's own state interface, which is what a
     * keyboard note reads. If a per-ring setting is the only store, changing it
     * for one cell necessarily changes the chord every OTHER cell in that ring
     * produces - which is the reported behaviour exactly.
     */
    std::printf("\n=== one cell's chord type leaves its neighbours alone ===\n");
    {
        Host h;

        /* Cell 0 of the key ring, as a plain triad. */
        h.gesture("press", 0, 0);
        h.block();
        int before = 0;
        for (size_t i = 0; i < harness::sent.size(); ++i)
            if (harness::sent[i].isNoteOn()) ++before;
        h.gesture("release", 0, 0);
        h.block();

        /* Now ask for a seventh on cell 0 ALONE, the way the editor does. */
        h.setCellExt(kRingKey, 0, kExt7);

        /* Cell 4 of the same ring, which was never given a seventh. */
        harness::reset();
        h.gesture("press", 4, 0);
        h.block();
        int neighbour = 0;
        for (size_t i = 0; i < harness::sent.size(); ++i)
            if (harness::sent[i].isNoteOn()) ++neighbour;

        char d[96];
        std::snprintf(d, sizeof d,
                      "cell 0 triad was %d notes, cell 4 now %d",
                      before, neighbour);
        ok("a seventh on one cell does not extend another", neighbour == before, d);
    }

    /*
     * VOICING is per cell too, and needs its own assertion.
     *
     * Extension changes the NUMBER of notes, so a note count catches it. Voicing
     * changes which note is in the bass and how the chord is spaced - the same
     * count, different pitches - so a count is blind to it. Reverting voicing to
     * a ring-wide lookup passed the whole suite until this existed, which is the
     * half-state the plan warned about: the right chord in the wrong inversion is
     * more confusing than the original bug.
     */
    std::printf("\n=== voicing is per cell, and audible ===\n");
    {
        /* Find a voicing that actually rearranges a triad. Asserting against a
         * particular enum value would break the moment the list is reordered. */
        Host probe;
        harness::reset();
        probe.gesture("press", 0, 0);
        probe.block();
        uint8_t plain[16];
        int plainCount = 0;
        for (size_t i = 0; i < harness::sent.size(); ++i)
            if (harness::sent[i].isNoteOn() && plainCount < 16)
                plain[plainCount++] = harness::sent[i].d1;

        int      movedBy = -1;
        uint8_t  moved[16];
        int      movedCount = 0;
        for (int v = 1; v < kVoicingCount && movedBy < 0; ++v) {
            Host h;
            h.setCellVoicing(kRingKey, 0, static_cast<Voicing>(v));
            harness::reset();
            h.gesture("press", 0, 0);
            h.block();

            uint8_t got[16];
            int n = 0;
            for (size_t i = 0; i < harness::sent.size(); ++i)
                if (harness::sent[i].isNoteOn() && n < 16)
                    got[n++] = harness::sent[i].d1;

            bool differs = (n != plainCount);
            for (int i = 0; i < n && i < plainCount && ! differs; ++i)
                if (got[i] != plain[i]) differs = true;

            if (differs) {
                movedBy    = v;
                movedCount = n;
                for (int i = 0; i < n; ++i) moved[i] = got[i];
            }
        }

        char d[112];
        std::snprintf(d, sizeof d, "voicing %d changes the chord", movedBy);
        ok("some voicing rearranges the notes", movedBy > 0, d);

        if (movedBy > 0) {
            /*
             * Now the property: that voicing is set on cell 0 ALONE, so cell 4
             * must be unaffected - and cell 0 must differ from plain whether it
             * is clicked or keyed.
             */
            Host h;
            h.setCellVoicing(kRingKey, 0, static_cast<Voicing>(movedBy));

            harness::reset();
            h.gesture("press", 4, 0);
            h.block();
            uint8_t neighbour[16];
            int nCount = 0;
            for (size_t i = 0; i < harness::sent.size(); ++i)
                if (harness::sent[i].isNoteOn() && nCount < 16)
                    neighbour[nCount++] = harness::sent[i].d1;
            h.gesture("release", 4, 0);
            h.block();

            /* Cell 4 with no voicing of its own, for comparison. */
            Host clean;
            harness::reset();
            clean.gesture("press", 4, 0);
            clean.block();
            uint8_t cleanNotes[16];
            int cCount = 0;
            for (size_t i = 0; i < harness::sent.size(); ++i)
                if (harness::sent[i].isNoteOn() && cCount < 16)
                    cleanNotes[cCount++] = harness::sent[i].d1;

            bool neighbourUntouched = (nCount == cCount);
            for (int i = 0; i < nCount && i < cCount && neighbourUntouched; ++i)
                if (neighbour[i] != cleanNotes[i]) neighbourUntouched = false;

            std::snprintf(d, sizeof d,
                          "cell 4 with/without cell 0's voicing: %d vs %d notes",
                          nCount, cCount);
            ok("one cell's voicing leaves its neighbour alone",
               neighbourUntouched, d);

            /* And a key on cell 0 must get cell 0's voicing, like a click. */
            int keyForZero = -1;
            for (int n = 60; n < 72 && keyForZero < 0; ++n) {
                int p; Ring r;
                if (cellForMidiNote(kDefaultKeyMap, n, 0, p, r) &&
                    p == 0 && r == kRingKey && octaveForMidiNote(n) == 4)
                    keyForZero = n;
            }

            if (keyForZero < 0) {
                ok("a key maps to cell 0", false, "none found");
            } else {
                Host viaKey;
                viaKey.setCellVoicing(kRingKey, 0, static_cast<Voicing>(movedBy));
                harness::reset();
                viaKey.keyDown(static_cast<uint8_t>(keyForZero));

                uint8_t keyed[16];
                int kCount = 0;
                for (size_t i = 0; i < harness::sent.size(); ++i)
                    if (harness::sent[i].isNoteOn() && kCount < 16)
                        keyed[kCount++] = harness::sent[i].d1;

                bool matchesClick = (kCount == movedCount);
                for (int i = 0; i < kCount && i < movedCount && matchesClick; ++i)
                    if (keyed[i] != moved[i]) matchesClick = false;

                std::snprintf(d, sizeof d,
                              "keyed [%d %d %d] vs clicked [%d %d %d]",
                              kCount > 0 ? keyed[0] : -1,
                              kCount > 1 ? keyed[1] : -1,
                              kCount > 2 ? keyed[2] : -1,
                              movedCount > 0 ? moved[0] : -1,
                              movedCount > 1 ? moved[1] : -1,
                              movedCount > 2 ? moved[2] : -1);
                ok("a key gets the cell's voicing, exactly as a click does",
                   matchesClick && kCount > 0, d);
            }
        }
    }

    /*
     * And a keyboard GLIDE must head for the destination cell's own voicing.
     *
     * The glide target is built separately from the group that lands, so it can
     * disagree: the ramp heads one way and the snap restates another, which is
     * heard as the pitch arriving and then jumping. This was unguarded until the
     * fault injection showed that dropping the cell's voicing from the keyboard's
     * target changed nothing any test could see.
     *
     * MPE only - it is the one mode where a glide is attempted at all.
     */
    std::printf("\n=== a keyboard glide heads for the cell's voicing ===\n");
    {
        /* Two mapped keys in the base octave, on different cells. */
        int  noteA = -1, noteB = -1, cellA = -1, cellB = -1;
        Ring ringA = kRingKey, ringB = kRingKey;
        for (int n = 60; n < 72; ++n) {
            int p; Ring r;
            if (! cellForMidiNote(kDefaultKeyMap, n, 0, p, r)) continue;
            if (octaveForMidiNote(n) != 4) continue;
            if (noteA < 0) { noteA = n; cellA = p; ringA = r; }
            else if (p != cellA) { noteB = n; cellB = p; ringB = r; break; }
        }

        if (noteB < 0) {
            ok("two mapped keys on different cells exist", false, "not found");
        } else {
            /*
             * The destination cell gets a voicing of its own. After the glide has
             * landed, what sounds must be that cell's arrangement - the same
             * pitches a press on it would produce.
             */
            Host glided;
            setGlideMode(glided, kGlideMpe);
            glided.setCellVoicing(ringB, cellB, static_cast<Voicing>(1));

            glided.keyDown(static_cast<uint8_t>(noteA));
            glided.settle(2);
            harness::reset();
            glided.keyDown(static_cast<uint8_t>(noteB));
            glided.settle(30);          /* well past the snap */

            uint8_t landedNotes[16];
            int landedCount = 0;
            for (size_t i = 0; i < harness::sent.size(); ++i)
                if (harness::sent[i].isNoteOn() && landedCount < 16)
                    landedNotes[landedCount++] = harness::sent[i].d1;

            /* What a plain press on that cell sounds, same voicing. */
            Host pressed;
            pressed.setCellVoicing(ringB, cellB, static_cast<Voicing>(1));
            harness::reset();
            pressed.gesture("press", cellB, static_cast<int>(ringB));
            pressed.block();
            uint8_t wanted[16];
            int wantedCount = 0;
            for (size_t i = 0; i < harness::sent.size(); ++i)
                if (harness::sent[i].isNoteOn() && wantedCount < 16)
                    wanted[wantedCount++] = harness::sent[i].d1;

            /* The glide's final note-ons are the snap's restatement, so the LAST
             * wantedCount of them are what should match. */
            bool matches = (landedCount >= wantedCount && wantedCount > 0);
            for (int i = 0; i < wantedCount && matches; ++i)
                if (landedNotes[landedCount - wantedCount + i] != wanted[i])
                    matches = false;

            char d[144];
            std::snprintf(d, sizeof d,
                          "landed ...[%d %d %d], a press gives [%d %d %d]",
                          landedCount > 2 ? landedNotes[landedCount - 3] : -1,
                          landedCount > 1 ? landedNotes[landedCount - 2] : -1,
                          landedCount > 0 ? landedNotes[landedCount - 1] : -1,
                          wantedCount > 0 ? wanted[0] : -1,
                          wantedCount > 1 ? wanted[1] : -1,
                          wantedCount > 2 ? wanted[2] : -1);
            ok("the glide lands on the cell's own arrangement", matches, d);
        }
    }

    /*
     * The table must survive a save and a reload, or every per-cell setting is
     * lost on reopen - which would look perfect in one session and silently
     * discard the user's work in the next.
     */
    std::printf("\n=== the per-cell table round-trips through state ===\n");
    {
        Host h;
        h.setCellExt(kRingKey, 0, kExt7);
        h.setCellExt(kRingMinor, 5, kExtSus4);
        h.setCellVoicing(kRingDim, 3, static_cast<Voicing>(1));

        /* What the host would save. */
        const String saved = h.get("cellSettings");
        ok("the table encodes to something", saved.length() > 0, saved.buffer());

        /* A fresh instance, given that string back. */
        Host reopened;
        reopened.set("cellSettings", saved.buffer());
        const String again = reopened.get("cellSettings");

        ok("  and decodes to the same string",
           std::strcmp(saved.buffer(), again.buffer()) == 0, again.buffer());

        /* And it sounds the same: cell 0 keeps its seventh. */
        harness::reset();
        reopened.gesture("press", 0, 0);
        reopened.block();
        int n = 0;
        for (size_t i = 0; i < harness::sent.size(); ++i)
            if (harness::sent[i].isNoteOn()) ++n;

        char d[64];
        std::snprintf(d, sizeof d, "cell 0 after reload = %d notes", n);
        ok("  and cell 0 still sounds its seventh", n == 4, d);

        /* An empty string is a fresh session, and must mean all defaults rather
         * than leaving the previous table in place. */
        reopened.set("cellSettings", "");
        harness::reset();
        reopened.gesture("press", 0, 0);
        reopened.block();
        int cleared = 0;
        for (size_t i = 0; i < harness::sent.size(); ++i)
            if (harness::sent[i].isNoteOn()) ++cleared;
        std::snprintf(d, sizeof d, "cell 0 after clearing = %d notes", cleared);
        ok("  an empty table restores the defaults", cleared == 3, d);
    }

    /* ---- the invariant that ties it together ------------------------ */

    std::printf("\n=== nothing held means nothing sounding ===\n");
    {
        /*
         * A long pseudo-random performance across every mode: presses, drags,
         * keys, pedal and octave changes interleaved. Whatever it does, once
         * every key and pointer is released the instrument must be silent.
         *
         * This is the property that the reported bug broke, and the one most
         * worth checking against arbitrary input rather than a chosen sequence.
         */
        const GlideMode modes[2] = { kGlideOff, kGlideMpe };
        const char* names[2] = { "off", "mpe" };
        int worstSounding = 0, worstRefs = 0;

        for (int m = 0; m < 2; ++m) {
            Host h;
            setGlideMode(h, modes[m]);

            unsigned seed = 12345u;
            const int keys[6] = { 60, 62, 64, 65, 67, 69 };
            bool down[6] = { false, false, false, false, false, false };

            for (int step = 0; step < 300; ++step) {
                seed = seed * 1103515245u + 12345u;
                const int action = (seed >> 16) % 6;
                const int which  = (seed >> 8) % 6;

                switch (action) {
                    case 0: case 1:
                        if (! down[which]) { h.keyDown(keys[which]); down[which] = true; }
                        break;
                    case 2: case 3:
                        if (down[which]) { h.keyUp(keys[which]); down[which] = false; }
                        break;
                    case 4:
                        h.gesture("press", which % 12, 0);
                        h.block();
                        h.gesture("release", which % 12, 0);
                        break;
                    case 5:
                        h.set("octave", 3 + (which % 4));
                        break;
                }
                h.block();
            }

            /* Everything up. */
            for (int i = 0; i < 6; ++i)
                if (down[i]) h.keyUp(keys[i]);
            h.settle(30);

            if (harness::soundingCount() > worstSounding)
                worstSounding = harness::soundingCount();
            if (h.heldRefs() > worstRefs) worstRefs = h.heldRefs();

            char d[96];
            std::snprintf(d, sizeof d, "%s: %d sounding, %d refs, %d groups",
                          names[m], harness::soundingCount(), h.heldRefs(),
                          h.activeGroups());
            ok("300 random events, then silence",
               harness::soundingCount() == 0 && h.heldRefs() == 0, d);
        }

        char d[80];
        std::snprintf(d, sizeof d, "worst case %d sounding, %d refs",
                      worstSounding, worstRefs);
        ok("no mode stranded anything", worstSounding == 0 && worstRefs == 0, d);
    }

    std::printf("\n%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
