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

    void start(int source)
    {
        Group* g = alloc();
        if (g == nullptr) return;
        g->active = true;
        g->source = source;

        if (sourceIsCell(source)) {
            g->litCell = source;
            cells.set(ringFromSource(source), positionFromSource(source), true);
        } else {
            g->litCell = -1;
        }
    }

    void stop(Group* g)
    {
        if (! g->active) return;

        const int  cell = g->litCell;
        const Ring r    = ringFromSource(cell);
        const int  pos  = positionFromSource(cell);

        g->active  = false;
        g->source  = -1;
        g->litCell = -1;

        if (sourceIsCell(cell)) {
            bool stillHeld = false;
            for (int i = 0; i < kMaxGroups && ! stillHeld; ++i)
                stillHeld = (group[i].active && group[i].litCell == cell);
            if (! stillHeld)
                cells.set(r, pos, false);
        }
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

    std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
