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

/* Which concentric ring a hit landed on. */
enum Ring {
    kRingMajor = 0,
    kRingMinor = 1,
    kRingDim   = 2,
    kRingCount = 3
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
    kChordSus2,
    kChordSus4,
    kChordDim,
    kChordAug,
    kChordTypeCount
};

struct ChordShape {
    const char* name;
    int         count;
    int         interval[5];
};

static constexpr ChordShape kChordShape[kChordTypeCount] = {
    { "Single",  1, { 0, 0, 0, 0, 0 } },
    { "Major",   3, { 0, 4, 7, 0, 0 } },
    { "Minor",   3, { 0, 3, 7, 0, 0 } },
    { "Maj7",    4, { 0, 4, 7, 11, 0 } },
    { "Min7",    4, { 0, 3, 7, 10, 0 } },
    { "Dom7",    4, { 0, 4, 7, 10, 0 } },
    { "Sus2",    3, { 0, 2, 7, 0, 0 } },
    { "Sus4",    3, { 0, 5, 7, 0, 0 } },
    { "Dim",     3, { 0, 3, 6, 0, 0 } },
    { "Aug",     3, { 0, 4, 8, 0, 0 } },
};

/* Root pitch class for a wheel position on a given ring. */
inline int rootForPosition(int position, Ring ring)
{
    const int major = kCircleRoot[position % 12];
    switch (ring) {
        case kRingMinor: return (major + 9) % 12;  /* relative minor: down a minor 3rd */
        case kRingDim:   return (major + 11) % 12; /* leading-tone diminished */
        default:         return major;
    }
}

/* The chord type a ring implies when the user has not overridden it. */
inline ChordType defaultChordForRing(Ring ring)
{
    switch (ring) {
        case kRingMinor: return kChordMinor;
        case kRingDim:   return kChordDim;
        default:         return kChordMajor;
    }
}

inline const char* labelForPosition(int position, Ring ring)
{
    const int p = position % 12;
    switch (ring) {
        case kRingMinor: return kMinorLabel[p];
        case kRingDim:   return kDimLabel[p];
        default:         return kMajorLabel[p];
    }
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
