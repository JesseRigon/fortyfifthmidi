/*
 * FortyFifthMidi - MIDI-generating Circle of Fifths plugin.
 *
 * ARCHITECTURAL RULE (spec section 5), which most of this file exists to honour:
 * the musical-decision controls - chord type, velocity, note length, glide on/off,
 * glide time - are NOT automatable plugin parameters. They are live state, read at
 * the instant of a gesture and baked into concrete MIDI events. Consequently,
 * changing a control later can never retroactively alter already-generated MIDI.
 *
 * In DPF terms that means they go through initState()/setState() (opaque host-saved
 * state, not automation lanes), and initParameter() stays empty. Add a Parameter here
 * only for a genuine instrument-level value - e.g. a future preview synth's cutoff.
 */

#include "DistrhoPlugin.hpp"
#include "CircleTheory.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

using namespace fortyfifth;

class FortyFifthPlugin : public Plugin
{
public:
    FortyFifthPlugin()
        : Plugin(0 /* parameters: see the rule above - intentionally none */,
                 0 /* programs */,
                 kStateCount)
    {
        std::memset(fVoice, 0, sizeof(fVoice));
    }

protected:
    /* ---- identity ------------------------------------------------------- */

    const char* getLabel()       const override { return "FortyFifthMidi"; }
    const char* getDescription() const override
    {
        return "Circle of Fifths MIDI generator. Emits note data only - no audio.";
    }
    const char* getMaker()   const override { return "Jesse Rigon"; }
    const char* getHomePage() const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t    getVersion() const override { return d_version(0, 1, 0); }
    int64_t     getUniqueId() const override { return d_cconst('4', '5', 'M', 'd'); }

    /* ---- parameters ------------------------------------------------------ */

    /* Intentionally empty. See the architectural rule at the top of this file. */
    void initParameter(uint32_t, Parameter&) override {}
    float getParameterValue(uint32_t) const override { return 0.0f; }
    void  setParameterValue(uint32_t, float) override {}

    /* ---- state (non-automatable settings) -------------------------------- */

    enum StateIndex {
        kStateChordType = 0,
        kStateOctave,
        kStateVelocity,
        kStateVelocityRandom,
        kStateNoteLengthMs,
        kStateHoldToSustain,
        kStateGlideEnabled,
        kStateGlideTimeMs,
        kStateBendRange,
        kStateCount
    };

    void initState(uint32_t index, State& state) override
    {
        switch (index) {
            case kStateChordType:
                state.key = "chordType";
                state.label = "Chord Type";
                state.defaultValue = "1"; /* major */
                break;
            case kStateOctave:
                state.key = "octave";
                state.label = "Octave Offset";
                state.defaultValue = "4";
                break;
            case kStateVelocity:
                state.key = "velocity";
                state.label = "Velocity";
                state.defaultValue = "100";
                break;
            case kStateVelocityRandom:
                state.key = "velocityRandom";
                state.label = "Velocity Randomise +/-";
                state.defaultValue = "0";
                break;
            case kStateNoteLengthMs:
                state.key = "noteLengthMs";
                state.label = "Note Length (ms)";
                state.defaultValue = "500";
                break;
            case kStateHoldToSustain:
                state.key = "holdToSustain";
                state.label = "Hold To Sustain";
                state.defaultValue = "1";
                break;
            case kStateGlideEnabled:
                state.key = "glideEnabled";
                state.label = "Glide";
                state.defaultValue = "1";
                break;
            case kStateGlideTimeMs:
                state.key = "glideTimeMs";
                state.label = "Glide Time (ms)";
                state.defaultValue = "120";
                break;
            case kStateBendRange:
                state.key = "bendRange";
                state.label = "Pitch Bend Range (semitones)";
                state.defaultValue = "12";
                break;
        }
    }

    void setState(const char* key, const char* value) override
    {
        const int v = std::atoi(value);

        if (std::strcmp(key, "chordType") == 0)
            fChordType = static_cast<ChordType>(v % kChordTypeCount);
        else if (std::strcmp(key, "octave") == 0)
            fOctave = v;
        else if (std::strcmp(key, "velocity") == 0)
            fVelocity = static_cast<uint8_t>(v < 1 ? 1 : (v > 127 ? 127 : v));
        else if (std::strcmp(key, "velocityRandom") == 0)
            fVelocityRandom = v;
        else if (std::strcmp(key, "noteLengthMs") == 0)
            fNoteLengthMs = v;
        else if (std::strcmp(key, "holdToSustain") == 0)
            fHoldToSustain = (v != 0);
        else if (std::strcmp(key, "glideEnabled") == 0)
            fGlideEnabled = (v != 0);
        else if (std::strcmp(key, "glideTimeMs") == 0)
            fGlideTimeMs = v;
        else if (std::strcmp(key, "bendRange") == 0)
            fBendRange = v;

        /* A gesture in progress keeps the settings it started with, so that a
         * control change mid-drag cannot rewrite notes already emitted. */
    }

    String getState(const char* key) const override
    {
        char buf[16];
        int  v = 0;

        if (std::strcmp(key, "chordType") == 0)            v = static_cast<int>(fChordType);
        else if (std::strcmp(key, "octave") == 0)          v = fOctave;
        else if (std::strcmp(key, "velocity") == 0)        v = fVelocity;
        else if (std::strcmp(key, "velocityRandom") == 0)  v = fVelocityRandom;
        else if (std::strcmp(key, "noteLengthMs") == 0)    v = fNoteLengthMs;
        else if (std::strcmp(key, "holdToSustain") == 0)   v = fHoldToSustain ? 1 : 0;
        else if (std::strcmp(key, "glideEnabled") == 0)    v = fGlideEnabled ? 1 : 0;
        else if (std::strcmp(key, "glideTimeMs") == 0)     v = fGlideTimeMs;
        else if (std::strcmp(key, "bendRange") == 0)       v = fBendRange;

        std::snprintf(buf, sizeof(buf), "%d", v);
        return String(buf);
    }

    /* ---- audio/MIDI thread ------------------------------------------------ */

    void activate() override
    {
        fSampleRate  = getSampleRate();
        fNeedsRpn    = true;
        fGlideActive = false;
        fVoiceCount  = 0;
    }

    /*
     * No audio is produced. The output buffers are empty (NUM_OUTPUTS is 0); this
     * callback exists purely to emit MIDI at sample-accurate offsets.
     */
    void run(const float**, float**, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        /* Pass through anything an upstream controller sent us, so the plugin can
         * sit in a chain without swallowing notes. */
        for (uint32_t i = 0; i < midiEventCount; ++i)
            writeMidiEvent(midiEvents[i]);

        /* Announce the bend range once, before any glide can need it (spec 6.2
         * step 2). Doing it here rather than in activate() guarantees the host has
         * a real event buffer to receive it. */
        if (fNeedsRpn) {
            sendBendRangeRpn(0);
            fNeedsRpn = false;
        }

        if (fGlideActive)
            advanceGlide(frames);

        if (! fHoldToSustain && fVoiceCount > 0 && fNoteOffCountdown > 0) {
            fNoteOffCountdown -= static_cast<int32_t>(frames);
            if (fNoteOffCountdown <= 0)
                releaseAllVoices(0);
        }
    }

private:
    static constexpr int kMaxVoices = 5;

    /* ---- MIDI emission helpers ------------------------------------------- */

    void sendRaw(uint32_t frame, uint8_t a, uint8_t b, uint8_t c, uint8_t size = 3)
    {
        MidiEvent ev;
        ev.frame   = frame;
        ev.size    = size;
        ev.data[0] = a;
        ev.data[1] = b;
        ev.data[2] = c;
        ev.data[3] = 0;
        ev.dataExt = nullptr;
        writeMidiEvent(ev);
    }

    /* RPN 0,0 - pitch bend sensitivity, in semitones (spec section 7). */
    void sendBendRangeRpn(uint32_t frame)
    {
        const uint8_t cc = 0xB0 | fChannel;
        sendRaw(frame, cc, 101, 0);                                  /* RPN MSB */
        sendRaw(frame, cc, 100, 0);                                  /* RPN LSB */
        sendRaw(frame, cc, 6, static_cast<uint8_t>(fBendRange));     /* data MSB */
        sendRaw(frame, cc, 38, 0);                                   /* data LSB */
        sendRaw(frame, cc, 101, 127);                                /* RPN null */
        sendRaw(frame, cc, 100, 127);
    }

    void sendPitchBend(uint32_t frame, float semitones)
    {
        const float norm = semitones / static_cast<float>(fBendRange);
        int value = 8192 + static_cast<int>(norm * 8191.0f);
        if (value < 0)     value = 0;
        if (value > 16383) value = 16383;

        sendRaw(frame, 0xE0 | fChannel,
                static_cast<uint8_t>(value & 0x7F),
                static_cast<uint8_t>((value >> 7) & 0x7F));
    }

    uint8_t pickVelocity() const
    {
        if (fVelocityRandom <= 0)
            return fVelocity;

        const int spread = fVelocityRandom * 2 + 1;
        int v = fVelocity - fVelocityRandom + (std::rand() % spread);
        if (v < 1)   v = 1;
        if (v > 127) v = 127;
        return static_cast<uint8_t>(v);
    }

    void releaseAllVoices(uint32_t frame)
    {
        for (int i = 0; i < fVoiceCount; ++i)
            sendRaw(frame, 0x80 | fChannel, fVoice[i], 0);
        fVoiceCount       = 0;
        fNoteOffCountdown = 0;
    }

    /* ---- glide ------------------------------------------------------------ */

    /* Ramp the bend toward the target, then resolve with snap-and-reset (spec 6.2
     * step 4a): re-trigger at true pitches and zero the bend, so the recorded clip
     * holds real, editable note numbers rather than permanently bent ones. */
    void advanceGlide(uint32_t frames)
    {
        fGlideElapsed += frames;

        const float progress = fGlideDuration > 0
            ? static_cast<float>(fGlideElapsed) / static_cast<float>(fGlideDuration)
            : 1.0f;

        if (progress >= 1.0f) {
            sendPitchBend(0, static_cast<float>(fGlideTargetSemis));
            releaseAllVoices(0);
            triggerChordInternal(0, fGlideTargetRoot, fGlideTargetType, fGlideVelocity);
            sendPitchBend(0, 0.0f);

            fGlideActive     = false;
            fCurrentRoot     = fGlideTargetRoot;
            fCurrentType     = fGlideTargetType;
            fGlideTargetSemis = 0;
        } else {
            sendPitchBend(0, progress * static_cast<float>(fGlideTargetSemis));
        }
    }

    void triggerChordInternal(uint32_t frame, int rootPitchClass,
                              ChordType type, uint8_t velocity)
    {
        const int base = fOctave * 12;
        fVoiceCount = buildChord(rootPitchClass, type, base, fVoice, kMaxVoices);

        for (int i = 0; i < fVoiceCount; ++i)
            sendRaw(frame, 0x90 | fChannel, fVoice[i], velocity);
    }

    /* ---- settings: live state, never automation (spec section 5) ---------- */
    ChordType fChordType      = kChordMajor;
    int       fOctave         = 4;
    uint8_t   fVelocity       = 100;
    int       fVelocityRandom = 0;
    int       fNoteLengthMs   = 500;
    bool      fHoldToSustain  = true;
    bool      fGlideEnabled   = true;
    int       fGlideTimeMs    = 120;
    int       fBendRange      = 12;

    /* ---- runtime voicing state -------------------------------------------- */
    uint8_t  fVoice[kMaxVoices];
    int      fVoiceCount  = 0;
    uint8_t  fChannel     = 0;
    int      fCurrentRoot = 0;
    ChordType fCurrentType = kChordMajor;
    double   fSampleRate  = 48000.0;
    bool     fNeedsRpn    = true;
    int32_t  fNoteOffCountdown = 0;

    bool      fGlideActive      = false;
    uint32_t  fGlideElapsed     = 0;
    uint32_t  fGlideDuration    = 0;
    int       fGlideTargetSemis = 0;
    int       fGlideTargetRoot  = 0;
    ChordType fGlideTargetType  = kChordMajor;
    uint8_t   fGlideVelocity    = 100;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FortyFifthPlugin)
};

Plugin* createPlugin()
{
    return new FortyFifthPlugin();
}

END_NAMESPACE_DISTRHO
