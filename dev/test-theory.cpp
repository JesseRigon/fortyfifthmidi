/*
 * Standalone check of the wheel geometry. CircleTheory.hpp deliberately pulls in
 * no plugin types, so it compiles and runs on its own:
 *
 *   g++ -std=c++17 -I src dev/test-theory.cpp -o /tmp/test-theory && /tmp/test-theory
 *
 * The minor-ring expectation below is the authoritative 24-cell reading of the
 * wheel, clockwise from twelve o'clock (Em, in line with C).
 */

#include "CircleTheory.hpp"

#include <cstdio>
#include <cstring>

using namespace fortyfifth;

static const char* kPitch[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static int failures = 0;

static void check(const char* what, int got, int want)
{
    const bool ok = (got == want);
    if (ok) {
        std::printf("  ok    %-30s %s\n", what, kPitch[got]);
    } else {
        std::printf("  FAIL  %-30s got %s, want %s\n",
                    what, kPitch[got], kPitch[want]);
        ++failures;
    }
}

/* Like check(), but for values that are plain numbers rather than pitch
 * classes - printing an octave as a note name would be gibberish. */
static void checkInt(const char* what, int got, int want)
{
    if (got == want) {
        std::printf("  ok    %-30s %d\n", what, got);
    } else {
        std::printf("  FAIL  %-30s got %d, want %d\n", what, got, want);
        ++failures;
    }
}

static void checkStr(const char* what, const char* got, const char* want)
{
    const bool ok = (got != nullptr && std::strcmp(got, want) == 0);
    if (ok) {
        std::printf("  ok    %-30s %s\n", what, got);
    } else {
        std::printf("  FAIL  %-30s got %s, want %s\n",
                    what, got ? got : "(null)", want);
        ++failures;
    }
}

int main()
{
    /* The full minor ring as read off the wheel, clockwise from Em at twelve
     * o'clock. Spelling is theoretical (E#m, B#m), but only pitch class is
     * compared here. */
    static const struct { const char* name; int pc; } kMinorRing[24] = {
        {"Em",4},  {"Am",9},  {"Bm",11}, {"Em",4},   {"F#m",6}, {"Bm",11},
        {"C#m",1}, {"F#m",6}, {"G#m",8}, {"C#m",1},  {"D#m",3}, {"G#m",8},
        {"A#m",10},{"D#m",3}, {"E#m",5}, {"A#m",10}, {"B#m",0}, {"Fm",5},
        {"Gm",7},  {"Cm",0},  {"Dm",2},  {"Gm",7},   {"Am",9},  {"Dm",2}
    };

    std::printf("=== minor ring: all 24 cells ===\n");
    for (int i = 0; i < 24; ++i) {
        char label[32];
        std::snprintf(label, sizeof(label), "cell %2d (%s)", i, kMinorRing[i].name);
        check(label, rootForPosition(i, kRingMinor), kMinorRing[i].pc);
    }

    std::printf("\n=== key ring ===\n");
    check("position 11 (IV of C = F)", rootForPosition(11, kRingKey), 5);
    check("position  0 (I  of C = C)", rootForPosition(0,  kRingKey), 0);
    check("position  1 (V  of C = G)", rootForPosition(1,  kRingKey), 7);

    std::printf("\n=== dim ring ===\n");
    check("position 0 (vii of C = B)", rootForPosition(0, kRingDim), 11);
    check("position 1 (vii of G = F#)", rootForPosition(1, kRingDim), 6);

    std::printf("\n=== degrees within key C ===\n");
    checkStr("key cell 11",   degreeInKey(11, kRingKey, 0),   "IV");
    checkStr("key cell 0",    degreeInKey(0,  kRingKey, 0),   "I");
    checkStr("key cell 1",    degreeInKey(1,  kRingKey, 0),   "V");
    checkStr("minor cell 23", degreeInKey(23, kRingMinor, 0), "ii");
    checkStr("minor cell 0",  degreeInKey(0,  kRingMinor, 0), "iii");
    checkStr("minor cell 1",  degreeInKey(1,  kRingMinor, 0), "vi");
    checkStr("dim cell 0",    degreeInKey(0,  kRingDim, 0),   "vii°");

    /* The complete wedge for C must be F C G / Dm Em Am / Bdim. */
    std::printf("\n=== full wedge for key C ===\n");
    check("IV  = F",  rootForPosition(11, kRingKey),   5);
    check("I   = C",  rootForPosition(0,  kRingKey),   0);
    check("V   = G",  rootForPosition(1,  kRingKey),   7);
    check("ii  = Dm", rootForPosition(23, kRingMinor), 2);
    check("iii = Em", rootForPosition(0,  kRingMinor), 4);
    check("vi  = Am", rootForPosition(1,  kRingMinor), 9);
    check("vii = B",  rootForPosition(0,  kRingDim),  11);

    /* And for G: C G D / Am Bm Em / F#dim. */
    std::printf("\n=== full wedge for key G ===\n");
    check("IV  = C",  rootForPosition(0, kRingKey),   0);
    check("I   = G",  rootForPosition(1, kRingKey),   7);
    check("V   = D",  rootForPosition(2, kRingKey),   2);
    check("ii  = Am", rootForPosition(1, kRingMinor), 9);
    check("iii = Bm", rootForPosition(2, kRingMinor), 11);
    check("vi  = Em", rootForPosition(3, kRingMinor), 4);
    check("vii = F#", rootForPosition(1, kRingDim),   6);

    /* Extensions must respect the cell's own quality, not override it. */
    std::printf("\n=== extensions extend, they do not override ===\n");
    struct { ChordType base; Extension ext; ChordType want; const char* what; } kExt[] = {
        { kChordMinor, kExt7,    kChordMinor7,   "Em + 7  -> Em7 (not E7)" },
        { kChordMajor, kExt7,    kChordMajor7,   "C  + 7  -> Cmaj7"        },
        { kChordDim,   kExt7,    kChordMinor7b5, "B° + 7  -> Bm7b5"   },
        { kChordMinor, kExt6,    kChordMinor6,   "Em + 6  -> Em6"          },
        { kChordMajor, kExt9,    kChordMajor9,   "C  + 9  -> Cmaj9"        },
        { kChordMinor, kExt9,    kChordMinor9,   "Em + 9  -> Em9"          },
        { kChordMajor, kExtNone, kChordMajor,    "C  + -  -> C"            },
    };
    for (const auto& t : kExt) {
        const ChordType got = extendChord(t.base, t.ext);
        if (got == t.want) {
            std::printf("  ok    %s\n", t.what);
        } else {
            std::printf("  FAIL  %s  (got %s, want %s)\n",
                        t.what, kChordShape[got].name, kChordShape[t.want].name);
            ++failures;
        }
    }

    /* sameShape decides whether a single bend can carry the move. */
    std::printf("\n=== shape comparison gates single-bend glide ===\n");
    struct { ChordType a, b; bool want; const char* what; } kShape[] = {
        { kChordMajor,  kChordMajor,  true,  "C -> G  (both major)"        },
        { kChordMinor,  kChordMinor,  true,  "Em -> Am (both minor)"       },
        { kChordMajor,  kChordMinor,  false, "C -> Em  (major to minor)"   },
        { kChordMajor,  kChordDim,    false, "C -> B° (major to dim)" },
        { kChordMinor7, kChordMinor7, true,  "Em7 -> Am7"                  },
        { kChordMinor,  kChordMinor7, false, "Em -> Am7 (3 vs 4 notes)"    },
    };
    for (const auto& t : kShape) {
        const bool got = sameShape(t.a, t.b);
        if (got == t.want) {
            std::printf("  ok    %-32s %s\n", t.what, got ? "bend" : "retrigger");
        } else {
            std::printf("  FAIL  %-32s got %s\n", t.what, got ? "bend" : "retrigger");
            ++failures;
        }
    }

    /*
     * Keyboard mapping. The real check is not that the table says what it says,
     * but that the cell it lands on genuinely PLAYS that degree - decoded by
     * degreeInKey(), which the wheel's own highlighting uses. If the two ever
     * disagree the keyboard would play chords the wheel does not light up.
     *
     * Checked in all 12 keys, because the offsets are relative and an error in
     * one ring's cell arithmetic would show up only after transposing.
     */
    std::printf("\n=== keyboard mapping: degree agrees in all 12 keys ===\n");
    struct { int note; const char* degree; const char* name; } kMap[] = {
        { 4,  "III",  "E  -> III"  },
        { 5,  "IV",   "F  -> IV"   },
        { 7,  "I",    "G  -> I"    },
        { 9,  "V",    "A  -> V"    },
        { 11, "II",   "B  -> II"   },
        { 3,  "vii°", "D# -> vii°" },
        { 6,  "ii",   "F# -> ii"   },
        { 8,  "iii",  "G# -> iii"  },
        { 10, "vi",   "A# -> vi"   },
    };

    for (const auto& m : kMap) {
        bool allKeys = true;
        for (int key = 0; key < 12 && allKeys; ++key) {
            int  pos;
            Ring ring;
            if (! cellForMidiNote(m.note, key, pos, ring)) {
                allKeys = false;
                break;
            }
            const char* deg = degreeInKey(pos, ring, key);
            if (deg == nullptr || std::strcmp(deg, m.degree) != 0)
                allKeys = false;
        }

        if (allKeys) {
            std::printf("  ok    %-30s %s in every key\n", m.name, m.degree);
        } else {
            std::printf("  FAIL  %-30s does not resolve to %s\n",
                        m.name, m.degree);
            ++failures;
        }
    }

    std::printf("\n=== unmapped keys stay silent ===\n");
    for (int pc = 0; pc <= 2; ++pc) {
        int  pos;
        Ring ring;
        if (! cellForMidiNote(pc, 0, pos, ring)) {
            std::printf("  ok    %-30s ignored\n", kPitch[pc]);
        } else {
            std::printf("  FAIL  %-30s should be unmapped\n", kPitch[pc]);
            ++failures;
        }
    }

    /* The controller's octave is absolute - middle C (60) is octave 4. */
    std::printf("\n=== octave follows the controller ===\n");
    checkInt("note 60 -> octave 4", octaveForMidiNote(60), 4);
    checkInt("note 48 -> octave 3", octaveForMidiNote(48), 3);
    checkInt("note 72 -> octave 5", octaveForMidiNote(72), 5);

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "PASS" : "FAIL",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
