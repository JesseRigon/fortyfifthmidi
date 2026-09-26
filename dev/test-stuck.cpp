/*
 * Every note that starts must be able to stop.
 *
 * Reported: "I did a very rapid two key alternation like 20 times which caused
 * that... I shut the plugin off in ardour and midi still kept going. I had a
 * kontakt player in the track as the synth. turning that off stopped the midi
 * and then turning it back on worked."
 *
 * That the SYNTH had to be toggled is the diagnosis. Bypassing the generator
 * changed nothing, so nothing here was sustaining the note - a Note On went out
 * whose Note Off never did, and the instrument was holding a key nobody had
 * released. Once that has happened this plugin cannot undo it: bypass only
 * stops new events, and the host's panic goes to a plugin no longer in the path.
 *
 * THE MECHANISM, traced in the source rather than guessed at:
 *
 * A keyboard group is released by OWNER - findGroupByNote(midiNote) - because
 * two octaves of one key share a cell, so the cell cannot identify it. A glide
 * hands that ownership to the newer key (g->midiNote = owner). Both rules are
 * right on their own; together they leave a gap.
 *
 * When a press CANNOT glide, noteOnCell() falls through to startGroup() and a
 * second group begins, while fLastKeyboardNote follows only the newest key.
 * Nothing points at the older group any more and the key that owned it is
 * already up, so no release can reach it. And a glide is refused far more often
 * than it looks:
 *
 *     bool canGlideBetween(...) { return fGlideMode == kGlideMpe && ...; }
 *
 * Outside MPE the answer is ALWAYS false, so outside MPE every overlap that
 * cannot be handed over strands a group. Hence the need for rapid repetition to
 * notice: one stranded chord hides under the next, twenty do not.
 *
 * These tests work on the two rules that actually decide whether a pitch is ever
 * released, transcribed from startGroup() and stopGroup(), because the fault is
 * in the refcount and not in the chord maths:
 *
 *     on : needsSend = (mpe || !dup || retriggerDuplicates)
 *          if (needsSend && !send) continue;   ++fHeld[note]
 *     off: fHeld == 1 -> send note-off, decrement
 *          fHeld  > 1 -> decrement ONLY, no message
 *
 * Two distinct failures fall out, and BOTH matter:
 *
 *   - a pitch left sounding, which is what was heard; and
 *   - a pitch left with a phantom REFERENCE, silent but permanent, because
 *     fHeld > 1 suppresses the note-off, so once a count can no longer reach
 *     zero that pitch never sounds again for the rest of the session.
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

/* ------------------------------------------------------------------------ */
/*
 * The instrument's view, driven by the plugin's own refcount rules.
 *
 * `sounding` is what a listener would hear: set by a note-on that really went
 * out, cleared by a note-off that really went out. `held` is fHeld.
 */
struct Instrument {
    int  held[128]     = {0};
    int  sounding[128] = {0};
    bool mpe           = false;

    void on(const int* notes, int n, bool retriggerDuplicates)
    {
        for (int i = 0; i < n; ++i) {
            const int  note = notes[i];
            const bool dup  = (held[note] > 0);
            if (mpe || ! dup || retriggerDuplicates)
                sounding[note] = 1;
            ++held[note];
        }
    }

    void off(const int* notes, int n)
    {
        for (int i = 0; i < n; ++i) {
            const int note = notes[i];
            if (mpe) {
                sounding[note] = 0;
                if (held[note] > 0) --held[note];
            } else if (held[note] == 1) {
                sounding[note] = 0;
                --held[note];
            } else if (held[note] > 1) {
                --held[note];          /* suppressed: someone else wants it */
            }
        }
    }

    int stuck() const
    {
        int n = 0;
        for (int i = 0; i < 128; ++i) if (sounding[i]) ++n;
        return n;
    }

    int refs() const
    {
        int n = 0;
        for (int i = 0; i < 128; ++i) n += held[i];
        return n;
    }

    /* The backstop, valid only with every key up: anything still counted is
     * stranded by definition, so clear it and silence the pitch. */
    void sweep()
    {
        for (int i = 0; i < 128; ++i) {
            if (held[i] == 0) continue;
            held[i]     = 0;
            sounding[i] = 0;
        }
    }
};

/* ------------------------------------------------------------------------ */
/* Keyboard groups, owned by a key, as the plugin keeps them.                 */

static constexpr int kGroups = 8;

struct Group {
    bool active = false;
    int  owner  = -1;
    int  root   = 0;
    int  n      = 0;
    int  note[4] = {0};
};

struct Keys {
    int note[16] = {0};
    int count    = 0;

    void push(int n)
    {
        for (int i = 0; i < count; ++i) if (note[i] == n) return;
        if (count < 16) note[count++] = n;
    }
    void remove(int n)
    {
        for (int i = 0; i < count; ++i) {
            if (note[i] == n) {
                for (int j = i; j < count - 1; ++j) note[j] = note[j + 1];
                --count;
                return;
            }
        }
    }
    int  top() const { return count > 0 ? note[count - 1] : -1; }
    bool isHeld(int n) const
    {
        for (int i = 0; i < count; ++i) if (note[i] == n) return true;
        return false;
    }
};

struct Synth {
    Instrument inst;
    Keys       keys;
    Group      group[kGroups];
    int        lastNote = -1;
    bool       reclaim;      /* the fix under test */
    bool       canGlide;     /* canGlideBetween(): MPE only, in the real code */

    Synth(bool fix, bool glide, bool mpe) : reclaim(fix), canGlide(glide)
    {
        inst.mpe = mpe;
    }

    /* Cells differ per key; only that they differ matters here. */
    static int rootFor(int midiNote)
    {
        int pos; Ring ring;
        if (! cellForMidiNote(kDefaultKeyMap, midiNote, 0, pos, ring))
            return -1;
        /* The interval this cell sits above the tonic - what cellAboveTonic()
         * computes in the plugin, built here from the theory layer since that
         * method belongs to the plugin class. */
        return intervalAboveTonic(rootForPosition(pos, ring), 0);
    }

    Group* byOwner(int owner)
    {
        for (int i = 0; i < kGroups; ++i)
            if (group[i].active && group[i].owner == owner) return &group[i];
        return nullptr;
    }

    Group* alloc()
    {
        for (int i = 0; i < kGroups; ++i)
            if (! group[i].active) return &group[i];
        return nullptr;
    }

    void start(int root, int owner, bool retriggerDuplicates)
    {
        Group* g = alloc();
        if (g == nullptr) return;
        g->active = true;
        g->owner  = owner;
        g->root   = root;
        g->n      = 3;
        for (int i = 0; i < 3; ++i) g->note[i] = 60 + root + i * 4;
        inst.on(g->note, g->n, retriggerDuplicates);
    }

    void stop(Group* g)
    {
        if (g == nullptr || ! g->active) return;
        inst.off(g->note, g->n);
        g->active = false;
        g->owner  = -1;
    }

    /* THE FIX: a group whose owning key is up can never be released. */
    void reclaimOrphans()
    {
        if (! reclaim) return;
        for (int i = 0; i < kGroups; ++i) {
            if (! group[i].active || group[i].owner < 0) continue;
            if (keys.isHeld(group[i].owner)) continue;
            stop(&group[i]);
        }
        if (keys.count == 0) {
            bool any = false;
            for (int i = 0; i < kGroups; ++i)
                if (group[i].active && group[i].owner >= 0) any = true;
            if (! any) inst.sweep();
        }
    }

    void noteOn(int n)
    {
        keys.push(n);
        const int root = rootFor(n);
        if (root < 0) return;

        Group* dup = byOwner(n);
        if (dup != nullptr) stop(dup);

        /* Glide hands the phrase over WITHOUT starting a group. */
        Group* from = (lastNote >= 0) ? byOwner(lastNote) : nullptr;
        if (from != nullptr && from->owner != n && canGlide && root != from->root) {
            from->owner = n;
            from->root  = root;
            lastNote    = n;
            return;
        }

        /* Refused: a second group begins and `from` is left behind. */
        reclaimOrphans();
        start(root, n, ! canGlide);
        lastNote = n;
    }

    void noteOff(int n)
    {
        keys.remove(n);

        Group* g = byOwner(n);
        if (g == nullptr) {
            if (lastNote == n) lastNote = keys.top();
            reclaimOrphans();
            return;
        }

        const int fallback = keys.top();
        if (fallback >= 0 && fallback != n && byOwner(fallback) == nullptr) {
            const int root = rootFor(fallback);
            if (root >= 0) {
                if (canGlide && root != g->root) {
                    g->owner = fallback;
                    g->root  = root;
                    lastNote = fallback;
                    reclaimOrphans();
                    return;
                }
                stop(g);
                start(root, fallback, true);
                lastNote = fallback;
                reclaimOrphans();
                return;
            }
        }

        stop(g);
        if (lastNote == n) lastNote = keys.top();
        reclaimOrphans();
    }

    int activeGroups() const
    {
        int n = 0;
        for (int i = 0; i < kGroups; ++i) if (group[i].active) ++n;
        return n;
    }
};

/* ------------------------------------------------------------------------ */

struct Result { int stuck; int refs; int groups; };

/*
 * The reported gesture, played through the group model.
 *
 * NOTE ON WHAT THIS DOES AND DOES NOT PROVE. The leak is demonstrated against
 * the real refcount rules in the first block of main(), which is the level it
 * lives at. This driver exercises the surrounding group bookkeeping and is a
 * guard against the ORDINARY cases regressing - it does not reproduce the leak,
 * because the model retires a group whose key is re-pressed and its fallback
 * always re-owns, so it cannot reach the state where nothing points at a group.
 * Reproducing that faithfully needs the plugin itself, not a transcription.
 */
static Result alternate(bool fix, bool glide, bool mpe, int a, int b, int reps)
{
    Synth s(fix, glide, mpe);

    s.noteOn(a);
    for (int i = 0; i < reps; ++i) {
        s.noteOn(b);
        s.noteOn(a);      /* retrigger while b is still held */
        s.noteOff(b);
        s.noteOn(b);
        s.noteOff(a);
        s.noteOn(a);
        s.noteOff(b);
    }
    s.noteOff(a);

    return { s.inst.stuck(), s.inst.refs(), s.activeGroups() };
}

int main()
{
    /* Two groups on one chord, then only one released - the exact shape the
     * fall-through produces. This is the whole bug in six lines. */
    std::printf("=== two groups on one chord, one release lost ===\n");
    {
        const int C[3] = { 60, 64, 67 };
        for (int m = 0; m < 2; ++m) {
            Instrument inst;
            inst.mpe = (m == 1);

            inst.on(C, 3, false);      /* group A: real note-ons */
            inst.on(C, 3, false);      /* group B: duplicates, suppressed */
            inst.off(C, 3);            /* B released; A's release never comes */

            char detail[96];
            std::snprintf(detail, sizeof(detail), "%s: %d sounding, %d refs left",
                          inst.mpe ? "MPE    " : "non-MPE", inst.stuck(), inst.refs());
            /* Either failure is fatal: a sounding note is heard, and a phantom
             * reference permanently mutes that pitch. */
            ok("a lost release leaves damage", inst.stuck() > 0 || inst.refs() > 0,
               detail);

            inst.sweep();
            ok("  and the sweep repairs it", inst.stuck() == 0 && inst.refs() == 0, "");
        }
    }

    std::printf("\n=== the simple cases still behave ===\n");
    {
        Synth s(true, false, false);
        s.noteOn(60);
        ok("one key sounds", s.inst.stuck() > 0, "");
        s.noteOff(60);
        ok("releasing it silences everything", s.inst.stuck() == 0, "");
        ok("no group survives", s.activeGroups() == 0, "");
        ok("no reference survives", s.inst.refs() == 0, "");
    }
    {
        Synth s(true, false, false);
        s.noteOn(60);
        s.noteOn(62);
        s.noteOff(60);
        ok("the still-held key keeps sounding", s.inst.stuck() > 0, "62 down");
        s.noteOff(62);
        ok("the last release silences it", s.inst.stuck() == 0, "");
        ok("and clears every reference", s.inst.refs() == 0, "");
    }

    /* The reported gesture, in every glide mode, at the reported repetition. */
    std::printf("\n=== rapid alternation, as reported ===\n");
    {
        const int reps[] = { 1, 2, 5, 20 };
        for (int m = 0; m < 2; ++m) {
            const bool mpe = (m == 1);
            for (int r = 0; r < 4; ++r) {
                const Result broken = alternate(false, mpe, mpe, 60, 62, reps[r]);
                const Result fixed  = alternate(true,  mpe, mpe, 60, 62, reps[r]);

                char detail[128];
                std::snprintf(detail, sizeof(detail),
                              "%s %2d reps: was %d/%d (snd/refs), now %d/%d",
                              mpe ? "MPE    " : "non-MPE", reps[r],
                              broken.stuck, broken.refs, fixed.stuck, fixed.refs);
                ok("alternation leaves nothing behind",
                   fixed.stuck == 0 && fixed.refs == 0 && fixed.groups == 0, detail);
                /* The unfixed path must be no worse here, since this driver
                 * does not reach the leaking state either way. */
                ok("  and the reclaim changes nothing it should not",
                   broken.stuck == fixed.stuck, "");
            }
        }
    }

    /* Every mapped pair, so the fix is not specific to C and D. */
    std::printf("\n=== across every mapped key pair ===\n");
    {
        int  tested = 0, leaked = 0;
        char worst[80] = "none";

        for (int m = 0; m < 2; ++m) {
            const bool mpe = (m == 1);
            for (int a = 60; a <= 71; ++a) {
                for (int b = 60; b <= 71; ++b) {
                    if (a == b) continue;
                    if (Synth::rootFor(a) < 0 || Synth::rootFor(b) < 0) continue;
                    ++tested;
                    const Result r = alternate(true, mpe, mpe, a, b, 7);
                    if (r.stuck != 0 || r.refs != 0 || r.groups != 0) {
                        ++leaked;
                        std::snprintf(worst, sizeof(worst),
                                      "%d/%d %s left %d snd %d refs",
                                      a, b, mpe ? "MPE" : "non-MPE", r.stuck, r.refs);
                    }
                }
            }
        }

        char detail[128];
        std::snprintf(detail, sizeof(detail), "%d pairs, %d leaked (%s)",
                      tested, leaked, worst);
        ok("no pair strands anything", leaked == 0, detail);
        ok("the sweep actually ran", tested > 40, "");
    }

    /* Three keys: a fallback chain is where ownership is easiest to drop. */
    std::printf("\n=== a three-key roll ===\n");
    {
        for (int m = 0; m < 2; ++m) {
            Synth s(true, m == 1, m == 1);
            s.noteOn(60); s.noteOn(62); s.noteOn(64);
            s.noteOff(62);              /* from the MIDDLE of the stack */
            s.noteOff(64);
            s.noteOff(60);
            char detail[64];
            std::snprintf(detail, sizeof(detail), "%s", m == 1 ? "MPE" : "non-MPE");
            ok("every key up leaves silence", s.inst.stuck() == 0, detail);
            ok("  and no stranded reference", s.inst.refs() == 0, "");
            ok("  and no surviving group", s.activeGroups() == 0, "");
        }
    }

    /*
     * The reclaim must not cut short a key that is still down - the one way a
     * fix like this could do harm.
     */
    std::printf("\n=== a held key is never reclaimed ===\n");
    {
        Synth s(true, false, false);
        s.noteOn(60);
        s.noteOn(62);
        s.noteOff(60);              /* 62 still down: reclaim runs here */
        ok("the held key still sounds", s.inst.stuck() > 0, "62 down");
        ok("and still owns a group", s.byOwner(62) != nullptr, "");
        s.noteOff(62);
        ok("only its own release ends it", s.inst.stuck() == 0, "");
    }

    std::printf("\n%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
