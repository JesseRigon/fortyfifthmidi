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
        { 0,  "I",    "C -> I"    },
        { 2,  "ii",   "D -> ii"   },
        { 4,  "iii",  "E -> iii"  },
        { 5,  "IV",   "F -> IV"   },
        { 7,  "V",    "G -> V"    },
        { 9,  "vi",   "A -> vi"   },
        { 11, "vii°", "B -> vii°" },
    };

    for (const auto& m : kMap) {
        bool allKeys = true;
        for (int key = 0; key < 12 && allKeys; ++key) {
            int  pos;
            Ring ring;
            if (! cellForMidiNote(kDefaultKeyMap, m.note, key, pos, ring)) {
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

    /*
     * What each black key does now.
     *
     * They used to be controls uniformly, and the property tested here was
     * that none of them played a chord. That is no longer the contract: D# and
     * F# play the two secondary dominants, which is the point of them. What
     * still holds is that a black key either plays a degree or changes a
     * setting - never both, and never a chord type.
     *
     *   C#  glide toggle
     *   D#  II   secondary dominant
     *   F#  III  secondary dominant
     *   G#  silent, rebindable
     *   A#  silent, rebindable
     */
    std::printf("\n=== black keys ===\n");
    {
        struct Expect { int pc; KeyAction action; int value; const char* what; };
        static const Expect kBlack[5] = {
            { 1,  kKeyGlideToggle, 0,              "glide toggle"   },
            { 3,  kKeyDegree,      kDegreeSecII,   "II  (secondary)"},
            { 6,  kKeyDegree,      kDegreeSecIII,  "III (secondary)"},
            { 8,  kKeyNone,        0,              "silent"         },
            { 10, kKeyNone,        0,              "silent"         },
        };

        for (int i = 0; i < 5; ++i) {
            const Expect&      x = kBlack[i];
            const KeyMapEntry& e = kDefaultKeyMap[x.pc];

            int  pos;
            Ring ring;
            const bool playsChord =
                cellForMidiNote(kDefaultKeyMap, x.pc, 0, pos, ring);

            const bool okAction = (e.action == x.action) &&
                                  (x.action != kKeyDegree || e.value == x.value);
            /* Plays a chord exactly when it is bound to a degree - no key may
             * both sound something and change a setting. */
            const bool okSound  = (playsChord == (x.action == kKeyDegree));

            if (okAction && okSound) {
                std::printf("  ok    %-6s %s\n", kPitch[x.pc], x.what);
            } else {
                std::printf("  FAIL  %-6s expected %s\n", kPitch[x.pc], x.what);
                ++failures;
            }
        }
    }

    /* And the white keys must all still play, or the keyboard is broken. */
    std::printf("\n=== white keys all play a degree ===\n");
    {
        static const int kWhite[7] = { 0, 2, 4, 5, 7, 9, 11 };
        bool allPlay = true;
        for (int i = 0; i < 7; ++i) {
            int  pos;
            Ring ring;
            if (! cellForMidiNote(kDefaultKeyMap, kWhite[i], 0, pos, ring))
                allPlay = false;
        }
        if (allPlay) {
            std::printf("  ok    %-30s all 7 play\n", "C D E F G A B");
        } else {
            std::printf("  FAIL  %-30s one is unbound\n", "C D E F G A B");
            ++failures;
        }
    }

    /*
     * The four chord-type keys must select DIFFERENT extensions. Binding two
     * of them to the same thing would waste a key and look like a bug under
     * the fingers.
     */
    /*
     * No black key selects a chord type any more.
     *
     * They used to, and the binding set the extension on every ring at once -
     * the same per-ring state the wheel's cells drive. A key press silently
     * rewrote what the circle was showing, and the circle never redrew to say
     * so. Chord type comes from the cell now, so the keyboard cannot
     * contradict what is on screen.
     *
     * D# and F# carry the two secondary dominants instead; G# and A# are
     * silent and rebindable.
     */
    std::printf("\n=== no key selects a chord type ===\n");
    {
        int extKeys = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (kDefaultKeyMap[pc].action == kKeyRetiredExt)
                ++extKeys;

        if (extKeys == 0) {
            std::printf("  ok    %-30s chord type comes from the cell\n",
                        "no chord-type bindings");
        } else {
            std::printf("  FAIL  %-30s %d keys still bound to it\n",
                        "no chord-type bindings", extKeys);
            ++failures;
        }

        const bool secs = kDefaultKeyMap[3].action  == kKeyDegree &&
                          kDefaultKeyMap[3].value   == kDegreeSecII &&
                          kDefaultKeyMap[6].action  == kKeyDegree &&
                          kDefaultKeyMap[6].value   == kDegreeSecIII;
        if (secs) {
            std::printf("  ok    %-30s D#=II  F#=III\n", "secondary dominants");
        } else {
            std::printf("  FAIL  %-30s not on D# and F#\n", "secondary dominants");
            ++failures;
        }

        const bool quiet = kDefaultKeyMap[8].action  == kKeyNone &&
                           kDefaultKeyMap[10].action == kKeyNone;
        if (quiet) {
            std::printf("  ok    %-30s G# and A# are silent\n", "freed keys");
        } else {
            std::printf("  FAIL  %-30s G#/A# are bound\n", "freed keys");
            ++failures;
        }
    }

    /*
     * Slide Mode's strips must resolve to the same cells the keyboard does -
     * they share kDegreeCell precisely so a strip and a key cannot disagree.
     * Checked through degreeInKey(), which is what the wheel highlights with,
     * so all three agree or the test fails.
     */
    std::printf("\n=== slides resolve to the right degrees, all keys ===\n");
    {
        struct { Scale scale; int count; const char* name; } kScales[] = {
            { kScaleDiatonic,  8, "diatonic"          },
            { kScaleMajorPent, 6, "major pentatonic"  },
            { kScaleMinorPent, 6, "minor pentatonic"  },
        };

        for (const auto& s : kScales) {
            bool ok = (slideCountForScale(s.scale) == s.count);

            const SlideDef* defs = slidesForScale(s.scale);
            for (int key = 0; key < 12 && ok; ++key) {
                for (int i = 0; i < s.count && ok; ++i) {
                    int  pos;
                    Ring ring;
                    cellForDegree(defs[i].degree, key, pos, ring);

                    const char* got = degreeInKey(pos, ring, key);
                    const char* want = kDegreeCell[defs[i].degree].numeral;
                    ok = (got != nullptr && std::strcmp(got, want) == 0);
                }
            }

            if (ok) {
                std::printf("  ok    %-30s %d slides\n", s.name, s.count);
            } else {
                std::printf("  FAIL  %-30s degrees disagree\n", s.name);
                ++failures;
            }
        }
    }

    /* The last slide repeats the tonic an octave up - that is what makes the
     * strip span a full scale rather than stopping a step short. */
    std::printf("\n=== last slide is the octave ===\n");
    {
        const SlideDef* d = slidesForScale(kScaleDiatonic);
        const SlideDef& first = d[0];
        const SlideDef& last  = d[7];
        if (first.degree == last.degree && last.octaveShift == 1) {
            std::printf("  ok    %-30s I then I+8ve\n", "diatonic");
        } else {
            std::printf("  FAIL  %-30s not an octave repeat\n", "diatonic");
            ++failures;
        }
    }

    /*
     * Minor pentatonic must need no chords from outside the key: i bIII iv v
     * bVII are vi I ii iii V read from the relative minor, so every slide is
     * diatonic and the wheel's wedge still covers it.
     */
    std::printf("\n=== minor pentatonic stays inside the key ===\n");
    {
        const SlideDef* d = slidesForScale(kScaleMinorPent);
        bool allDiatonic = true;
        for (int i = 0; i < 6 && allDiatonic; ++i) {
            int  pos;
            Ring ring;
            cellForDegree(d[i].degree, 0, pos, ring);
            allDiatonic = (roleInKey(pos, ring, 0) == kCellDiatonic);
        }
        if (allDiatonic) {
            std::printf("  ok    %-30s no borrowed chords\n", "all 6 slides");
        } else {
            std::printf("  FAIL  %-30s leaves the key\n", "all 6 slides");
            ++failures;
        }
    }

    /* The controller's octave is absolute - middle C (60) is octave 4. */
    std::printf("\n=== octave follows the controller ===\n");
    checkInt("note 60 -> octave 4", octaveForMidiNote(60), 4);
    checkInt("note 48 -> octave 3", octaveForMidiNote(48), 3);
    checkInt("note 72 -> octave 5", octaveForMidiNote(72), 5);

    /*
     * Voice leading. The complaint it answers: built naively, a I-vi-IV-V in C
     * leaps around because every chord stacks upward from its own root, so Am
     * lands nearly an octave above C and the notes they share are sounded in
     * different places. Checked by the two properties that matter - the chord
     * must keep its pitch classes, and it must stop leaping.
     */
    std::printf("\n=== voice leading keeps the harmony, kills the leap ===\n");
    {
        /* I - vi - IV - V in C, the progression that exposed this. */
        const int kRoots[4]  = { 0, 9, 5, 7 };
        const ChordType kTys[4] = {
            kChordMajor, kChordMinor, kChordMajor, kChordMajor
        };

        uint8_t prev[8];
        int     prevCount = 0;
        int     worstLead = 0;
        int     worstRaw  = 0;
        bool    pcOk      = true;

        uint8_t rawPrev[8];
        int     rawPrevCount = 0;

        for (int c = 0; c < 4; ++c) {
            uint8_t raw[8];
            const int rawN = buildChord(kRoots[c], kTys[c], 4 * 12, raw,
                                        kMaxChordTones);

            uint8_t led[8];
            std::memcpy(led, raw, sizeof(uint8_t) * rawN);
            applyVoiceLeading(led, rawN, prev, prevCount, 60);

            /* Property 1: same pitch classes - only octaves may change. */
            for (int i = 0; i < rawN; ++i) {
                bool found = false;
                for (int j = 0; j < rawN && ! found; ++j)
                    found = (led[j] % 12 == raw[i] % 12);
                if (! found) pcOk = false;
            }

            /* Property 2: how far the lowest voice jumps between chords. */
            if (prevCount > 0) {
                const int d = (led[0] > prev[0]) ? led[0] - prev[0]
                                                 : prev[0] - led[0];
                if (d > worstLead) worstLead = d;
            }
            if (rawPrevCount > 0) {
                const int d = (raw[0] > rawPrev[0]) ? raw[0] - rawPrev[0]
                                                    : rawPrev[0] - raw[0];
                if (d > worstRaw) worstRaw = d;
            }

            std::memcpy(prev, led, sizeof(uint8_t) * rawN);
            prevCount = rawN;
            std::memcpy(rawPrev, raw, sizeof(uint8_t) * rawN);
            rawPrevCount = rawN;
        }

        if (pcOk) {
            std::printf("  ok    %-30s only octaves changed\n",
                        "harmony is preserved");
        } else {
            std::printf("  FAIL  %-30s pitch classes changed\n",
                        "harmony is preserved");
            ++failures;
        }

        if (worstLead < worstRaw) {
            std::printf("  ok    %-30s %d semitones, was %d\n",
                        "bass movement is reduced", worstLead, worstRaw);
        } else {
            std::printf("  FAIL  %-30s %d semitones, was %d\n",
                        "bass movement is reduced", worstLead, worstRaw);
            ++failures;
        }

        if (worstLead <= 6) {
            std::printf("  ok    %-30s max %d semitones\n",
                        "no voice leaps a tritone", worstLead);
        } else {
            std::printf("  FAIL  %-30s max %d semitones\n",
                        "no voice leaps a tritone", worstLead);
            ++failures;
        }
    }

    /* Common tones must actually be held, not merely be present somewhere. */
    std::printf("\n=== C -> Am holds the shared notes ===\n");
    {
        uint8_t c[8];
        const int cn = buildChord(0, kChordMajor, 4 * 12, c, kMaxChordTones);

        uint8_t a[8];
        const int an = buildChord(9, kChordMinor, 4 * 12, a, kMaxChordTones);
        applyVoiceLeading(a, an, c, cn, 60);

        /* C major is C E G; A minor is A C E. C and E are shared and should
         * come back at exactly the pitches C major sounded them at. */
        int held = 0;
        for (int i = 0; i < an; ++i)
            for (int j = 0; j < cn; ++j)
                if (a[i] == c[j]) ++held;

        if (held >= 2) {
            std::printf("  ok    %-30s %d common tones held\n",
                        "C and E stay put", held);
        } else {
            std::printf("  FAIL  %-30s only %d held\n",
                        "C and E stay put", held);
            ++failures;
        }
    }

    /*
     * Inversion, through the voicing axis - the only route now that the
     * separate bass-note control is gone. Two properties matter: the requested
     * tone really is lowest, and the chord is still ascending afterwards. The
     * second caught a real bug - the inversion cases shifted octaves without
     * re-sorting, so "1st inversion" on A-C-E gave A3 C3 E3: neither ascending
     * nor inverted, with whatever happened to be lowest in the bass rather
     * than the named note.
     */
    std::printf("\n=== inversion puts the right tone lowest ===\n");
    {
        struct { int root; ChordType type; int nth; int wantPc;
                 const char* what; } kBass[] = {
            { 0, kChordMajor,  0, 0, "C major, root -> C" },
            { 0, kChordMajor,  1, 4, "C major, 1st  -> E" },
            { 0, kChordMajor,  2, 7, "C major, 2nd  -> G" },
            { 9, kChordMinor,  0, 9, "A minor, root -> A" },
            { 9, kChordMinor,  1, 0, "A minor, 1st  -> C" },
            { 9, kChordMinor,  2, 4, "A minor, 2nd  -> E" },
            { 0, kChordMajor7, 2, 7, "Cmaj7,   2nd  -> G" },
        };

        for (const auto& b : kBass) {
            uint8_t n[8];
            const int c = buildChord(b.root, b.type, 4 * 12, n, kMaxChordTones);
            invertChord(n, c, b.nth);

            bool ascending = true;
            for (int i = 1; i < c; ++i)
                if (n[i] <= n[i - 1]) ascending = false;

            const bool right = (n[0] % 12 == b.wantPc);

            if (right && ascending) {
                std::printf("  ok    %-30s %s\n", b.what, kPitch[n[0] % 12]);
            } else {
                std::printf("  FAIL  %-30s bass %s%s\n", b.what,
                            kPitch[n[0] % 12],
                            ascending ? "" : ", not ascending");
                ++failures;
            }
        }
    }

    /* Every voicing must leave the chord ascending, or notes[0] is not the
     * bass and anything reading it - the glide's voice pairing included - is
     * looking at the wrong note. */
    std::printf("\n=== every voicing leaves the chord ascending ===\n");
    {
        bool allAscending = true;
        for (int v = 0; v < kVoicingCount; ++v) {
            uint8_t n[8];
            int c = buildChord(9, kChordMinor, 3 * 12, n, kMaxChordTones);
            c = applyVoicing(n, c, static_cast<Voicing>(v), 8);
            for (int i = 1; i < c; ++i)
                if (n[i] < n[i - 1]) allAscending = false;
        }
        if (allAscending) {
            std::printf("  ok    %-30s all %d voicings\n",
                        "sorted after octave shifts",
                        static_cast<int>(kVoicingCount));
        } else {
            std::printf("  FAIL  %-30s a voicing is out of order\n",
                        "sorted after octave shifts");
            ++failures;
        }
    }

    /*
     * A repeated degree must not light two columns.
     *
     * The diatonic bank ends on the octave-up I and the minor pentatonic on
     * the octave-up i, and cellForDegree ignores the octave - so both columns
     * resolve to the SAME ring cell. Slide Mode lights a strip when its cell
     * is sounding, which meant pressing the first I lit the whole of the
     * eighth column too. The UI now lights only the first slide carrying a
     * degree; this records which banks repeat one, so a scale added later
     * cannot reintroduce the problem unnoticed.
     */
    std::printf("\n=== repeated degrees in a slide bank ===\n");
    {
        for (int sc = 0; sc < kScaleCount; ++sc) {
            const Scale s = static_cast<Scale>(sc);
            const int n = slideCountForScale(s);
            const SlideDef* defs = slidesForScale(s);

            /* Every degree must resolve to a first slide, and every later
             * appearance of it must report that same first slide - which is
             * what stops a second column lighting. */
            bool consistent = true;
            int  repeats = 0;

            for (int i = 0; i < n; ++i) {
                int first = -1;
                for (int j = 0; j < n && first < 0; ++j)
                    if (defs[j].degree == defs[i].degree)
                        first = j;

                if (first < 0) { consistent = false; break; }
                if (first != i) ++repeats;
            }

            char d[96];
            std::snprintf(d, sizeof d, "%d slides, %d repeated", n, repeats);
            if (consistent) {
                std::printf("  ok    %-30s %s\n", kScaleName[sc], d);
            } else {
                std::printf("  FAIL  %-30s %s\n", kScaleName[sc], d);
                ++failures;
            }
        }
    }

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "PASS" : "FAIL",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
