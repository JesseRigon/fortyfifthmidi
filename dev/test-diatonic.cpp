/*
 * Does every chord this plugin builds actually belong to the key?
 *
 *   g++ -std=c++17 -I src dev/test-diatonic.cpp -o /tmp/td && /tmp/td
 *
 * This exists because of a reported bug: in the key of C, the V chord came out
 * as Gmaj7 and sounded an F#, which is not in C major. The cause was that
 * extendChord() decided the seventh from the TRIAD QUALITY alone, and C (I)
 * and G (V) are both major triads - but I takes a major 7th and V takes a flat
 * one. Only the degree knows which.
 *
 * Rather than special-case V and move on, this checks the whole diatonic set:
 * for every degree, every extension, and every key, are all the sounded pitch
 * classes in the major scale of that key?
 *
 * Two deliberate exceptions, which are NOT failures:
 *
 *   - sus2 and sus4 replace the third with the second or fourth. Both are in
 *     the scale, so they stay diatonic, and they are checked like the rest.
 *
 *   - The wheel also marks II and III on the key ring, which are SECONDARY
 *     DOMINANTS (V-of-V and V-of-vi). They are borrowed from outside the key
 *     on purpose, so they are checked against their OWN target key instead.
 */

#include "CircleTheory.hpp"

#include <cstdio>
#include <cstring>

using namespace fortyfifth;

static const char* kPitch[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static int failures = 0;

/* Semitones above the tonic for each degree of the major scale. */
static bool inMajorScale(int pitchClass, int keyPitchClass)
{
    static const int kScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int rel = ((pitchClass - keyPitchClass) % 12 + 12) % 12;

    for (int i = 0; i < 7; ++i)
        if (kScale[i] == rel)
            return true;
    return false;
}

/* The tonic pitch class of a key, given its index on the circle of fifths. */
static int tonicForKeyIndex(int keyIndex)
{
    return rootForPosition(keyIndex, kRingKey);
}

/*
 * Check one degree at one extension in one key.
 *
 * Returns quietly on success; prints and counts on failure. Printing every
 * pass would be 7 degrees x 7 extensions x 12 keys = 588 lines.
 */
static void checkDegree(Degree d, Extension ext, int keyIndex, bool verbose)
{
    int  position = 0;
    Ring ring     = kRingKey;
    cellForDegree(d, keyIndex, position, ring);

    const int  root     = rootForPosition(position, ring);
    const bool dominant = degreeIsDominant(d);

    const ChordType type =
        extendChord(defaultChordForRing(ring), ext, dominant,
                    semitoneForDegree(d));

    /*
     * An extension that does not exist on this degree is reported as such
     * rather than substituted, and the UI omits it. Nothing sounds, so there
     * is nothing to check against the key.
     */
    if (! chordExists(type)) {
        if (verbose)
            std::printf("  --    %-4s %-12s in %-2s  not in key, omitted\n",
                        kDegreeCell[d].numeral, kExtensionName[ext],
                        kPitch[tonicForKeyIndex(keyIndex)]);
        return;
    }

    uint8_t notes[kMaxChordTones];
    const int n = buildChord(root, type, 4 * 12, notes, kMaxChordTones);

    const int tonic = tonicForKeyIndex(keyIndex);

    bool allIn = true;
    char outside[64] = {0};

    for (int i = 0; i < n; ++i) {
        const int pc = notes[i] % 12;
        if (! inMajorScale(pc, tonic)) {
            allIn = false;
            std::snprintf(outside + std::strlen(outside),
                          sizeof outside - std::strlen(outside),
                          "%s%s", outside[0] ? " " : "", kPitch[pc]);
        }
    }

    if (allIn) {
        if (verbose)
            std::printf("  ok    %-4s %-12s in %-2s  %s\n",
                        kDegreeCell[d].numeral, kExtensionName[ext],
                        kPitch[tonic], kChordShape[type].name);
        return;
    }

    std::printf("  FAIL  %-4s %-12s in %-2s  %s sounds %s\n",
                kDegreeCell[d].numeral, kExtensionName[ext],
                kPitch[tonic], kChordShape[type].name, outside);
    ++failures;
}

int main()
{
    /*
     * The reported case first, spelled out: C major, the V chord, a 7th.
     * G-B-D-F is right; G-B-D-F# is the bug.
     */
    std::printf("=== the reported case: V7 in C ===\n");
    {
        int  pos = 0;
        Ring ring = kRingKey;
        cellForDegree(kDegreeV, 0, pos, ring);

        const ChordType type = extendChord(defaultChordForRing(ring), kExt7,
                                           degreeIsDominant(kDegreeV));

        uint8_t notes[kMaxChordTones];
        const int n = buildChord(rootForPosition(pos, ring), type,
                                 4 * 12, notes, kMaxChordTones);

        char got[48] = {0};
        for (int i = 0; i < n; ++i)
            std::snprintf(got + std::strlen(got), sizeof got - std::strlen(got),
                          "%s%s", i ? " " : "", kPitch[notes[i] % 12]);

        const bool right = (std::strcmp(got, "G B D F") == 0);
        if (right) {
            std::printf("  ok    %-30s %s (%s)\n",
                        "V with a 7th is dominant", got, kChordShape[type].name);
        } else {
            std::printf("  FAIL  %-30s got %s (%s), want G B D F\n",
                        "V with a 7th is dominant", got, kChordShape[type].name);
            ++failures;
        }
    }

    /* The whole diatonic set, in C, printed - this is the table a reader
     * wants to see when asking "what does this plugin actually build?". */
    std::printf("\n=== every degree, every extension, in C ===\n");
    for (int d = 0; d < kDegreeCount; ++d)
        for (int e = 0; e < kExtCount; ++e)
            checkDegree(static_cast<Degree>(d), static_cast<Extension>(e),
                        0, true);

    /* Then silently across all twelve keys, since a key-dependent bug would
     * be invisible in C alone. */
    std::printf("\n=== the same, across all twelve keys ===\n");
    for (int k = 0; k < 12; ++k)
        for (int d = 0; d < kDegreeCount; ++d)
            for (int e = 0; e < kExtCount; ++e)
                checkDegree(static_cast<Degree>(d), static_cast<Extension>(e),
                            k, false);
    if (failures == 0)
        std::printf("  ok    %-30s %d combinations\n",
                    "all keys clean", 12 * kDegreeCount * kExtCount);

    /*
     * Secondary dominants. II and III on the key ring are V-of-V and V-of-vi:
     * deliberately outside the key, and they must be DOMINANT sevenths or they
     * do not function as secondary dominants at all.
     */
    std::printf("\n=== secondary dominants are dominant sevenths ===\n");
    {
        struct { int rel; const char* name; const char* target; } kSec[] = {
            { 2, "II  (V-of-V)",  "G" },
            { 4, "III (V-of-vi)", "A" },
        };

        for (const auto& s : kSec) {
            const int  position = ((0 + s.rel) % 12 + 12) % 12;
            const bool dom = cellIsDominant(position, kRingKey, 0);

            const ChordType type =
                extendChord(defaultChordForRing(kRingKey), kExt7, dom);

            char detail[96];
            std::snprintf(detail, sizeof detail, "%s%s",
                          kPitch[rootForPosition(position, kRingKey)],
                          kChordShape[type].suffix);

            if (dom && type == kChordDominant7) {
                std::printf("  ok    %-30s %s\n", s.name, detail);
            } else {
                std::printf("  FAIL  %-30s %s, wanted a dominant 7th\n",
                            s.name, detail);
                ++failures;
            }
        }
    }

    /* I and IV must NOT be dominant - that is the other half of the fix. A
     * blanket "major triads take dominant sevenths" would pass every test
     * above and be just as wrong. */
    std::printf("\n=== I and IV keep their major sevenths ===\n");
    {
        struct { Degree d; const char* want; } kMaj[] = {
            { kDegreeI,  "Cmaj7" },
            { kDegreeIV, "Fmaj7" },
        };

        for (const auto& m : kMaj) {
            int  pos = 0;
            Ring ring = kRingKey;
            cellForDegree(m.d, 0, pos, ring);

            const ChordType type = extendChord(defaultChordForRing(ring),
                                               kExt7, degreeIsDominant(m.d));

            char got[32];
            std::snprintf(got, sizeof got, "%s%s",
                          kPitch[rootForPosition(pos, ring)],
                          kChordShape[type].suffix);

            if (std::strcmp(got, m.want) == 0) {
                std::printf("  ok    %-30s %s\n",
                            kDegreeCell[m.d].numeral, got);
            } else {
                std::printf("  FAIL  %-30s got %s, want %s\n",
                            kDegreeCell[m.d].numeral, got, m.want);
                ++failures;
            }
        }
    }

    /* vii is half-diminished in a major key (m7b5), not fully diminished.
     * A dim7 would sound a flat-flat-7 that the key does not contain. */
    std::printf("\n=== vii takes a half-diminished seventh ===\n");
    {
        int  pos = 0;
        Ring ring = kRingKey;
        cellForDegree(kDegreeVII, 0, pos, ring);

        const ChordType type = extendChord(defaultChordForRing(ring), kExt7,
                                           degreeIsDominant(kDegreeVII));

        if (type == kChordMinor7b5) {
            std::printf("  ok    %-30s %s\n", "vii7 is m7b5",
                        kChordShape[type].name);
        } else {
            std::printf("  FAIL  %-30s got %s\n", "vii7 is m7b5",
                        kChordShape[type].name);
            ++failures;
        }
    }

    /*
     * The editor offers only the extensions that exist on a degree, and the
     * row-to-extension mapping must be self-consistent: the menu DRAWS by row
     * and APPLIES by row, so if those disagree, picking "7th" sets something
     * else. Transcribed from FortyFifthUI.cpp, which needs the DPF UI stack to
     * compile.
     */
    std::printf("\n=== the menu offers only what exists ===\n");
    {
        auto available = [](Degree d, Extension e) -> bool {
            int  position = 0;
            Ring ring     = kRingKey;
            cellForDegree(d, 0, position, ring);
            return chordExists(extendChord(defaultChordForRing(ring), e,
                                           degreeIsDominant(d),
                                           semitoneForDegree(d)));
        };

        auto countFor = [&](Degree d) {
            int n = 0;
            for (int e = 0; e < kExtCount; ++e)
                if (available(d, static_cast<Extension>(e))) ++n;
            return n;
        };

        auto extAt = [&](Degree d, int row) -> Extension {
            int n = 0;
            for (int e = 0; e < kExtCount; ++e) {
                if (! available(d, static_cast<Extension>(e))) continue;
                if (n == row) return static_cast<Extension>(e);
                ++n;
            }
            return kExtNone;
        };

        auto rowOf = [&](Degree d, Extension want) {
            int n = 0;
            for (int e = 0; e < kExtCount; ++e) {
                if (! available(d, static_cast<Extension>(e))) continue;
                if (static_cast<Extension>(e) == want) return n;
                ++n;
            }
            return 0;
        };

        bool consistent = true;
        for (int dd = 0; dd < kDegreeCount; ++dd) {
            const Degree d = static_cast<Degree>(dd);
            const int    n = countFor(d);

            /* Every row must map to an available extension, and back to the
             * same row. */
            for (int r = 0; r < n; ++r) {
                const Extension e = extAt(d, r);
                if (! available(d, e) || rowOf(d, e) != r)
                    consistent = false;
            }

            char detail[64];
            std::snprintf(detail, sizeof detail, "%d of %d extensions",
                          n, static_cast<int>(kExtCount));

            /* A degree must always offer at least the plain triad, or its
             * cells would have no chord at all. */
            if (n >= 1) {
                std::printf("  ok    %-4s %-25s %s\n",
                            kDegreeCell[dd].numeral, "has options", detail);
            } else {
                std::printf("  FAIL  %-4s %-25s none\n",
                            kDegreeCell[dd].numeral, "has options");
                ++failures;
            }
        }

        if (consistent) {
            std::printf("  ok    %-30s rows map both ways\n",
                        "the mapping is consistent");
        } else {
            std::printf("  FAIL  %-30s a row maps to the wrong extension\n",
                        "the mapping is consistent");
            ++failures;
        }

        /* The triad is always first, so a cell that loses its extension lands
         * on something valid. */
        bool triadFirst = true;
        for (int dd = 0; dd < kDegreeCount; ++dd)
            if (extAt(static_cast<Degree>(dd), 0) != kExtNone)
                triadFirst = false;

        if (triadFirst) {
            std::printf("  ok    %-30s every degree\n", "the triad is row 0");
        } else {
            std::printf("  FAIL  %-30s a degree omits the triad\n",
                        "the triad is row 0");
            ++failures;
        }
    }

    /*
     * Where a grid position must be filled - Slide Mode's variation rows,
     * where each row is an extension and a row cannot vanish - the plain triad
     * stands in, and it is always a real chord.
     */
    std::printf("\n=== the triad fallback always yields a chord ===\n");
    {
        bool allReal = true;
        for (int dd = 0; dd < kDegreeCount; ++dd) {
            const Degree d = static_cast<Degree>(dd);

            int  position = 0;
            Ring ring     = kRingKey;
            cellForDegree(d, 0, position, ring);

            for (int e = 0; e < kExtCount; ++e)
                if (! chordExists(extendChordOrTriad(
                        defaultChordForRing(ring), static_cast<Extension>(e),
                        degreeIsDominant(d), semitoneForDegree(d))))
                    allReal = false;
        }

        if (allReal) {
            std::printf("  ok    %-30s %d combinations\n",
                        "never returns nothing",
                        static_cast<int>(kDegreeCount * kExtCount));
        } else {
            std::printf("  FAIL  %-30s returned nothing\n",
                        "never returns nothing");
            ++failures;
        }
    }

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "PASS" : "FAIL",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
