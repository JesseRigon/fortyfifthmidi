/*
 * Overlapping keys, the way a classic monosynth handles them.
 *
 * Reported: "hitting one key then overlapping another works but if I still
 * keep the first key down then release key 2 should glide back to the previous
 * chord and make sure the highlighting is tracked correctly too."
 *
 * The rule is the last-note-priority stack every mono synth has used since the
 * Minimoog: the newest key down owns the voice, and releasing it falls back to
 * whichever key is still held rather than silencing the phrase.
 *
 * The stack itself was already right, and the release path already handed the
 * group back. What it did not do was move the SOUND - it reassigned ownership
 * and nothing else, so the voice kept playing the released key's chord and the
 * highlight stayed on its cell. Lifting a finger changed nothing audible or
 * visible.
 *
 * These tests model the stack and the group's identity together, because the
 * bug lived in the gap between them.
 */
#include <cstdio>
#include <cstring>
#include "CircleTheory.hpp"

using namespace fortyfifth;

static int failures = 0;

static void ok(const char* what, bool cond, const char* detail)
{
    if (cond) {
        std::printf("  ok    %-48s %s\n", what, detail);
    } else {
        std::printf("  FAIL  %-48s %s\n", what, detail);
        ++failures;
    }
}

/* ------------------------------------------------------------------------ */
/* The held-key stack, transcribed from FortyFifthPlugin.                    */

struct HeldKeys {
    static constexpr int kMax = 16;
    int  note[kMax] = {0};
    int  count      = 0;

    void push(int n)
    {
        for (int i = 0; i < count; ++i)
            if (note[i] == n) return;
        if (count < kMax) note[count++] = n;
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

    int top() const { return count > 0 ? note[count - 1] : -1; }
};

/*
 * One voice, as the plugin keeps it: which key owns it, and which cell it is
 * sounding. Those two must move together - the whole bug was that they did not.
 */
struct Voice {
    bool active  = false;
    int  owner   = -1;    /* midiNote of the key holding it */
    int  cell    = -1;    /* packed ring|position it is sounding */
};

struct Synth {
    HeldKeys held;
    Voice    v;
    int      lastNote = -1;
    int      glides   = 0;   /* how many times the voice glided */
    int      starts   = 0;   /* how many times a fresh voice began */

    /* Which cell a key maps to. The identity of the mapping does not matter
     * here, only that different keys give different cells. */
    static int cellFor(int midiNote)
    {
        int pos; Ring ring;
        if (! cellForMidiNote(kDefaultKeyMap, midiNote, 0, pos, ring))
            return -1;
        return (static_cast<int>(ring) << 8) | pos;
    }

    void noteOn(int n)
    {
        held.push(n);
        const int c = cellFor(n);
        if (c < 0) return;

        if (v.active && lastNote >= 0 && v.owner != n) {
            /* A newer key takes the voice over and glides it. */
            v.owner = n;
            v.cell  = c;
            ++glides;
        } else {
            v.active = true;
            v.owner  = n;
            v.cell   = c;
            ++starts;
        }
        lastNote = n;
    }

    void noteOff(int n)
    {
        held.remove(n);

        if (! v.active || v.owner != n) {
            if (lastNote == n) lastNote = held.top();
            return;
        }

        const int fallback = held.top();
        if (fallback >= 0 && fallback != n) {
            /* FALL BACK: the phrase returns to the key still held, and the
             * voice glides there rather than merely changing hands. */
            const int c = cellFor(fallback);
            if (c >= 0) {
                v.owner = fallback;
                v.cell  = c;
                ++glides;
                lastNote = fallback;
                return;
            }
        }

        v.active = false;
        v.owner  = -1;
        v.cell   = -1;
        lastNote = held.top();
    }

    /* The lit cell is derived from the voice, as in the plugin. */
    int litCell() const { return v.active ? v.cell : -1; }
};

/* White keys, so each maps to a degree. C4=60. */
static constexpr int kC = 60, kD = 62, kE = 64, kF = 65, kG = 67;

int main()
{
    char d[96];

    /* --- the reported sequence ---------------------------------------- */
    std::printf("=== press C, press G, release G ===\n");
    {
        Synth s;
        const int cellC = Synth::cellFor(kC);
        const int cellG = Synth::cellFor(kG);

        s.noteOn(kC);
        ok("C sounds, and lights its cell", s.litCell() == cellC, "I");

        s.noteOn(kG);
        ok("G takes over the voice",      s.v.owner == kG,       "newest wins");
        ok("and the light moves to G",    s.litCell() == cellG,  "V");

        s.noteOff(kG);
        ok("the voice returns to C",      s.v.owner == kC,       "C still held");
        ok("IT GLIDES BACK, not silent",  s.v.active,            "still sounding");
        ok("and the light follows it",    s.litCell() == cellC,  "back to I");

        s.noteOff(kC);
        ok("releasing the last key stops it", ! s.v.active,      "silence");
        ok("and nothing stays lit",       s.litCell() == -1,     "dark");
    }

    /* --- release the OLDER key instead -------------------------------- */
    std::printf("\n=== press C, press G, release C ===\n");
    {
        Synth s;
        const int cellG = Synth::cellFor(kG);

        s.noteOn(kC);
        s.noteOn(kG);
        s.noteOff(kC);          /* the key that is NOT holding the voice */

        ok("the voice stays with G",  s.v.owner == kG,      "unaffected");
        ok("and keeps sounding",      s.v.active,           "no interruption");
        ok("the light stays on G",    s.litCell() == cellG, "V");

        s.noteOff(kG);
        ok("then it stops",           ! s.v.active,         "silence");
    }

    /* --- three deep ---------------------------------------------------- */
    std::printf("\n=== three keys, released newest first ===\n");
    {
        Synth s;
        s.noteOn(kC);
        s.noteOn(kE);
        s.noteOn(kG);
        ok("the newest owns it", s.v.owner == kG, "G");

        s.noteOff(kG);
        ok("falls back to E", s.v.owner == kE && s.v.active, "E still held");
        ok("  and lights E",  s.litCell() == Synth::cellFor(kE), "iii");

        s.noteOff(kE);
        ok("falls back to C", s.v.owner == kC && s.v.active, "C still held");
        ok("  and lights C",  s.litCell() == Synth::cellFor(kC), "I");

        s.noteOff(kC);
        ok("then silence",    ! s.v.active, "all released");
        ok("  nothing lit",   s.litCell() == -1, "dark");
    }

    /* --- released out of order ----------------------------------------- */
    std::printf("\n=== three keys, released from the middle ===\n");
    {
        Synth s;
        s.noteOn(kC);
        s.noteOn(kE);
        s.noteOn(kG);

        s.noteOff(kE);      /* not the owner, and not the bottom */
        ok("the voice is undisturbed", s.v.owner == kG && s.v.active, "G");
        ok("the light too",            s.litCell() == Synth::cellFor(kG), "V");

        s.noteOff(kG);
        ok("now it falls back past the gap", s.v.owner == kC, "to C");
        ok("  lighting C",                   s.litCell() == Synth::cellFor(kC), "I");
    }

    /* --- the light NEVER contradicts the sound -------------------------- */
    std::printf("\n=== the light always agrees with the voice ===\n");
    {
        Synth s;
        static const int seq[] = { kC, kD, kE, kF, kG };
        bool agreed = true;

        /* Press all five, then release them newest-first, checking at every
         * step that what is lit is what the voice is sounding. */
        for (int i = 0; i < 5; ++i) {
            s.noteOn(seq[i]);
            if (s.litCell() != s.v.cell) agreed = false;
            if (s.litCell() != Synth::cellFor(seq[i])) agreed = false;
        }
        for (int i = 4; i >= 0; --i) {
            s.noteOff(seq[i]);
            if (s.v.active) {
                if (s.litCell() != s.v.cell) agreed = false;
                /* The owner must be a key that is genuinely still down. */
                bool ownerHeld = false;
                for (int j = 0; j < s.held.count; ++j)
                    if (s.held.note[j] == s.v.owner) ownerHeld = true;
                if (! ownerHeld) agreed = false;
            } else if (s.litCell() != -1) {
                agreed = false;
            }
        }
        ok("through five presses and five releases", agreed, "never diverged");
        ok("and it ends silent",  ! s.v.active && s.litCell() == -1, "dark");
    }

    /* --- it glides rather than retriggering ----------------------------- */
    std::printf("\n=== falling back glides, it does not restart ===\n");
    {
        Synth s;
        s.noteOn(kC);
        const int startsAfterFirst = s.starts;

        s.noteOn(kG);
        s.noteOff(kG);

        std::snprintf(d, sizeof d, "%d starts, %d glides", s.starts, s.glides);
        ok("no new voice was started", s.starts == startsAfterFirst, d);
        ok("two glides: there and back", s.glides == 2, d);
    }

    /* --- the stack itself ----------------------------------------------- */
    std::printf("\n=== the held-key stack ===\n");
    {
        HeldKeys h;
        h.push(kC); h.push(kE); h.push(kG);
        ok("top is the newest", h.top() == kG, "G");

        h.push(kE);                       /* already down */
        std::snprintf(d, sizeof d, "%d held", h.count);
        ok("a repeated press does not duplicate", h.count == 3, d);

        h.remove(kE);
        ok("removing a middle key keeps order", h.top() == kG, "G still top");

        h.remove(kG);
        ok("then the older one surfaces", h.top() == kC, "C");

        h.remove(kC);
        ok("and empty means -1", h.top() == -1, "nothing held");

        h.remove(kC);                     /* already gone */
        ok("removing an absent key is harmless", h.count == 0, "0 held");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
