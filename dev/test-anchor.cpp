/*
 * Where a chord sounds, in every key.
 *
 * Reported: "on key of C all the chords work well but if we take Ab key as an
 * example then it breaks. the chord shapes work well but they don't stick to
 * the correct octave range."
 *
 * Exactly so, and "the shapes work well" is the clue - the intervals were
 * always right, only their placement was wrong.
 *
 * A PITCH CLASS CANNOT SAY WHICH OCTAVE IT BELONGS IN. It is 0..11 with C at
 * zero, and that zero is an accident of notation: nothing about A flat makes C
 * its floor. buildChord() used to add the pitch class to an octave floor,
 * which placed a chord by where its root happened to fall in the C-based
 * ordering rather than by its relationship to the key.
 *
 * In C that is invisible, because C's tonic IS 0 and every diatonic degree
 * ascends from it. In A flat the tonic is 8, so IV (C sharp, 1) and V (E flat,
 * 3) wrapped to the BOTTOM of the same octave - the subdominant sounded below
 * the tonic. Seven of twelve keys voiced a I ii IV V differently from C.
 *
 * The fix is in what a root MEANS: an interval above the key's tonic, with the
 * tonic on the octave floor. These tests hold that line.
 */
#include <cstdio>
#include <cstring>
#include "CircleTheory.hpp"

using namespace fortyfifth;

static int failures = 0;

static void ok(const char* what, bool cond, const char* detail)
{
    if (cond) {
        std::printf("  ok    %-46s %s\n", what, detail);
    } else {
        std::printf("  FAIL  %-46s %s\n", what, detail);
        ++failures;
    }
}

static const char* kPitch[12] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static void noteName(int n, char* out, size_t cap)
{
    std::snprintf(out, cap, "%s%d", kPitch[((n % 12) + 12) % 12], (n / 12) - 1);
}

/* Build a degree of a key, the way the plugin does. */
static int buildDegree(Degree d, int keyIndex, int octave,
                       uint8_t* out, ChordType* outType = nullptr)
{
    int pos; Ring ring;
    cellForDegree(d, keyIndex, pos, ring);

    const int tonicPc = rootForPosition(keyIndex, kRingKey);
    const int above   = intervalAboveTonic(rootForPosition(pos, ring), tonicPc);
    const ChordType t = (ring == kRingMinor) ? kChordMinor
                      : (ring == kRingDim)   ? kChordDim : kChordMajor;
    if (outType != nullptr) *outType = t;

    return buildChord(above, t, tonicMidi(tonicPc, octave), out, kMaxChordTones);
}

int main()
{
    char d[96];

    /* --- the primitives ---------------------------------------------- */
    std::printf("=== interval above the tonic ===\n");
    ok("a tonic is zero above itself", intervalAboveTonic(8, 8) == 0, "Ab in Ab");
    ok("IV of C is five up",           intervalAboveTonic(5, 0) == 5, "F in C");
    ok("IV of Ab is FIVE UP, not seven down",
       intervalAboveTonic(1, 8) == 5, "C# in Ab");
    ok("V of Ab is seven up",          intervalAboveTonic(3, 8) == 7, "Eb in Ab");
    ok("vii of C is eleven up",        intervalAboveTonic(11, 0) == 11, "B in C");
    ok("never negative",               intervalAboveTonic(0, 11) == 1, "C above B");
    ok("always under an octave",       intervalAboveTonic(11, 0) < 12, "0..11");

    std::printf("\n=== the tonic's own note ===\n");
    ok("C4 is MIDI 60",  tonicMidi(0, 4) == 60, "middle C");
    ok("Ab4 is MIDI 68", tonicMidi(8, 4) == 68, "8 above it");
    ok("C3 is MIDI 48",  tonicMidi(0, 3) == 48, "an octave down");

    /*
     * --- the reported bug -------------------------------------------------
     *
     * A progression is the same SHAPE in every key. Its degrees sit at fixed
     * intervals from the tonic, so the pattern of distances between the chords
     * must not change when the key does.
     */
    std::printf("\n=== a progression keeps its shape in all 12 keys ===\n");
    {
        static const Degree prog[4] = { kDegreeI, kDegreeII, kDegreeIV, kDegreeV };

        int ref[4] = {0};
        int wrong  = 0;

        for (int k = 0; k < 12; ++k) {
            int rel[4], tonicNote = 0;

            for (int i = 0; i < 4; ++i) {
                uint8_t notes[kMaxChordTones];
                buildDegree(prog[i], k, 4, notes);
                if (i == 0) tonicNote = notes[0];
                rel[i] = notes[0] - tonicNote;
            }

            if (k == 0) std::memcpy(ref, rel, sizeof ref);
            if (std::memcmp(rel, ref, sizeof ref) != 0) {
                ++wrong;
                std::printf("        %-3s voices it as %d %d %d %d, want %d %d %d %d\n",
                            kPitch[rootForPosition(k, kRingKey)],
                            rel[0], rel[1], rel[2], rel[3],
                            ref[0], ref[1], ref[2], ref[3]);
            }
        }

        std::snprintf(d, sizeof d, "%d keys differ from C", wrong);
        ok("I ii IV V is voiced identically everywhere", wrong == 0, d);
        ok("  and it ascends, as in C",
           ref[0] == 0 && ref[1] == 2 && ref[2] == 5 && ref[3] == 7,
           "0 2 5 7");
    }

    /* --- no degree may fall below its own tonic ----------------------- */
    std::printf("\n=== every degree sits above its tonic ===\n");
    {
        int below = 0;
        for (int k = 0; k < 12; ++k) {
            uint8_t tonicChord[kMaxChordTones];
            buildDegree(kDegreeI, k, 4, tonicChord);

            for (int deg = 0; deg < kDegreeDiatonicCount; ++deg) {
                uint8_t notes[kMaxChordTones];
                buildDegree(static_cast<Degree>(deg), k, 4, notes);
                if (notes[0] < tonicChord[0]) {
                    ++below;
                    char a[12], b[12];
                    noteName(notes[0], a, sizeof a);
                    noteName(tonicChord[0], b, sizeof b);
                    std::printf("        %-3s degree %d at %s, below its tonic %s\n",
                                kPitch[rootForPosition(k, kRingKey)], deg, a, b);
                }
            }
        }
        std::snprintf(d, sizeof d, "%d below", below);
        ok("no degree sounds under the tonic", below == 0, d);
    }

    /* The A flat case from the report, named explicitly. */
    std::printf("\n=== the reported key, A flat ===\n");
    {
        int kAb = -1;
        for (int i = 0; i < 12; ++i)
            if (rootForPosition(i, kRingKey) == 8) kAb = i;

        ok("A flat is on the wheel", kAb >= 0, "found");

        uint8_t I[kMaxChordTones], IV[kMaxChordTones], V[kMaxChordTones];
        buildDegree(kDegreeI,  kAb, 4, I);
        buildDegree(kDegreeIV, kAb, 4, IV);
        buildDegree(kDegreeV,  kAb, 4, V);

        char a[12], b[12], c[12];
        noteName(I[0], a, sizeof a);
        noteName(IV[0], b, sizeof b);
        noteName(V[0], c, sizeof c);
        std::printf("        I=%s  IV=%s  V=%s\n", a, b, c);

        ok("IV is five semitones above I",  IV[0] - I[0] == 5, b);
        ok("V is seven semitones above I",  V[0]  - I[0] == 7, c);
        ok("and IV is not below I",         IV[0] > I[0],      "was 7 below");
    }

    /* --- transposition ------------------------------------------------ */
    std::printf("\n=== changing key transposes, and nothing else ===\n");
    {
        /* The same degree in two keys differs by exactly the key distance. */
        bool clean = true;
        for (int k = 1; k < 12; ++k) {
            const int shift = intervalAboveTonic(rootForPosition(k, kRingKey),
                                                 rootForPosition(0, kRingKey));
            for (int deg = 0; deg < kDegreeDiatonicCount; ++deg) {
                uint8_t inC[kMaxChordTones], inK[kMaxChordTones];
                const int n1 = buildDegree(static_cast<Degree>(deg), 0, 4, inC);
                const int n2 = buildDegree(static_cast<Degree>(deg), k, 4, inK);
                if (n1 != n2) { clean = false; break; }
                for (int i = 0; i < n1; ++i)
                    if (inK[i] - inC[i] != shift) { clean = false; break; }
            }
        }
        ok("every degree shifts by the key distance, exactly", clean,
           "a pure transposition");
    }

    /* --- the octave control still works ------------------------------- */
    std::printf("\n=== the octave slider still moves everything ===\n");
    {
        bool clean = true;
        for (int k = 0; k < 12; ++k) {
            uint8_t lo[kMaxChordTones], hi[kMaxChordTones];
            const int a2 = buildDegree(kDegreeI, k, 3, lo);
            const int b2 = buildDegree(kDegreeI, k, 4, hi);
            if (a2 != b2) { clean = false; break; }
            for (int i = 0; i < a2; ++i)
                if (hi[i] - lo[i] != 12) { clean = false; break; }
        }
        ok("one octave up is exactly twelve semitones", clean, "all keys");
    }

    /* --- range -------------------------------------------------------- */
    std::printf("\n=== how far the whole thing spans ===\n");
    {
        int lo = 999, hi = -999;
        for (int k = 0; k < 12; ++k) {
            for (int deg = 0; deg < kDegreeDiatonicCount; ++deg) {
                uint8_t n[kMaxChordTones];
                const int c = buildDegree(static_cast<Degree>(deg), k, 4, n);
                if (n[0] < lo)   lo = n[0];
                if (n[c-1] > hi) hi = n[c-1];
            }
        }
        char a[12], b[12];
        noteName(lo, a, sizeof a);
        noteName(hi, b, sizeof b);
        std::snprintf(d, sizeof d, "%s..%s (%d semitones)", a, b, hi - lo);

        /* Anchoring widens the range - degrees stack upward instead of
         * wrapping down - but it must stay inside MIDI and stay sane. */
        ok("everything lands inside MIDI range", lo >= 0 && hi <= 127, d);
        ok("and inside three octaves",           hi - lo <= 36, d);
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
