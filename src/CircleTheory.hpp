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

#if FORTYFIFTH_MIDI_MONITOR
# include <atomic>
#endif

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

/* Cells per ring, for callers that only have the enum. */
inline int cellsForRing(Ring ring) { return kRingSegments[ring]; }

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

#if FORTYFIFTH_MIDI_MONITOR
/*
 * Test-rig plumbing, standalone build only.
 *
 * DPF offers no way to get this data across: the UI can setState() but has no
 * getState(), Plugin::updateStateValue() must not be called during run(), and
 * there is no plugin-side idle hook to call it from. In the standalone build,
 * however, the DSP and UI live in one process, so they can share a ring buffer
 * directly. The DSP writes from the audio thread, the UI drains it from uiIdle().
 *
 * This is emphatically NOT how a shipping plugin should communicate - in a real
 * host the UI may be in a different process entirely. It is compiled only into
 * the standalone target and never into the CLAP or VST3.
 */
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

/* Defined in FortyFifthPlugin.cpp. */
MonitorRing& monitorRing();
#endif /* FORTYFIFTH_MIDI_MONITOR */

} /* namespace fortyfifth */

#endif /* FORTYFIFTH_CIRCLE_THEORY_HPP */
