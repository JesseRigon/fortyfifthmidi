/*
 * Cell highlighting.
 *
 * Reported: cells lit on the wheel that nothing was playing - three lit while
 * the note count said one chord's worth. Sound was correct throughout, because
 * the audio path never consults any of this.
 *
 * TWO FAULTS, both from treating a sentinel as a packed cell.
 *
 * A source packs a cell as (ring << 8) | position. Two sources are sentinels
 * instead: kProgSource (0xF000) and kMergedSource (-2). Decoded as cells they
 * give ring 240 and ring 255, against three rings that exist - so both
 * kRingSegments[] and the ring array were indexed out of bounds.
 *
 *   1. startGroup() took the ring from its parameter but the position from the
 *      source. For the sequencer that is a REAL ring paired with position 0, so
 *      it lit cell 0 of whichever ring the chord landed on. stopGroup() then
 *      went to clear ring 240 and missed, leaving that cell lit forever.
 *
 *   2. A merge reassigns a sounding group's source to kMergedSource, so a
 *      later move does not move it again. That destroyed the only record of
 *      which cell the group had lit, and stopGroup() could no longer clear it.
 */
#include <cstdio>
#include <cstring>
#include "CircleTheory.hpp"

using namespace fortyfifth;

static int failures = 0;

static void ok(const char* what, bool cond, const char* detail)
{
    if (cond) {
        std::printf("  ok    %-50s %s\n", what, detail);
    } else {
        std::printf("  FAIL  %-50s %s\n", what, detail);
        ++failures;
    }
}

/* Mirrors FortyFifthPlugin. */
static constexpr int kProgSource   = 0xF000;
static constexpr int kMergedSource = -2;

static int  positionFromSource(int s) { return s & 0xFF; }
static Ring ringFromSource(int s) { return static_cast<Ring>((s >> 8) & 0xFF); }

static bool sourceIsCell(int source)
{
    if (source < 0)
        return false;
    const int ring = (source >> 8) & 0xFF;
    if (ring < 0 || ring >= kRingCount)
        return false;
    return positionFromSource(source) < segmentsInRing(static_cast<Ring>(ring));
}

static int packSource(Ring r, int pos)
{
    return (static_cast<int>(r) << 8) | pos;
}

/* A cut-down group, carrying only what the highlight depends on. */
struct Group {
    bool active  = false;
    int  source  = -1;
    int  litCell = -1;
};

struct Engine {
    static constexpr int kMaxGroups = 8;
    Group       group[kMaxGroups];
    ActiveCells cells;

    Group* alloc()
    {
        for (int i = 0; i < kMaxGroups; ++i)
            if (! group[i].active) return &group[i];
        return nullptr;
    }

    /*
     * Mirrors FortyFifthPlugin::publishCells().
     *
     * The lit set is DERIVED from the groups that are sounding, not maintained
     * alongside them. Every incremental version drifted, because there is
     * always one more path that changes what sounds without remembering to
     * change the lights too.
     */
    void publish()
    {
        uint32_t bits[kRingCount] = {0};
        for (int i = 0; i < kMaxGroups; ++i) {
            const Group& g = group[i];
            if (! g.active || ! sourceIsCell(g.litCell)) continue;
            const int r = (g.litCell >> 8) & 0xFF;
            const int n = segmentsInRing(static_cast<Ring>(r));
            bits[r] |= 1u << (((g.litCell & 0xFF) % n + n) % n);
        }
        cells.setRings(bits);
    }

    void start(int source)
    {
        Group* g = alloc();
        if (g == nullptr) return;
        g->active  = true;
        g->source  = source;
        g->litCell = sourceIsCell(source) ? source : -1;
        publish();
    }

    void stop(Group* g)
    {
        if (! g->active) return;
        g->active  = false;
        g->source  = -1;
        g->litCell = -1;
        publish();      /* no "was anyone else holding it?" bookkeeping */
    }

    /*
     * Mirrors FortyFifthPlugin::moveLitCell().
     *
     * A glide retargets the notes of a group that is ALREADY sounding rather
     * than starting a new one - that is the whole point, nothing retriggers.
     * But litCell is written when a group starts and never again, so the
     * highlight stayed on the cell the glide began from.
     */
    void moveLit(Group* g, int newSource)
    {
        if (g == nullptr) return;
        g->litCell = sourceIsCell(newSource) ? newSource : -1;
        publish();
    }

    /*
     * Mirrors the snap at the end of advanceGlide(): the group is stopped and
     * restarted from its ORIGINAL source, then the cell it landed on is put
     * back. Without that restore the snap relights the cell the phrase began
     * on - which is why moving the highlight when the glide STARTED did not
     * stick, and the reported symptom survived that fix.
     */
    void glideSnap(Group* g)
    {
        if (g == nullptr) return;
        const int landed = g->litCell;
        const int source = g->source;
        stop(g);
        start(source);
        moveLit(find(source), landed);
    }

    /* Which cells are lit, as a sorted list, for exact comparison. */
    void litCells(int* out, int& n) const
    {
        n = 0;
        for (int r = 0; r < kRingCount; ++r)
            for (int p = 0; p < segmentsInRing(static_cast<Ring>(r)); ++p)
                if (cells.isOn(static_cast<Ring>(r), p))
                    out[n++] = (r << 8) | p;
    }

    Group* find(int source)
    {
        for (int i = 0; i < kMaxGroups; ++i)
            if (group[i].active && group[i].source == source) return &group[i];
        return nullptr;
    }

    void stopAll() { for (int i = 0; i < kMaxGroups; ++i) stop(&group[i]); }

    int litCount() const
    {
        int n = 0;
        for (int r = 0; r < kRingCount; ++r)
            for (int p = 0; p < segmentsInRing(static_cast<Ring>(r)); ++p)
                if (cells.isOn(static_cast<Ring>(r), p)) ++n;
        return n;
    }
};

int main()
{
    char d[96];

    /* --- the sentinels are not cells --------------------------------- */
    std::printf("=== sentinels are not cells ===\n");
    {
        std::snprintf(d, sizeof d, "decodes to ring %d",
                      static_cast<int>(ringFromSource(kProgSource)));
        ok("kProgSource is rejected", ! sourceIsCell(kProgSource), d);

        std::snprintf(d, sizeof d, "decodes to ring %d",
                      static_cast<int>(ringFromSource(kMergedSource)));
        ok("kMergedSource is rejected", ! sourceIsCell(kMergedSource), d);

        ok("a real cell is accepted", sourceIsCell(packSource(kRingKey, 5)),
           "ring 0, position 5");
        ok("a position past the ring is rejected",
           ! sourceIsCell(packSource(kRingKey, 99)), "out of range");

        /* The 24-cell minor ring must not be truncated to 12. */
        ok("the minor ring's upper cells are accepted",
           sourceIsCell(packSource(kRingMinor, 20)), "position 20 of 24");
    }

    /* --- out-of-bounds writes are refused ---------------------------- */
    std::printf("\n=== a sentinel cannot light anything ===\n");
    {
        ActiveCells c;
        /* Before the range check this indexed kRingSegments[240]. */
        c.set(ringFromSource(kProgSource), 0, true);
        c.set(ringFromSource(kMergedSource), 0, true);

        int lit = 0;
        for (int r = 0; r < kRingCount; ++r)
            for (int p = 0; p < segmentsInRing(static_cast<Ring>(r)); ++p)
                if (c.isOn(static_cast<Ring>(r), p)) ++lit;
        ok("neither sentinel lit a cell", lit == 0, "nothing lit");
        ok("and reading one is safe",
           ! c.isOn(ringFromSource(kProgSource), 0), "returns false");
    }

    /* --- the sequencer ------------------------------------------------ */
    std::printf("\n=== the sequencer lights no wheel cell ===\n");
    {
        Engine e;
        for (int i = 0; i < 32; ++i) {
            e.start(kProgSource);
            e.stopAll();
        }
        ok("32 sequencer chords leave nothing lit", e.litCount() == 0,
           "it has no cell to light");
    }

    /* --- the reported bug --------------------------------------------- */
    std::printf("\n=== a merged group still unlights its cell ===\n");
    {
        Engine e;
        const int a = packSource(kRingKey, 3);
        const int b = packSource(kRingKey, 4);

        e.start(a);
        ok("the first chord lights one cell", e.litCount() == 1, "cell 3");

        /* A glide crosses into b while a is inside the merge window, so a is
         * left sounding and its SOURCE is reassigned. */
        Group* g = e.find(a);
        g->source = kMergedSource;
        e.start(b);
        ok("both are lit while both sound", e.litCount() == 2, "cells 3 and 4");

        /* Releasing everything must clear both. Before the fix the merged one
         * could not be identified, and cell 3 stayed lit forever. */
        e.stopAll();
        std::snprintf(d, sizeof d, "%d still lit", e.litCount());
        ok("releasing clears both", e.litCount() == 0, d);
    }

    /* --- a long glide, which is how it accumulated -------------------- */
    std::printf("\n=== a glide across many cells strands nothing ===\n");
    {
        Engine e;
        int prev = -1;
        for (int p = 0; p < 12; ++p) {
            const int s = packSource(kRingKey, p);
            if (prev >= 0) {
                Group* g = e.find(prev);
                if (g != nullptr) g->source = kMergedSource;   /* merged */
            }
            e.start(s);
            prev = s;
        }
        e.stopAll();
        std::snprintf(d, sizeof d, "%d still lit", e.litCount());
        ok("12 merged chords leave nothing lit", e.litCount() == 0, d);
    }

    /* --- two groups sharing one cell ---------------------------------- */
    std::printf("\n=== a shared cell stays lit until the last holder goes ===\n");
    {
        Engine e;
        const int s = packSource(kRingMinor, 7);

        e.start(s);          /* latched */
        e.start(s);          /* pressed again on the same cell */
        ok("one cell, two groups", e.litCount() == 1, "lit once");

        /* Stopping one must NOT darken a cell the other still sounds. */
        for (int i = 0; i < Engine::kMaxGroups; ++i)
            if (e.group[i].active) { e.stop(&e.group[i]); break; }
        ok("still lit with one holder left", e.litCount() == 1, "not darkened");

        e.stopAll();
        ok("dark once both are gone", e.litCount() == 0, "cleared");
    }

    /* --- mixed traffic ------------------------------------------------ */
    std::printf("\n=== the sequencer and the wheel do not interfere ===\n");
    {
        Engine e;
        const int s = packSource(kRingDim, 2);

        e.start(s);                       /* a chord held by hand */
        for (int i = 0; i < 16; ++i)      /* the sequencer runs underneath */
            e.start(kProgSource);
        ok("only the hand-played cell is lit", e.litCount() == 1, "one cell");

        /* Stopping the sequencer's groups must not darken the held cell. */
        for (int i = 0; i < Engine::kMaxGroups; ++i)
            if (e.group[i].active && e.group[i].source == kProgSource)
                e.stop(&e.group[i]);
        ok("still lit after the sequencer stops", e.litCount() == 1, "untouched");

        e.stopAll();
        ok("clear at the end", e.litCount() == 0, "nothing lit");
    }


    /* --- glide moves the highlight ------------------------------------ */
    /*
     * Reported: "when two keys on the keyboard midi inputs overlap only 1 cell
     * can be highlighted at once and they don't both show up and it doesn't
     * switch highlighting when new keys play. the sounds trigger fine."
     *
     * Exactly right, and the "sounds trigger fine" is the clue: the audio path
     * never consults the highlight. With glide on, a second held key does not
     * start a second group - it GLIDES the first key's group onto the new
     * chord, so one group sounds and one cell is lit. The cell was simply the
     * wrong one, and never moved.
     */
    std::printf("\n=== a glide takes the highlight with it ===\n");
    {
        Engine e;
        const int a = packSource(kRingKey, 0);
        const int b = packSource(kRingKey, 1);

        e.start(a);
        ok("first key lights its cell", e.cells.isOn(kRingKey, 0), "cell 0");

        /* Second key glides the SAME group onto cell b. */
        Group* g = e.find(a);
        e.moveLit(g, b);

        ok("the new cell is lit",      e.cells.isOn(kRingKey, 1), "cell 1");
        ok("the old cell is dark",   ! e.cells.isOn(kRingKey, 0), "moved, not added");
        std::snprintf(d, sizeof d, "%d lit", e.litCount());
        ok("exactly one cell is lit",  e.litCount() == 1, d);

        /* Releasing ends it, and nothing is stranded on either cell. */
        e.stopAll();
        ok("nothing left lit", e.litCount() == 0, "clean");
    }

    /* A run of keys, which is what playing actually does. */
    std::printf("\n=== a legato run lights only where it is ===\n");
    {
        Engine e;
        int prev = packSource(kRingKey, 0);
        e.start(prev);

        bool always1 = true;
        for (int p = 1; p < 12; ++p) {
            const int s = packSource(kRingKey, p);
            e.moveLit(e.find(prev), s);
            if (e.litCount() != 1) always1 = false;
            if (! e.cells.isOn(kRingKey, p)) always1 = false;
        }
        ok("12 glided keys keep exactly one cell lit, the current one",
           always1, "follows the phrase");

        e.stopAll();
        ok("and nothing survives the release", e.litCount() == 0, "clean");
    }

    /*
     * Two groups genuinely sounding at once - what happens with glide OFF,
     * where a second key starts its own group. Both must light.
     */
    std::printf("\n=== two real groups light two cells ===\n");
    {
        Engine e;
        const int a = packSource(kRingKey, 2);
        const int b = packSource(kRingMinor, 5);

        e.start(a);
        e.start(b);
        std::snprintf(d, sizeof d, "%d lit", e.litCount());
        ok("both cells lit", e.litCount() == 2, d);
        ok("  on their own rings",
           e.cells.isOn(kRingKey, 2) && e.cells.isOn(kRingMinor, 5), "key + minor");

        /* Stopping one must not darken the other. */
        e.stop(e.find(a));
        ok("one stops, the other stays lit",
           e.litCount() == 1 && e.cells.isOn(kRingMinor, 5), "independent");

        e.stopAll();
        ok("both clear", e.litCount() == 0, "clean");
    }

    /* A glide onto a cell a DIFFERENT group already holds must not darken it
     * when the glide later leaves. */
    std::printf("\n=== gliding across a cell someone else holds ===\n");
    {
        Engine e;
        const int held  = packSource(kRingKey, 4);
        const int start = packSource(kRingKey, 6);

        e.start(held);                 /* group 1 sits on cell 4 */
        e.start(start);                /* group 2 starts on cell 6 */
        ok("two cells lit", e.litCount() == 2, "4 and 6");

        Group* g = e.find(start);
        e.moveLit(g, held);            /* group 2 glides ONTO cell 4 */
        ok("still lit where both now are", e.cells.isOn(kRingKey, 4), "shared");
        ok("the vacated cell went dark", ! e.cells.isOn(kRingKey, 6), "left it");

        e.moveLit(g, packSource(kRingKey, 8));   /* and glides away again */
        ok("the held cell survives the departure",
           e.cells.isOn(kRingKey, 4), "its owner still holds it");
        ok("the new cell is lit", e.cells.isOn(kRingKey, 8), "arrived");

        e.stopAll();
        ok("all clear", e.litCount() == 0, "clean");
    }

    /* A glide to a sentinel is not a cell, so it just unlights. */
    std::printf("\n=== gliding to a non-cell ===\n");
    {
        Engine e;
        const int a = packSource(kRingKey, 3);
        e.start(a);
        e.moveLit(e.find(a), kProgSource);
        ok("no cell lit", e.litCount() == 0, "a sentinel has none");
        e.stopAll();
        ok("and stopping is still safe", e.litCount() == 0, "clean");
    }


    /* --- the snap at the end of a glide ------------------------------- */
    /*
     * The fix that did not hold.
     *
     * Moving the highlight when the glide STARTS is correct but insufficient:
     * advanceGlide() finishes by stopping the group and restarting it from the
     * source it began with, so the cell went straight back to where the phrase
     * started. The lights only stay put if the landed cell survives that
     * rebuild.
     */
    std::printf("\n=== the highlight survives the glide's snap ===\n");
    {
        Engine e;
        const int a = packSource(kRingKey, 0);
        const int b = packSource(kRingKey, 5);

        e.start(a);
        e.moveLit(e.find(a), b);          /* glide begins, cell moves */
        ok("moved on the way", e.cells.isOn(kRingKey, 5), "cell 5");

        e.glideSnap(e.find(a));           /* glide completes */
        ok("still on the landed cell", e.cells.isOn(kRingKey, 5), "cell 5");
        ok("not back at the start",  ! e.cells.isOn(kRingKey, 0), "cell 0 dark");
        std::snprintf(d, sizeof d, "%d lit", e.litCount());
        ok("exactly one lit", e.litCount() == 1, d);

        e.stopAll();
        ok("clean after release", e.litCount() == 0, "nothing lit");
    }

    /* A full phrase: several glides, each snapping, then release. */
    std::printf("\n=== a phrase of glides, each one snapping ===\n");
    {
        Engine e;
        int cur = packSource(kRingKey, 0);
        e.start(cur);

        bool right = true;
        for (int p = 1; p < 12; ++p) {
            const int nxt = packSource(kRingKey, p);
            Group* g = e.find(cur);
            e.moveLit(g, nxt);
            e.glideSnap(e.find(cur));
            if (e.litCount() != 1 || ! e.cells.isOn(kRingKey, p)) right = false;
        }
        ok("the lit cell tracks the phrase throughout", right, "12 glides");

        e.stopAll();
        ok("and nothing is stranded", e.litCount() == 0, "clean");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
