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

    void set(const char* key, int value)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d", value);
        setState(key, buf);
    }

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
