/*
 * FortyFifthMidi - circle-of-fifths geometry and chord spelling.
 *
 * Deliberately free of any DPF/plugin types so it can be unit-tested standalone
 * (see dev/test-theory.cpp).
 *
 * Layout note: the wheel is concentric, like a printed chord wheel - an outer ring
 * of majors and an inner ring of their relative minors, with room reserved for a
 * third (diminished) ring. Only the *theory* is shared with printed wheels; all
 * visual design here is original. See docs/spec.md.
 */

#ifndef FORTYFIFTH_CIRCLE_THEORY_HPP
#define FORTYFIFTH_CIRCLE_THEORY_HPP

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <atomic>

namespace fortyfifth {

/* Chromatic pitch class of each of the 12 circle-of-fifths positions, starting at
 * C and moving clockwise by ascending fifths: C G D A E B F# Db Ab Eb Bb F. */
static constexpr int kCircleRoot[12] = { 0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 };

static constexpr const char* kMajorLabel[12] = {
    "C", "G", "D", "A", "E", "B", "F♯", "D♭", "A♭", "E♭", "B♭", "F"
};

/* Relative minor of each position - always three semitones below the major. */
static constexpr const char* kMinorLabel[12] = {
    "Am", "Em", "Bm", "F♯m", "C♯m", "G♯m",
    "E♭m", "B♭m", "Fm", "Cm", "Gm", "Dm"
};

static constexpr const char* kDimLabel[12] = {
    "B°", "F♯°", "C♯°", "G♯°", "D♯°", "A♯°",
    "F°", "C°", "G°", "D°", "A°", "E°"
};

/*
 * Ring layout - a chord-progression wheel, not simply "majors and minors".
 *
 *   kRingKey (inner, 12)     the circle of fifths itself; picks the key.
 *                            C sits at twelve o'clock.
 *   kRingMinor (middle, 24)  the minor chords, at HALF width.
 *   kRingDim (outer, 12)     the vii diminished of each key.
 *
 * The point of the arrangement: every diatonic chord of a key occupies one
 * contiguous radial wedge. For the key of C, reading outward from the C cell:
 *
 *        outer     B°                      vii°
 *        middle    Dm   Em   Am             ii  iii  vi
 *        inner     F    C    G              IV  I    V
 *
 * The minor ring needs 24 cells because each key claims three minors while there
 * are only 12 key positions - so minors are half-width and neighbouring keys
 * share them. That sharing is what the "repeats" are: Am belongs to C, F and G
 * alike, and sits where all three wedges can reach it.
 *
 * This arrangement is music theory and is not protectable; only a given printed
 * product's artwork, palette and text are. All visual design here is original.
 */
enum Ring {
    kRingKey   = 0,   /* inner:  12 cells */
    kRingMinor = 1,   /* middle: 24 cells */
    kRingDim   = 2,   /* outer:  12 cells */
    kRingCount = 3
};

/* Cells per ring. The minor ring is double resolution. */
static constexpr int kRingSegments[kRingCount] = { 12, 24, 12 };

/* Relative band thickness. The key ring dominates; the diminished ring is
 * thinnest because it carries one chord per key and is used least. */
static constexpr float kRingWeight[kRingCount] = {
    1.00f,  /* key   */
    0.80f,  /* minor */
    0.50f   /* dim   */
};

/* Chord shapes as semitone offsets from the root. The *shape* is what the ring
 * and the chord-type selector choose; the root comes from the wheel position.
 * Keeping shape and root separate is what makes glide a single uniform pitch
 * bend (spec section 6.1): a glide changes only the root, so every voice moves
 * by the same interval. */
enum ChordType {
    kChordSingleNote = 0,
    kChordMajor,
    kChordMinor,
    kChordMajor7,
    kChordMinor7,
    kChordDominant7,
    kChordMinor7b5,
    kChordDim,
    kChordDim7,
    kChordAug,
    kChordAug7,
    kChordSus2,
    kChordSus4,
    kChord7sus4,
    kChordMajor6,
    kChordMinor6,
    kChordMajor9,
    kChordMinor9,
    kChordDominant9,
    kChordAdd9,
    kChordPower5,
    kChordTypeCount
};

static constexpr int kMaxChordTones = 5;

struct ChordShape {
    const char* name;      /* for the dropdown */
    const char* suffix;    /* appended to a root label, e.g. C + "m7" */
    int         count;
    int         interval[kMaxChordTones];
};

/* Semitone offsets from the root. Ninths are voiced as 14 rather than 2 so the
 * extension sits above the seventh instead of colliding with the root. */
static constexpr ChordShape kChordShape[kChordTypeCount] = {
    { "Single Note",     "",      1, { 0, 0, 0, 0, 0 } },
    { "Major",           "",      3, { 0, 4, 7, 0, 0 } },
    { "Minor",           "m",     3, { 0, 3, 7, 0, 0 } },
    { "Major 7th",       "maj7",  4, { 0, 4, 7, 11, 0 } },
    { "Minor 7th",       "m7",    4, { 0, 3, 7, 10, 0 } },
    { "Dominant 7th",    "7",     4, { 0, 4, 7, 10, 0 } },
    { "Minor 7 flat 5",  "m7♭5", 4, { 0, 3, 6, 10, 0 } },
    { "Diminished",      "dim",   3, { 0, 3, 6, 0, 0 } },
    { "Diminished 7th",  "dim7",  4, { 0, 3, 6, 9, 0 } },
    { "Augmented",       "aug",   3, { 0, 4, 8, 0, 0 } },
    { "Augmented 7th",   "aug7",  4, { 0, 4, 8, 10, 0 } },
    { "Suspended 2nd",   "sus2",  3, { 0, 2, 7, 0, 0 } },
    { "Suspended 4th",   "sus4",  3, { 0, 5, 7, 0, 0 } },
    { "7 Suspended 4th", "7sus4", 4, { 0, 5, 7, 10, 0 } },
    { "Major 6th",       "6",     4, { 0, 4, 7, 9, 0 } },
    { "Minor 6th",       "m6",    4, { 0, 3, 7, 9, 0 } },
    { "Major 9th",       "maj9",  5, { 0, 4, 7, 11, 14 } },
    { "Minor 9th",       "m9",    5, { 0, 3, 7, 10, 14 } },
    { "Dominant 9th",    "9",     5, { 0, 4, 7, 10, 14 } },
    { "Add 9",           "add9",  4, { 0, 4, 7, 14, 0 } },
    { "Power 5th",       "5",     2, { 0, 7, 0, 0, 0 } },
};

/*
 * Root pitch class for a cell.
 *
 * Key ring: the circle of fifths, C at index 0.
 *
 * Minor ring (24 cells): the minors also advance by fifths, but at half the
 * angular rate, so index m covers key m/2. The relative minor of a key is a
 * minor third below its tonic (+9 semitones), which makes cell m the relative
 * minor of key m/2 - and that same chord is the ii of the key a fifth up and the
 * iii of the key a fifth down. One cell, three owners.
 *
 * Dim ring: the leading-tone diminished, a semitone below the tonic.
 */
inline int rootForPosition(int position, Ring ring)
{
    switch (ring) {
        case kRingMinor: {
            /*
             * Two cells per key, holding that key's iii and vi:
             *
             *   cell 2k     = iii of key k   (tonic + 4 semitones)
             *   cell 2k + 1 = vi  of key k   (tonic + 9 semitones)
             *
             * That single rule generates the whole ring, including its apparent
             * repeats: Em is cell 0 (iii of C) and cell 3 (vi of G), because one
             * chord genuinely serves two adjacent keys in different roles. The
             * ii of a key is not stored separately - it is the vi cell of the
             * key one step anticlockwise, which sits immediately clockwise of
             * this key's cells.
             */
            const int cell = ((position % 24) + 24) % 24;
            const int key  = kCircleRoot[(cell / 2) % 12];
            return (cell % 2 == 0) ? (key + 4) % 12   /* iii */
                                   : (key + 9) % 12;  /* vi  */
        }
        case kRingDim:
            return (kCircleRoot[position % 12] + 11) % 12;
        default:
            return kCircleRoot[position % 12];
    }
}

/* Roman-numeral degree a cell plays within a given key index, or nullptr when
 * the cell is not diatonic to that key. Used for the wedge highlight. */
inline const char* degreeInKey(int position, Ring ring, int keyIndex)
{
    const int rel = ((position - keyIndex) % 12 + 12) % 12;

    switch (ring) {
        case kRingKey:
            if (rel == 11) return "IV";
            if (rel == 0)  return "I";
            if (rel == 1)  return "V";
            /* Secondary dominants: two and four steps clockwise are the major II
             * and III - V-of-V and V-of-vi. They are not diatonic to the key, but
             * they are the chords most often borrowed into it, so the wheel marks
             * them. */
            if (rel == 2)  return "II";
            if (rel == 4)  return "III";
            return nullptr;
        case kRingDim:
            return (rel == 0) ? "vii°" : nullptr;
        case kRingMinor: {
            /* This key owns cells 2k (iii) and 2k+1 (vi). Its ii is the vi cell
             * of the key one step anticlockwise, i.e. cell 2k-1. */
            const int relm = ((position - keyIndex * 2) % 24 + 24) % 24;
            if (relm == 23) return "ii";
            if (relm == 0)  return "iii";
            if (relm == 1)  return "vi";
            return nullptr;
        }
        default:
            return nullptr;
    }
}

/* The chord type a ring implies when the user has not overridden it. */
inline ChordType defaultChordForRing(Ring ring)
{
    switch (ring) {
        case kRingMinor: return kChordMinor;
        case kRingDim:   return kChordDim;
        default:         return kChordMajor;  /* key ring holds majors */
    }
}

/*
 * Extension the user asked for, independent of the underlying triad. The chord
 * selector EXTENDS a cell rather than overriding it: picking "7" on the Em cell
 * gives Em7, not E7, so the wheel's harmony is respected and the result stays
 * diatonic to the key.
 */
enum Extension {
    kExtNone = 0,
    kExt6,
    kExt7,
    kExt9,
    kExtAdd9,
    kExtSus2,
    kExtSus4,
    kExtCount
};

static constexpr const char* kExtensionName[kExtCount] = {
    "None (triad)", "6th", "7th", "9th", "Add 9", "Sus 2", "Sus 4"
};

/*
 * Apply an extension to a triad.
 *
 * dominant says this chord functions as a V - the degree whose seventh is a
 * FLAT seventh rather than a major one. The triad quality alone cannot decide
 * this: in C major both C (I) and G (V) are major triads, but I takes a major
 * 7th (B) and V takes a minor 7th (F). Treating every major triad the same
 * produced Gmaj7 in the key of C, which sounds an F# the key does not contain.
 *
 * Everything else follows the triad: minor and diminished take a minor 7th (a
 * fully-diminished 7th would need its own selection).
 */
inline ChordType extendChord(ChordType base, Extension ext,
                             bool dominant = false,
                             int  scaleDegree = -1)
{
    if (ext == kExtNone)
        return base;

    const bool isMinor = (base == kChordMinor);
    const bool isDim   = (base == kChordDim);
    const bool isMajor = (base == kChordMajor);

    /*
     * Some extensions add an interval that is only sometimes in the key, and
     * the triad quality cannot tell which. scaleDegree is the degree's
     * position as a semitone offset from the tonic (0, 2, 4, 5, 7, 9, 11), or
     * -1 when the caller does not know - in which case the chord is built
     * without the key check, as it always was.
     *
     * The test to apply is simply: is tonic + degree + interval in the major
     * scale? Written out per case rather than as a loop, because each case
     * has a different fallback.
     */
    static const auto inKey = [](int degree, int interval) -> bool {
        if (degree < 0)
            return true;              /* caller does not know; allow it */

        static const int kScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const int rel = ((degree + interval) % 12 + 12) % 12;

        for (int i = 0; i < 7; ++i)
            if (kScale[i] == rel)
                return true;
        return false;
    };

    switch (ext) {
        case kExt6:
            /*
             * A "minor 6th" chord has a MAJOR sixth interval (+9). That is
             * diatonic on ii but sharp on iii and vi.
             */
            if (isMinor) return inKey(scaleDegree, 9) ? kChordMinor6
                                                      : kChordTypeCount;
            if (isMajor) return inKey(scaleDegree, 9) ? kChordMajor6
                                                      : kChordTypeCount;
            return base;                       /* no standard dim 6 */
        case kExt7:
            if (isMinor) return kChordMinor7;
            if (isDim)   return kChordMinor7b5; /* half-diminished */
            if (isMajor) return dominant ? kChordDominant7 : kChordMajor7;
            return base;
        case kExt9:
            /* The ninth is a major second above the root (+14); sharp on
             * iii. */
            if (isMinor) return inKey(scaleDegree, 14) ? kChordMinor9
                                                       : kChordTypeCount;
            if (isMajor) return inKey(scaleDegree, 14)
                       ? (dominant ? kChordDominant9 : kChordMajor9)
                       : kChordTypeCount;
            return base;
        case kExtAdd9:
            if (isMajor) return inKey(scaleDegree, 14) ? kChordAdd9
                                                       : kChordTypeCount;
            return base;                       /* add9 on minor not in the table */
        case kExtSus2:
            /* Sus chords replace the third, so what matters is whether the
             * replacement is in the key: +2 for sus2, +5 for sus4. On iii the
             * second is sharp; on IV the fourth is the tritone. */
            if (isDim) return base;
            return inKey(scaleDegree, 2) ? kChordSus2 : kChordTypeCount;
        case kExtSus4:
            if (isDim) return base;
            return inKey(scaleDegree, 5) ? kChordSus4 : kChordTypeCount;
        default:
            return base;
    }
}

/*
 * kChordTypeCount as a return from extendChord() means "this extension does
 * not exist on this degree in this key".
 *
 * Reported rather than substituted, because a substitute is the same lie in a
 * different place: asking for a 9th on iii and being handed a 7th looks like
 * the plugin obeyed. The UI leaves such a cell blank, omits the option, or
 * falls back to the plain triad where a grid position must be filled - and
 * each of those tells the truth about what is available.
 */
inline bool chordExists(ChordType t) { return t != kChordTypeCount; }

/* The chord for a cell, or its plain triad when the chosen extension is not
 * in the key. For places that must show SOMETHING in a fixed position - the
 * variation rows of Slide Mode, where each row is a different extension and a
 * row cannot simply vanish. */
inline ChordType extendChordOrTriad(ChordType base, Extension ext,
                                    bool dominant, int scaleDegree)
{
    const ChordType t = extendChord(base, ext, dominant, scaleDegree);
    return chordExists(t) ? t : base;
}

/* Semitones above the tonic for each scale degree, for the key check in
 * extendChord(). kDegreeCount entries, in Degree order. */
static constexpr int kDegreeSemitone[7] = { 0, 2, 4, 5, 7, 9, 11 };

/*
 * Voicing modifiers - how the chord tones are arranged once the notes are
 * chosen. These rearrange octaves only; they never change which pitch classes
 * sound, so a chord stays the chord it is.
 *
 * Crucially for glide: a modifier is a per-ring setting applied uniformly, so
 * every cell in a ring produces the same interval pattern. That is what keeps
 * within-ring glide valid under spec 6.1.
 */
enum Voicing {
    kVoicingRegular = 0,  /* root position, ascending */
    kVoicingInv1,         /* third in the bass */
    kVoicingInv2,         /* fifth in the bass */
    kVoicingHighAsBass,   /* top voice dropped below the rest */
    kVoicingMidAsBass,    /* middle voice dropped below the rest */
    kVoicingReverse,      /* stacked downward from the root */
    kVoicingDrop2,        /* second voice from the top, down an octave */
    kVoicingDrop3,        /* third voice from the top, down an octave */
    kVoicingSpread,       /* alternate voices pushed an octave apart */
    kVoicingOctaveDouble, /* root doubled an octave up */
    kVoicingPowerRoot,    /* root doubled an octave DOWN, for weight */
    kVoicingCount
};

static constexpr const char* kVoicingName[kVoicingCount] = {
    "Regular",
    "1st inversion",
    "2nd inversion",
    "High as bass",
    "Mid as bass",
    "Reverse",
    "Drop 2",
    "Drop 3",
    "Spread",
    "Octave double",
    "Power root"
};

/*
 * Rearrange the octaves of an already-built chord. Operates in place on MIDI
 * note numbers; returns the new voice count (only the doubling modes change it).
 * Anything that would leave the MIDI range is left where it was.
 */
/* Small insertion sort - chords are at most six notes, so this is the right
 * shape and keeps the file free of <algorithm>. */
inline void sortAscending(uint8_t* notes, int count)
{
    for (int i = 1; i < count; ++i) {
        const uint8_t v = notes[i];
        int j = i - 1;
        while (j >= 0 && notes[j] > v) { notes[j + 1] = notes[j]; --j; }
        notes[j + 1] = v;
    }
}

inline int applyVoicing(uint8_t* notes, int count, Voicing voicing,
                        size_t capacity)
{
    if (count <= 0)
        return count;

    auto down = [](uint8_t n) -> uint8_t {
        return (n >= 12) ? static_cast<uint8_t>(n - 12) : n;
    };
    auto up = [](uint8_t n) -> uint8_t {
        return (n <= 115) ? static_cast<uint8_t>(n + 12) : n;
    };

    switch (voicing) {
        case kVoicingRegular:
            break;

        case kVoicingInv1:
            if (count >= 2) notes[0] = up(notes[0]);
            break;

        case kVoicingInv2:
            if (count >= 3) { notes[0] = up(notes[0]); notes[1] = up(notes[1]); }
            break;

        case kVoicingHighAsBass:
            notes[count - 1] = down(notes[count - 1]);
            break;

        case kVoicingMidAsBass:
            if (count >= 3) notes[count / 2] = down(notes[count / 2]);
            break;

        case kVoicingReverse:
            /* Stack downward from the root instead of upward, inverting the
             * chord's vertical direction while keeping the same pitch classes. */
            for (int i = 1; i < count; ++i)
                notes[i] = down(notes[i]);
            break;

        case kVoicingDrop2:
            if (count >= 2) notes[count - 2] = down(notes[count - 2]);
            break;

        case kVoicingDrop3:
            if (count >= 3) notes[count - 3] = down(notes[count - 3]);
            break;

        case kVoicingSpread:
            for (int i = 1; i < count; i += 2)
                notes[i] = up(notes[i]);
            break;

        case kVoicingOctaveDouble:
            if (static_cast<size_t>(count) < capacity) {
                notes[count] = up(notes[0]);
                return count + 1;
            }
            break;

        case kVoicingPowerRoot:
            if (static_cast<size_t>(count) < capacity) {
                notes[count] = down(notes[0]);
                sortAscending(notes, count + 1);
                return count + 1;
            }
            break;

        default:
            break;
    }

    /*
     * Sort, because every case above moves notes by an octave without
     * regard to where that leaves them in the list.
     *
     * Without this, "1st inversion" on A-C-E produced A3 C3 E3: the A was
     * raised but stayed at index 0, so the chord was neither ascending nor
     * actually inverted - the note in the bass was whatever happened to be
     * lowest, not the one the voicing named. Anything downstream that treats
     * notes[0] as the bass, including the glide's per-voice pairing, was
     * reading the wrong voice.
     */
    sortAscending(notes, count);
    return count;
}

/*
 * ---- voice leading ------------------------------------------------------
 *
 * Built naively, every chord stacks upward from its own root in a fixed
 * octave, so C is C3 E3 G3 but Am is A3 C4 E4 - nearly an octave higher. The
 * pitches are right and the progression is still diatonic, but it does not
 * SOUND diatonic: the harmony leaps around instead of moving, and the notes C
 * and E that both chords share are sounded at different octaves rather than
 * held in place.
 *
 * Voice leading fixes that by choosing the octave arrangement closest to the
 * previous chord. Common tones stay where they are and the remaining voices
 * move by the shortest distance, which is what a keyboard player does without
 * thinking and what makes a progression hang together.
 *
 * The notes are the same notes; only their octaves change. Harmony is
 * untouched.
 */

/*
 * Shift each voice to whichever octave sits nearest the previous chord.
 *
 * Works per voice against the previous chord's nearest pitch, rather than
 * transposing the chord as a block, so a chord can genuinely re-invert: given
 * C3 E3 G3 the chord A-C-E becomes A2 C3 E3, holding C3 and E3 exactly.
 *
 * refNotes/refCount describe the chord being moved away from. With no previous
 * chord (refCount 0) the notes are returned untouched.
 */
inline void applyVoiceLeading(uint8_t* notes, int count,
                              const uint8_t* refNotes, int refCount,
                              int centreMidi)
{
    if (count <= 0)
        return;

    /*
     * Choose the inversion as a WHOLE rather than moving each voice to its own
     * nearest neighbour. Voices considered separately do not cooperate - they
     * can all chase the same reference pitch and drift the chord upward, which
     * is precisely the leap this is meant to remove. Evaluating complete
     * candidates and scoring them is both more correct and easier to reason
     * about.
     *
     * Candidates are every rotation of the chord (each inversion) across a
     * range of octaves; the winner is the one whose voices are collectively
     * closest to the previous chord.
     */
    uint8_t best[16];
    int     bestScore = -1;

    const int base = (refCount > 0) ? refNotes[0] : centreMidi;

    for (int rot = 0; rot < count; ++rot) {
        for (int oct = -2; oct <= 2; ++oct) {
            uint8_t cand[16];

            /* Build this rotation, ascending from the rotated starting voice. */
            int prev = -1;
            bool ok = true;
            for (int i = 0; i < count; ++i) {
                const int pc = notes[(rot + i) % count] % 12;
                int p = ((base + oct * 12) / 12) * 12 + pc;

                /* Keep the chord ascending and compact: lift each voice to sit
                 * just above the one below it. */
                while (prev >= 0 && p <= prev)
                    p += 12;

                if (p < 0 || p > 127) { ok = false; break; }
                cand[i] = static_cast<uint8_t>(p);
                prev = p;
            }
            if (! ok)
                continue;

            /* Score: total distance from each voice to the nearest note of the
             * previous chord. Lower is smoother; a common tone scores zero,
             * which is what makes shared notes stay put. */
            int score = 0;
            if (refCount > 0) {
                for (int i = 0; i < count; ++i) {
                    int nearest = 127;
                    for (int r = 0; r < refCount; ++r) {
                        const int d = (refNotes[r] > cand[i])
                            ? refNotes[r] - cand[i] : cand[i] - refNotes[r];
                        if (d < nearest) nearest = d;
                    }
                    score += nearest;
                }
            } else {
                /* No previous chord: settle nearest the centre instead. */
                for (int i = 0; i < count; ++i) {
                    const int d = (cand[i] > centreMidi)
                        ? cand[i] - centreMidi : centreMidi - cand[i];
                    score += d;
                }
            }

            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                for (int i = 0; i < count; ++i)
                    best[i] = cand[i];
            }
        }
    }

    if (bestScore < 0)
        return;   /* nothing representable; leave the chord alone */

    for (int i = 0; i < count; ++i)
        notes[i] = best[i];
}

/*
 * ---- bass note ----------------------------------------------------------
 *
 * Which chord tone sits at the bottom. This is the classical inversion
 * choice, stated as "which note is the bass" rather than as an ordinal,
 * because that is what a player actually hears and chooses.
 *
 *   First   the root      C E G   - root position
 *   Second  the third     E G C   - first inversion
 *   Third   the fifth     G C E   - second inversion
 */
enum BassNote {
    kBassFirst = 0,
    kBassSecond,
    kBassThird,
    kBassNoteCount
};

static constexpr const char* kBassNoteName[kBassNoteCount] = {
    "ROOT: FIRST", "ROOT: SECOND", "ROOT: THIRD"
};

/*
 * Rotate a chord so the requested tone is lowest, keeping it ascending.
 *
 * Rotating rather than transposing is what makes this an inversion: the notes
 * below the new bass move UP an octave, so the chord keeps its pitch classes
 * and stays in roughly the same register instead of dropping.
 */
inline void applyBassNote(uint8_t* notes, int count, BassNote bass)
{
    const int want = static_cast<int>(bass);
    if (count <= 1 || want <= 0 || want >= count)
        return;

    /* Lift everything below the chosen tone by an octave; it then sits
     * lowest and the rest stack above it. */
    for (int i = 0; i < want; ++i) {
        if (notes[i] <= 115)
            notes[i] = static_cast<uint8_t>(notes[i] + 12);
    }

    sortAscending(notes, count);
}

/* Two chords can share a single pitch bend only if their interval patterns are
 * identical - that is what spec 6.1's simplification actually requires. Same
 * ring is not sufficient once extensions are in play. */
inline bool sameShape(ChordType a, ChordType b)
{
    if (a == b)
        return true;
    if (kChordShape[a].count != kChordShape[b].count)
        return false;
    for (int i = 0; i < kChordShape[a].count; ++i)
        if (kChordShape[a].interval[i] != kChordShape[b].interval[i])
            return false;
    return true;
}

/* Cells per ring, for callers that only have the enum. */
inline int cellsForRing(Ring ring) { return kRingSegments[ring]; }

/*
 * Does this cell function as a dominant in the selected key?
 *
 * Only the key ring can: V is one step clockwise of the key. The secondary
 * dominants the wheel also marks - II (V-of-V) and III (V-of-vi) - are
 * dominants by definition too; they are borrowed chords whose whole purpose is
 * to resolve like a V, and a major 7th on them would defeat that.
 *
 * I and IV are NOT dominant. In C that is C and F, which take major 7ths.
 */
inline bool cellIsDominant(int position, Ring ring, int keyIndex)
{
    if (ring != kRingKey)
        return false;

    const int rel = ((position - keyIndex) % 12 + 12) % 12;
    return rel == 1      /* V            */
        || rel == 2      /* II, V-of-V   */
        || rel == 4;     /* III, V-of-vi */
}


/*
 * ---- scale degrees -----------------------------------------------------
 *
 * The one definition of where a scale degree lives on the wheel. Three things
 * need this answer - the keyboard map, Slide Mode's strips, and the wheel's own
 * highlighting - and if they disagreed the same chord would be in different
 * places depending on how it was played.
 *
 * Offsets are in each ring's own cell units, measured from the key's cell,
 * exactly as degreeInKey() decodes them:
 *
 *   key ring (12 cells)    IV = -1, I = 0, V = +1
 *   minor ring (24 cells)  ii = -1, iii = 0, vi = +1   (key occupies cell 2k)
 *   dim ring (12 cells)    vii = 0
 */
enum Degree {
    kDegreeI = 0,
    kDegreeII,     /* ii  - minor */
    kDegreeIII,    /* iii - minor */
    kDegreeIV,
    kDegreeV,
    kDegreeVI,     /* vi  - minor */
    kDegreeVII,    /* vii - diminished */
    kDegreeCount
};

struct DegreeCell {
    Ring        ring;
    int         offset;     /* cells clockwise from the key's own cell */
    const char* numeral;    /* as degreeInKey() spells it */
};

static constexpr DegreeCell kDegreeCell[kDegreeCount] = {
    /* I   */ { kRingKey,    0, "I"    },
    /* ii  */ { kRingMinor, -1, "ii"   },
    /* iii */ { kRingMinor,  0, "iii"  },
    /* IV  */ { kRingKey,   -1, "IV"   },
    /* V   */ { kRingKey,    1, "V"    },
    /* vi  */ { kRingMinor,  1, "vi"   },
    /* vii */ { kRingDim,    0, "vii°" },
};

/* Whether a degree functions as a dominant, for the sequencer and the
 * keyboard, which hold a degree rather than a cell. Only V does: its seventh
 * is the flat seventh, which is what makes it resolve. See extendChord(). */
inline bool degreeIsDominant(Degree d) { return d == kDegreeV; }

/* A degree's distance above the tonic, for extendChord()'s key check. */
inline int semitoneForDegree(Degree d)
{
    return (d >= 0 && d < kDegreeCount) ? kDegreeSemitone[d] : -1;
}

/*
 * The same, for a cell the user clicked rather than a degree.
 *
 * Returns -1 when the cell is not diatonic to the key at all - the borrowed
 * II and III on the key ring, and every cell outside the key's own set. There
 * is no scale position to check against, so extendChord() builds the chord
 * without the key test, which is right: a borrowed chord is outside the key by
 * intent and should keep its full quality.
 */
inline int semitoneForCell(int position, Ring ring, int keyIndex)
{
    const int root  = rootForPosition(position, ring);
    const int tonic = rootForPosition(keyIndex, kRingKey);
    const int rel   = ((root - tonic) % 12 + 12) % 12;

    for (int i = 0; i < 7; ++i)
        if (kDegreeSemitone[i] == rel)
            return rel;
    return -1;
}

/* Resolve a degree to a cell in the given key. */
inline void cellForDegree(Degree degree, int keyIndex,
                          int& outPosition, Ring& outRing)
{
    const DegreeCell& d = kDegreeCell[degree];
    outRing = d.ring;

    /* The minor ring runs at double resolution, so the key's own cell is 2k
     * there and offsets are in half-width cells. */
    const int base = (d.ring == kRingMinor) ? keyIndex * 2 : keyIndex;
    const int n    = kRingSegments[d.ring];

    outPosition = ((base + d.offset) % n + n) % n;
}

/*
 * ---- scales ------------------------------------------------------------
 *
 * Which degrees a scale offers, and so how many slides Slide Mode shows. The
 * last entry of each list repeats the tonic an octave up, which is what makes
 * a strip span a full scale rather than stopping one short.
 */
enum Scale {
    kScaleDiatonic = 0,   /* 7 degrees + octave = 8 slides */
    kScaleMajorPent,      /* drops IV and vii   = 6 slides */
    kScaleMinorPent,      /* relative minor     = 6 slides */
    kScaleCount
};

static constexpr const char* kScaleName[kScaleCount] = {
    "Diatonic", "Major pentatonic", "Minor pentatonic"
};

/*
 * A slide: a degree, and how many octaves above the tonic it sits. The octave
 * field is what lets the last slide repeat the tonic up top.
 */
struct SlideDef {
    Degree degree;
    int    octaveShift;
};

static constexpr SlideDef kDiatonicSlides[8] = {
    { kDegreeI,   0 }, { kDegreeII,  0 }, { kDegreeIII, 0 }, { kDegreeIV, 0 },
    { kDegreeV,   0 }, { kDegreeVI,  0 }, { kDegreeVII, 0 }, { kDegreeI,  1 },
};

/* Major pentatonic drops IV and vii - the two degrees carrying the semitone
 * tension - leaving five consonant chords plus the octave. */
static constexpr SlideDef kMajorPentSlides[6] = {
    { kDegreeI,   0 }, { kDegreeII,  0 }, { kDegreeIII, 0 },
    { kDegreeV,   0 }, { kDegreeVI,  0 }, { kDegreeI,   1 },
};

/*
 * Minor pentatonic, read from the relative minor: i bIII iv v bVII.
 *
 * In degree terms against the parent major these are vi, I, ii, iii, V - the
 * same seven-chord vocabulary, started six degrees round. So it needs no
 * chords from outside the key, and the wheel's wedge still covers it.
 */
static constexpr SlideDef kMinorPentSlides[6] = {
    { kDegreeVI,  0 },   /* i    */
    { kDegreeI,   1 },   /* bIII */
    { kDegreeII,  1 },   /* iv   */
    { kDegreeIII, 1 },   /* v    */
    { kDegreeV,   1 },   /* bVII */
    { kDegreeVI,  1 },   /* i    */
};

inline int slideCountForScale(Scale scale)
{
    switch (scale) {
        case kScaleMajorPent: return 6;
        case kScaleMinorPent: return 6;
        default:              return 8;
    }
}

inline const SlideDef* slidesForScale(Scale scale)
{
    switch (scale) {
        case kScaleMajorPent: return kMajorPentSlides;
        case kScaleMinorPent: return kMinorPentSlides;
        default:              return kDiatonicSlides;
    }
}

/*
 * ---- keyboard mapping --------------------------------------------------
 *
 * An incoming MIDI note triggers a cell, so a controller keyboard can play the
 * wheel. The mapping is by DEGREE, not by absolute chord: the same physical key
 * plays I in whatever key is selected, so the keyboard transposes with the app
 * rather than fighting it.
 *
 * The white keys carry the scale in order:
 *
 *      C   D   E   F   G   A   B
 *      I   ii  iii IV  V   vi  vii°
 *
 * In the key of C those are the literal chords; in G, playing C sounds G major.
 * This is the order every chord chart uses, it matches Slide Mode's strips
 * exactly so the two input methods agree, and it leaves all five black keys
 * free for later assignment.
 */
/*
 * What a physical key does.
 *
 * The white keys play degrees; the black keys are free for real-time control,
 * so a player can change the chord quality or toggle glide mid-phrase without
 * reaching for the mouse. Every binding repeats in every octave, so the whole
 * scheme is playable with one hand wherever it happens to be.
 *
 * Nothing is passed through: an unbound key is SILENT rather than forwarded.
 * Forwarding it would sound the raw note under the chords, which is exactly
 * what the black keys were doing before they had jobs.
 */
enum KeyAction {
    kKeyNone = 0,     /* silent - bound to nothing */
    kKeyDegree,       /* play a scale degree */
    kKeyExtension,    /* select a chord extension for the NEXT chord */
    kKeyGlideToggle,  /* flip glide between off and its last on-state */
    kKeyLatchToggle,
    kKeySingleToggle, /* chords <-> single notes */
    kKeyPanic,
    kKeyActionCount
};

static constexpr const char* kKeyActionName[kKeyActionCount] = {
    "(silent)", "Degree", "Chord type", "Glide toggle",
    "Latch toggle", "Single notes", "Panic"
};

struct KeyMapEntry {
    KeyAction action;
    /* Which degree, or which extension, depending on the action. Unused by
     * the toggles, which need no argument. */
    int       value;
};

/*
 * The factory map. Every binding is editable from the Keyboard Setup tab, so
 * this is a starting point rather than a fixed scheme.
 *
 *   white   C D E F G A B  ->  I ii iii IV V vi vii
 *   black   C#             ->  glide toggle
 *           D# F# G# A#    ->  triad, 7th, 9th, sus4
 *
 * The four chord types are the ones a progression reaches for most; the other
 * three extensions remain available from the wheel, and any black key can be
 * rebound to them.
 */
static constexpr KeyMapEntry kDefaultKeyMap[12] = {
    /* C  */ { kKeyDegree,      kDegreeI   },
    /* C# */ { kKeyGlideToggle, 0          },
    /* D  */ { kKeyDegree,      kDegreeII  },
    /* D# */ { kKeyExtension,   kExtNone   },
    /* E  */ { kKeyDegree,      kDegreeIII },
    /* F  */ { kKeyDegree,      kDegreeIV  },
    /* F# */ { kKeyExtension,   kExt7      },
    /* G  */ { kKeyDegree,      kDegreeV   },
    /* G# */ { kKeyExtension,   kExt9      },
    /* A  */ { kKeyDegree,      kDegreeVI  },
    /* A# */ { kKeyExtension,   kExtSus4   },
    /* B  */ { kKeyDegree,      kDegreeVII },
};

/*
 * The key map's wire format: "action:value" per key, comma separated, twelve
 * entries. Plain text so a saved session stays readable and diffable.
 *
 * Encode and decode live together here for the same reason the progression's
 * do: the editor sends the map, the DSP receives it, and the DSP hands it back
 * when the host saves.
 */
static constexpr int kKeyMapStringMax = 128;

inline void encodeKeyMap(const KeyMapEntry* map, char* out, size_t cap)
{
    out[0] = '\0';
    int len = 0;

    for (int i = 0; i < 12; ++i)
        len += std::snprintf(out + len, cap - len, "%s%d:%d",
                             i ? "," : "",
                             static_cast<int>(map[i].action), map[i].value);
}

/*
 * Anything malformed leaves that key at its FACTORY binding rather than
 * silently unbinding it - a keyboard that stops responding is a worse failure
 * than one that ignores a bad setting.
 */
inline void decodeKeyMap(const char* value, KeyMapEntry* out)
{
    KeyMapEntry parsed[12];
    std::memcpy(parsed, kDefaultKeyMap, sizeof(parsed));

    const char* p = value;
    for (int i = 0; i < 12 && p != nullptr && *p != '\0'; ++i) {
        int a = 0, v = 0;
        if (std::sscanf(p, "%d:%d", &a, &v) == 2) {
            if (a >= 0 && a < kKeyActionCount) {
                parsed[i].action = static_cast<KeyAction>(a);
                parsed[i].value  = v;
            }
        }
        p = std::strchr(p, ',');
        if (p != nullptr) ++p;
    }

    std::memcpy(out, parsed, sizeof(parsed));
}

/* Pedal bindings, so a player with a sustain pedal can spend it on something
 * other than sustain - and free C# for another use. */
enum PedalAction {
    kPedalSustain = 0,   /* the default: defer releases while held */
    kPedalGlide,         /* glide on while held */
    kPedalLatch,
    kPedalSingle,
    kPedalPanic,
    kPedalNone,
    kPedalActionCount
};

static constexpr const char* kPedalActionName[kPedalActionCount] = {
    "Sustain", "Glide while held", "Latch toggle",
    "Single notes", "Panic", "(nothing)"
};

static constexpr const char* kPitchName[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* Black keys, for drawing and for deciding what a default rebind should be. */
inline bool isBlackKey(int pitchClass)
{
    switch (((pitchClass % 12) + 12) % 12) {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

/*
 * Resolve an incoming MIDI note to a wheel cell in the given key.
 *
 * Returns false for the unmapped pitch classes, which the caller should ignore
 * entirely. outPosition is a cell index in outRing's own numbering.
 */
inline bool cellForMidiNote(const KeyMapEntry* map, int midiNote, int keyIndex,
                            int& outPosition, Ring& outRing)
{
    const int pc = ((midiNote % 12) + 12) % 12;
    const KeyMapEntry& e = map[pc];

    if (e.action != kKeyDegree)
        return false;

    /* Shared with Slide Mode, so a key and a strip cannot disagree. */
    cellForDegree(static_cast<Degree>(e.value), keyIndex, outPosition, outRing);
    return true;
}

/*
 * Octave a played note asks for, as the chord's base octave.
 *
 * The controller's own octave is absolute: playing the mapped G in the second
 * octave sounds the chord there, so the keyboard behaves like an instrument
 * rather than like a switch. MIDI note 60 is C4 by the convention used here,
 * hence the -1.
 */
inline int octaveForMidiNote(int midiNote)
{
    return (midiNote / 12) - 1;
}

/* How a cell relates to the selected key, for colouring. */
enum CellRole {
    kCellOutside = 0,  /* not in the key */
    kCellDiatonic,     /* I IV V ii iii vi vii - the core wedge */
    kCellSecondary     /* II III - borrowed secondary dominants */
};

inline CellRole roleInKey(int position, Ring ring, int keyIndex)
{
    const char* deg = degreeInKey(position, ring, keyIndex);
    if (deg == nullptr)
        return kCellOutside;

    /* The two secondary dominants are the only all-caps degrees that are not
     * I, IV or V. */
    if (std::strcmp(deg, "II") == 0 || std::strcmp(deg, "III") == 0)
        return kCellSecondary;

    return kCellDiatonic;
}

/*
 * How a move between two selections is voiced.
 *
 *   kGlideOff   retrigger at the new chord immediately.
 *   kGlideMpe   one MIDI channel per voice, so voices bend independently and
 *               ANY chord can glide to any other. Requires an MPE-capable
 *               instrument downstream.
 *
 * THE MIDDLE MODE IS GONE. A single channel-wide pitch bend moves every voice
 * by the same interval, so it could only express a move between chords of
 * identical shape AND inversion - and it forced everything else to accommodate
 * that: extensions had to be uniform across a whole ring, and voice leading
 * had to be suspended whenever it was on, because a re-inversion cannot ride
 * one bend.
 *
 * Removing it lets chords be chosen per cell and lets voice leading always
 * apply. Overlapping chords are now the instrument's job: both are sent, and
 * two triggers falling within the merge window are treated as one so a change
 * does not clip. See kMergeWindowMs.
 */
enum GlideMode {
    kGlideOff = 0,
    kGlideMpe,
    kGlideModeCount
};

static constexpr const char* kGlideModeName[kGlideModeCount] = {
    "GLIDE: OFF", "GLIDE: MPE"
};

/*
 * How close two triggers must be to count as one gesture, in milliseconds.
 *
 * Without this, two chords struck at nearly the same moment - a drag crossing
 * a cell boundary, or two fingers on a touchscreen - produce a note-off and a
 * note-on a few milliseconds apart, which clips audibly. Inside the window the
 * second chord joins the first instead of replacing it.
 *
 * Adjustable because the right value depends on the player and the instrument:
 * a fast run wants a short window so genuinely separate chords stay separate.
 */
static constexpr int kMergeWindowMsDefault = 20;
static constexpr int kMergeWindowMsMax     = 200;

/*
 * Minor-ring labels, cell by cell. Spelled theoretically rather than simplified -
 * E#m and B#m are correct for their keys even though they sound as Fm and Cm,
 * and a chord wheel shows them that way so the key signatures stay consistent.
 */
static constexpr const char* kMinorRingLabel[24] = {
    "Em",  "Am",  "Bm",  "Em",  "F♯m", "Bm",
    "C♯m", "F♯m", "G♯m", "C♯m", "D♯m", "G♯m",
    "A♯m", "D♯m", "E♯m", "A♯m", "B♯m", "Fm",
    "Gm",  "Cm",  "Dm",  "Gm",  "Am",  "Dm"
};

inline const char* labelForPosition(int position, Ring ring)
{
    switch (ring) {
        case kRingMinor:
            return kMinorRingLabel[((position % 24) + 24) % 24];
        case kRingDim:
            return kDimLabel[((position % 12) + 12) % 12];
        default:
            return kMajorLabel[((position % 12) + 12) % 12];
    }
}

/* Cells in a ring - drawing and hit-testing both need this. */
inline int segmentsInRing(Ring ring)
{
    return kRingSegments[ring];
}

/* Signed semitone distance to move from one root to another, chosen as the
 * shortest path so a drag never bends further than 6 semitones per step. */
inline int shortestSemitoneDelta(int fromPitchClass, int toPitchClass)
{
    int delta = (toPitchClass - fromPitchClass) % 12;
    if (delta > 6)  delta -= 12;
    if (delta < -6) delta += 12;
    return delta;
}

/* Build the MIDI note numbers for a chord. Returns the voice count.
 * octave is a semitone offset already multiplied out by the caller. */
inline int buildChord(int rootPitchClass,
                      ChordType type,
                      int baseOctaveMidi,
                      uint8_t* outNotes,
                      size_t outCapacity)
{
    const ChordShape& shape = kChordShape[type];
    int written = 0;

    for (int i = 0; i < shape.count && static_cast<size_t>(written) < outCapacity; ++i) {
        const int note = baseOctaveMidi + rootPitchClass + shape.interval[i];
        if (note >= 0 && note <= 127) {
            outNotes[written++] = static_cast<uint8_t>(note);
        }
    }
    return written;
}

/*
 * Lock-free ring carrying emitted MIDI from the DSP to the UI's event log.
 *
 * DPF offers no ordinary route for this: the UI can setState() but has no
 * getState(), Plugin::updateStateValue() must not be called during run(), and
 * there is no plugin-side idle hook to call it from. What DPF does offer is
 * DISTRHO_PLUGIN_WANT_DIRECT_ACCESS, which hands the UI a pointer to the plugin
 * instance - legitimate here because DPF hosts the UI in the plugin's own
 * process for CLAP, VST3 and the standalone alike.
 *
 * The DSP writes from the audio thread and never blocks or allocates; the UI
 * drains from uiIdle(). Events are dropped when the ring is full, which is the
 * right trade for a monitor: it must never stall the audio thread.
 */
/*
 * A lock-free ring of TEXT lines, for the diagnostic log.
 *
 * The event monitor shows MIDI as it is emitted, which is not enough to
 * explain why a chord arrives in pieces: the interesting facts are the ones
 * that never became MIDI - a refused write, a voice dropped from a group, a
 * gesture that arrived while another was in flight.
 *
 * The audio thread must never touch a file, so it formats into this ring and
 * the UI writes it out from its idle callback. Lines are fixed-width and
 * pre-allocated: formatting on the audio thread is fine, allocating is not.
 *
 * Overflow drops the OLDEST line rather than the newest, because when
 * something goes wrong the lines immediately before and after it are the ones
 * worth keeping, and a reader who is behind has already lost the thread.
 */
struct LogRing {
    static constexpr uint32_t kCapacity = 512;
    static constexpr uint32_t kLineMax  = 96;

    char                  line[kCapacity][kLineMax] = {{0}};
    std::atomic<uint32_t> write { 0 };
    std::atomic<uint32_t> read  { 0 };

    /* Audio thread. Never blocks, never allocates. */
    void push(const char* text)
    {
        const uint32_t w = write.load(std::memory_order_relaxed);

        /* snprintf rather than strncpy: it always terminates, and it does not
         * warn about a source exactly as long as the destination. */
        std::snprintf(line[w % kCapacity], kLineMax, "%s", text);

        write.store(w + 1, std::memory_order_release);

        /* Full: advance the reader so the oldest line is overwritten rather
         * than the newest refused. */
        const uint32_t r = read.load(std::memory_order_acquire);
        if (w + 1 - r > kCapacity)
            read.store(w + 1 - kCapacity, std::memory_order_release);
    }

    /* UI thread. Returns false when drained. */
    bool pop(char* out, size_t cap)
    {
        const uint32_t r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire))
            return false;

        std::snprintf(out, cap, "%s", line[r % kCapacity]);

        read.store(r + 1, std::memory_order_release);
        return true;
    }
};

struct MonitorRing {
    static constexpr uint32_t kCapacity = 256;

    uint32_t              slot[kCapacity] = {0};
    std::atomic<uint32_t> write { 0 };
    std::atomic<uint32_t> read  { 0 };

    /* Audio thread. Never blocks, never allocates; drops events when full. */
    void push(uint32_t word)
    {
        const uint32_t w = write.load(std::memory_order_relaxed);
        if (w - read.load(std::memory_order_acquire) >= kCapacity)
            return;
        slot[w % kCapacity] = word;
        write.store(w + 1, std::memory_order_release);
    }

    /* UI thread. Returns false when drained. */
    bool pop(uint32_t& out)
    {
        const uint32_t r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire))
            return false;
        out = slot[r % kCapacity];
        read.store(r + 1, std::memory_order_release);
        return true;
    }
};

/*
 * What the DSP is currently sounding, for the UI to light up.
 *
 * The monitor ring cannot answer this. It carries raw MIDI, so the UI would
 * have to guess which cell produced a pitch - and several cells can produce
 * the same one. It is also only drained while the monitor panel is open,
 * whereas the highlight has to work whether or not the panel is expanded.
 *
 * So the DSP publishes the cells themselves. One bit per cell of each ring,
 * written by the audio thread and read by the UI: a plain atomic word, so
 * neither side blocks and a torn read is impossible.
 *
 * The 24-cell minor ring is why this is 32-bit per ring rather than 16.
 */
struct ActiveCells {
    std::atomic<uint32_t> ring[kRingCount];

    ActiveCells()
    {
        for (int i = 0; i < kRingCount; ++i)
            ring[i].store(0, std::memory_order_relaxed);
    }

    /* Audio thread. */
    void set(Ring r, int position, bool on)
    {
        const int n = kRingSegments[r];
        const uint32_t bit = 1u << (((position % n) + n) % n);

        uint32_t cur = ring[r].load(std::memory_order_relaxed);
        ring[r].store(on ? (cur | bit) : (cur & ~bit),
                      std::memory_order_release);
    }

    void clear()
    {
        for (int i = 0; i < kRingCount; ++i)
            ring[i].store(0, std::memory_order_release);
    }

    /* UI thread. */
    bool isOn(Ring r, int position) const
    {
        const int n = kRingSegments[r];
        const uint32_t bit = 1u << (((position % n) + n) % n);
        return (ring[r].load(std::memory_order_acquire) & bit) != 0;
    }

    bool any() const
    {
        for (int i = 0; i < kRingCount; ++i)
            if (ring[i].load(std::memory_order_acquire) != 0)
                return true;
        return false;
    }

    /*
     * Where the sequencer is, packed as (section << 8) | step, or -1 when it
     * is not running.
     *
     * Carried here rather than through a registry of its own because it is the
     * same kind of fact as the lit cells - one word, written by the audio
     * thread, read by the UI to draw what is sounding - and the UI already
     * holds a pointer to this.
     */
    std::atomic<int32_t> playhead { -1 };

    /*
     * Messages the host refused, for the editor's warning line.
     *
     * Carried here for the same reason as the playhead: one word, written by
     * the audio thread, read by the UI. A non-zero count is the single number
     * that explains a chord arriving in pieces while every display insists it
     * was correct.
     */
    std::atomic<uint32_t> dropped { 0 };

    void setPlayhead(int section, int step)
    {
        playhead.store((section << 8) | step, std::memory_order_release);
    }

    void clearPlayhead() { playhead.store(-1, std::memory_order_release); }

    /* UI thread. Returns false when the sequencer is stopped. */
    bool playheadAt(int& outSection, int& outStep) const
    {
        const int32_t v = playhead.load(std::memory_order_acquire);
        if (v < 0)
            return false;
        outSection = (v >> 8) & 0xFF;
        outStep    = v & 0xFF;
        return true;
    }
};

/*
 * Where saved progressions and preferences are kept.
 *
 * The user folder always works and needs no setup, so it is the default.
 * Portable puts the data beside the plugin, which suits an install the user
 * owns - a USB stick carried between studios, or a VST folder that is not
 * under Program Files. Custom is for anything else, such as a synced folder.
 *
 * Portable is offered rather than assumed because plugin directories are
 * frequently read-only, and a plugin that cannot write where it promised is
 * worse than one that asked.
 */
enum StorageMode {
    kStorageUser = 0,
    kStoragePortable,
    kStorageCustom,
    kStorageModeCount
};

static constexpr const char* kStorageModeName[kStorageModeCount] = {
    "User folder", "Beside the plugin", "Custom folder"
};

/*
 * ---- progressions ------------------------------------------------------
 *
 * A step sequencer for chords. One cell is one beat; one row is a section; the
 * sections play one after another and then round again.
 *
 * Cells store a DEGREE, not a chord. A progression written as I-vi-IV-V is
 * then the same progression in every key, and changing the key transposes it
 * without rewriting it - which is the entire point of a circle-of-fifths
 * instrument. The degree resolves to a ring and cell through cellForDegree(),
 * the same call the keyboard and Slide Mode make, so a progression sounds
 * identical to playing those cells by hand.
 *
 * Each cell also carries its own extension, because the user asked for chord
 * type per cell rather than per column: a ii-V-I wants the ii and V as
 * sevenths and the I plain, and that is a property of the step, not of the
 * beat it falls on.
 */

/*
 * An empty cell is a rest: the beat passes and nothing is triggered. Under
 * legato a rest does NOT cut the previous chord - the chord rings until the
 * next real trigger, which is what legato means here.
 *
 * The octave is per cell, and relative: 0 means the plugin's own octave
 * setting, -1 an octave below, +1 above. Relative rather than absolute so
 * that moving the whole instrument up or down carries the progression with
 * it, and a chord written an octave below its neighbours stays an octave
 * below them.
 */
struct ProgCell {
    bool      filled = false;
    Degree    degree = kDegreeI;
    Extension ext    = kExtNone;
    int8_t    octave = 0;       /* -2..+2, relative to the global octave */
};

static constexpr int kProgOctaveMin = -2;
static constexpr int kProgOctaveMax =  2;

/*
 * Beats in a section.
 *
 * 64 is the ceiling; a section is set to 16, 32 or 64 through
 * kProgLengthChoice. Sixteen was too short to hold a verse at one chord per
 * beat, which is what the longer grids are for.
 */
static constexpr int kMaxProgSteps    = 64;
static constexpr int kMaxProgSections = 8;    /* A..H */

/* The grid sizes offered. A section's length is free to be anything up to the
 * chosen size; this is the size of the editable grid, not the loop. */
static constexpr int kProgLengthChoice[] = { 16, 32, 64 };
static constexpr int kProgLengthChoiceCount =
    static_cast<int>(sizeof(kProgLengthChoice) / sizeof(kProgLengthChoice[0]));

struct ProgSection {
    ProgCell cell[kMaxProgSteps];
    int      length = 4;        /* beats actually played before moving on */
};

/* Sections are named by letter, as the user described them: A, B, C, D. */
inline char sectionLetter(int index)
{
    return static_cast<char>('A' + (index % 26));
}

struct Progression {
    ProgSection section[kMaxProgSections];
    int         count = 1;      /* sections in use */

    /* Total beats in one pass through every section. */
    int totalBeats() const
    {
        int n = 0;
        for (int i = 0; i < count; ++i)
            n += section[i].length;
        return n;
    }

    /*
     * Where a beat lands, counting from the start of the whole progression and
     * wrapping at the end.
     *
     * Sections CHAIN rather than layer: beat 0 is A's first step, and once A's
     * length is used up the count carries into B. Returns false only if there
     * is nothing to play at all, which keeps callers from dividing by zero
     * when every section has been emptied.
     */
    bool locate(long long beat, int& outSection, int& outStep) const
    {
        const int total = totalBeats();
        if (total <= 0)
            return false;

        /* True modulo: the host's beat count is free to be negative when a
         * transport is rolled back before the start. */
        long long b = beat % total;
        if (b < 0) b += total;

        for (int i = 0; i < count; ++i) {
            const int len = section[i].length;
            if (b < len) {
                outSection = i;
                outStep    = static_cast<int>(b);
                return true;
            }
            b -= len;
        }

        /* Unreachable while total is the sum of the lengths, but a sequencer
         * that silently played the wrong chord would be worse than one that
         * plays none. */
        return false;
    }

    /*
     * Insert a copy of a section, either straight after it or at the end.
     *
     * Returns the new section's index, or -1 when full. "As next" is for
     * building a variation you want heard immediately after the original; "as
     * last" is for reusing a section later in the arrangement.
     */
    int duplicate(int index, bool asNext)
    {
        if (count >= kMaxProgSections || index < 0 || index >= count)
            return -1;

        const int dest = asNext ? index + 1 : count;

        /* Open a gap, copying backwards so overlapping moves stay intact. */
        for (int i = count; i > dest; --i)
            section[i] = section[i - 1];

        section[dest] = section[index];
        ++count;
        return dest;
    }

    /* Remove a section. The last one is never removed: a progression with no
     * sections has nothing to show and no way back to a usable state. */
    bool remove(int index)
    {
        if (count <= 1 || index < 0 || index >= count)
            return false;

        for (int i = index; i < count - 1; ++i)
            section[i] = section[i + 1];

        --count;
        return true;
    }

    /* Append an empty section. Returns its index, or -1 when full. */
    int add()
    {
        if (count >= kMaxProgSections)
            return -1;

        section[count] = ProgSection();
        return count++;
    }

    /*
     * Move a chord from one cell to another, or copy it.
     *
     * A move leaves a rest behind; a copy does not. Dropping onto a cell
     * REPLACES it rather than pushing the rest along, because a progression
     * grid is positional - beat three is beat three, and shuffling everything
     * right would move chords the user never touched.
     *
     * Dropping past a section's end extends it, so a chord can be dragged out
     * to lengthen a phrase in one gesture.
     */
    bool moveCell(int fromSection, int fromStep,
                  int toSection, int toStep, bool copy)
    {
        if (fromSection < 0 || fromSection >= count ||
            toSection   < 0 || toSection   >= count)
            return false;
        if (fromStep < 0 || fromStep >= kMaxProgSteps ||
            toStep   < 0 || toStep   >= kMaxProgSteps)
            return false;
        if (fromSection == toSection && fromStep == toStep)
            return false;

        const ProgCell moved = section[fromSection].cell[fromStep];

        if (! copy)
            section[fromSection].cell[fromStep] = ProgCell();

        section[toSection].cell[toStep] = moved;

        if (toStep >= section[toSection].length)
            section[toSection].length = toStep + 1;

        return true;
    }
};

/*
 * Progressions offered in the left-hand menu.
 *
 * Written as degrees so each one works in any key. These are the progressions
 * worth having to hand rather than an exhaustive catalogue - the grid is
 * editable, so the menu only needs to save typing on the common ones.
 */
/* Presets are short phrases, so they are sized for a phrase rather than for
 * the whole 64-beat grid - which would make every entry mostly padding. */
static constexpr int kMaxPresetSteps = 16;

struct NamedProgression {
    const char* name;
    int         length;
    Degree      degree[kMaxPresetSteps];
};

static constexpr NamedProgression kPresetProgression[] = {
    { "I-V-vi-IV",  4, { kDegreeI,  kDegreeV,   kDegreeVI, kDegreeIV  } },
    { "I-vi-IV-V",  4, { kDegreeI,  kDegreeVI,  kDegreeIV, kDegreeV   } },
    { "ii-V-I",     3, { kDegreeII, kDegreeV,   kDegreeI              } },
    { "I-IV-V-I",   4, { kDegreeI,  kDegreeIV,  kDegreeV,  kDegreeI   } },
    { "vi-IV-I-V",  4, { kDegreeVI, kDegreeIV,  kDegreeI,  kDegreeV   } },
    { "I-IV-vi-V",  4, { kDegreeI,  kDegreeIV,  kDegreeVI, kDegreeV   } },
    { "12-bar",     8, { kDegreeI,  kDegreeI,   kDegreeIV, kDegreeIV,
                         kDegreeI,  kDegreeV,   kDegreeIV, kDegreeI   } },
};

static constexpr int kPresetProgressionCount =
    static_cast<int>(sizeof(kPresetProgression) / sizeof(kPresetProgression[0]));

/*
 * Write a preset into a section, starting at a given step.
 *
 * Starting at a step rather than always at the beginning is what makes the
 * menu useful for building rather than only for replacing: a preset can be
 * dropped in after what is already there, so an eight-bar section can be
 * assembled from two four-bar phrases.
 *
 * Cells before the start are left exactly as they were, and the section is
 * lengthened only if the preset runs past its current end - so loading into
 * step 0 of a longer section does not truncate the tail.
 */
inline void loadPresetAt(Progression& prog, int presetIndex,
                         int section, int startStep)
{
    if (presetIndex < 0 || presetIndex >= kPresetProgressionCount)
        return;
    if (section < 0 || section >= kMaxProgSections)
        return;
    if (startStep < 0 || startStep >= kMaxProgSteps)
        return;

    const NamedProgression& p = kPresetProgression[presetIndex];
    ProgSection&            s = prog.section[section];

    for (int i = 0; i < p.length; ++i) {
        const int at = startStep + i;
        if (at >= kMaxProgSteps)
            break;

        s.cell[at].filled = true;
        s.cell[at].degree = p.degree[i];
        s.cell[at].ext    = kExtNone;
        s.cell[at].octave = 0;
    }

    const int end = startStep + p.length;
    if (end > s.length)
        s.length = (end > kMaxProgSteps) ? kMaxProgSteps : end;

    if (section >= prog.count)
        prog.count = section + 1;
}

/*
 * ---- the progression wire format ---------------------------------------
 *
 * Sections separated by ';', cells by ',', a cell being
 * "degree.extension.octave" or "-" for a rest, with the section's length
 * ahead of a '|'. So "4|0.0.0,5.2.0,3.0.-1,4.0.0;2|0.0.0,4.0.0" is a
 * four-beat I-vi-IV-V with the IV an octave down, then a two-beat I-V.
 *
 * One string rather than a key per cell, because a reader must never see a
 * half-applied grid - a progression arriving cell by cell would be a chimera
 * of the old and new for however many beats the update straddled.
 *
 * Encode and decode live here, together, because three places need them: the
 * editor sends the grid, the DSP receives it, and the DSP sends it back when
 * the host saves. Two copies of this format drifting apart would corrupt
 * every saved session.
 *
 * Buffer size for the encoded form: every section full at the maximum length,
 * each cell "dd.e.oo," at up to ten bytes, plus the length prefix.
 */
static constexpr int kProgStringMax =
    kMaxProgSections * (kMaxProgSteps * 10 + 8) + 16;

inline void encodeProgression(const Progression& prog, char* out, size_t cap)
{
    out[0] = '\0';
    int len = 0;

    for (int s = 0; s < prog.count; ++s) {
        const ProgSection& sec = prog.section[s];

        len += std::snprintf(out + len, cap - len, "%s%d|",
                             s ? ";" : "", sec.length);

        for (int i = 0; i < sec.length && i < kMaxProgSteps; ++i) {
            const ProgCell& c = sec.cell[i];

            if (c.filled)
                len += std::snprintf(out + len, cap - len, "%s%d.%d.%d",
                                     i ? "," : "",
                                     static_cast<int>(c.degree),
                                     static_cast<int>(c.ext),
                                     static_cast<int>(c.octave));
            else
                len += std::snprintf(out + len, cap - len, "%s-",
                                     i ? "," : "");

            /* Stop rather than emit a truncated grid, which would decode as a
             * different progression than the one held. */
            if (len >= static_cast<int>(cap) - 16)
                return;
        }

        if (len >= static_cast<int>(cap) - 16)
            return;
    }
}

/*
 * Decode into out, returning false if nothing usable was found - in which case
 * out is untouched, so a malformed string leaves the caller's grid alone
 * rather than half-replacing it.
 */
inline bool decodeProgression(const char* value, Progression& out)
{
    if (value == nullptr)
        return false;

    Progression parsed;
    parsed.count = 0;

    const char* p = value;
    while (p != nullptr && *p != '\0' && parsed.count < kMaxProgSections) {
        ProgSection& sec = parsed.section[parsed.count];
        sec = ProgSection();

        int len = 0;
        if (std::sscanf(p, "%d|", &len) != 1)
            break;

        sec.length = (len < 0) ? 0
                   : (len > kMaxProgSteps) ? kMaxProgSteps : len;

        const char* c = std::strchr(p, '|');
        if (c == nullptr)
            break;
        ++c;

        for (int i = 0; i < sec.length; ++i) {
            if (*c == '-') {
                sec.cell[i].filled = false;
            } else {
                int d = 0, e = 0, o = 0;
                if (std::sscanf(c, "%d.%d.%d", &d, &e, &o) == 3 &&
                    d >= 0 && d < kDegreeCount &&
                    e >= 0 && e < kExtCount) {
                    sec.cell[i].filled = true;
                    sec.cell[i].degree = static_cast<Degree>(d);
                    sec.cell[i].ext    = static_cast<Extension>(e);
                    /* Clamped rather than rejected: an out-of-range octave
                     * should still play the right chord. */
                    sec.cell[i].octave = static_cast<int8_t>(
                        (o < kProgOctaveMin) ? kProgOctaveMin
                      : (o > kProgOctaveMax) ? kProgOctaveMax : o);
                }
            }

            const char* comma = std::strchr(c, ',');
            const char* semi  = std::strchr(c, ';');
            if (comma == nullptr || (semi != nullptr && semi < comma))
                break;
            c = comma + 1;
        }

        ++parsed.count;

        p = std::strchr(p, ';');
        if (p != nullptr) ++p;
    }

    if (parsed.count == 0)
        return false;

    out = parsed;
    return true;
}

/* Replace a section outright with a preset. Used for the opening grid, where
 * there is nothing to preserve. */
inline void loadPreset(Progression& prog, int presetIndex, int section)
{
    if (presetIndex < 0 || presetIndex >= kPresetProgressionCount)
        return;
    if (section < 0 || section >= kMaxProgSections)
        return;

    prog.section[section] = ProgSection();
    prog.section[section].length = 0;

    loadPresetAt(prog, presetIndex, section, 0);
}

/*
 * Bridge between the two translation units.
 *
 * getPluginInstancePointer() hands the UI a void* to the Plugin. Casting that to
 * the concrete plugin class from FortyFifthUI.cpp is not possible: each file
 * compiles its own DPF symbols, so the class definition is not shared. Instead
 * the plugin registers its ring here at construction and the UI looks it up.
 *
 * Defined in FortyFifthPlugin.cpp.
 */
MonitorRing* monitorRingFor(void* pluginInstance);
void registerMonitorRing(void* pluginInstance, MonitorRing* ring);
void unregisterMonitorRing(void* pluginInstance);

/* Same registry, so the UI finds its own instance's highlight state. */
ActiveCells* activeCellsFor(void* pluginInstance);
void registerActiveCells(void* pluginInstance, ActiveCells* cells);

/* And the diagnostic log, which the UI drains to a file. */
LogRing* logRingFor(void* pluginInstance);
void registerLogRing(void* pluginInstance, LogRing* ring);

} /* namespace fortyfifth */

#endif /* FORTYFIFTH_CIRCLE_THEORY_HPP */
