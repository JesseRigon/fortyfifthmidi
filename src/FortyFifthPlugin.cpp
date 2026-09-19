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

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

namespace fortyfifth {

/*
 * Instance -> ring registry, so the UI can find its own plugin's monitor.
 *
 * A host can load many instances, and each UI must read the ring belonging to
 * its own plugin. Registration happens on construction and removal on
 * destruction, both on the main thread; the mutex guards against a host doing
 * either concurrently. The audio thread never touches this - it holds a direct
 * reference to its own ring.
 */
namespace {
    std::mutex gRegistryMutex;
    std::map<void*, MonitorRing*> gRegistry;
}

void registerMonitorRing(void* instance, MonitorRing* ring)
{
    const std::lock_guard<std::mutex> lock(gRegistryMutex);
    gRegistry[instance] = ring;
}

void unregisterMonitorRing(void* instance)
{
    const std::lock_guard<std::mutex> lock(gRegistryMutex);
    gRegistry.erase(instance);
}

MonitorRing* monitorRingFor(void* instance)
{
    const std::lock_guard<std::mutex> lock(gRegistryMutex);
    const std::map<void*, MonitorRing*>::const_iterator it = gRegistry.find(instance);
    return (it != gRegistry.end()) ? it->second : nullptr;
}

} /* namespace fortyfifth */

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
        std::memset(fHeld, 0, sizeof(fHeld));
        registerMonitorRing(this, &fMonitor);
    }

    ~FortyFifthPlugin() override
    {
        unregisterMonitorRing(this);
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
        /* Per-ring: "ext0".."ext2" and "voice0".."voice2". */
        kStateExt0 = 0,
        kStateExt1,
        kStateExt2,
        kStateVoice0,
        kStateVoice1,
        kStateVoice2,
        kStateOctave,
        kStateVelocity,
        kStateVelocityRandom,
        kStateNoteLengthMs,
        kStateHoldToSustain,
        kStateLatch,
        kStateGlideMode,
        kStateGlideTimeMs,
        kStateBendRange,
        kStateGesture,
        kStatePanic,
        kStateCount
    };

    void initState(uint32_t index, State& state) override
    {
        switch (index) {
            case kStateExt0:
            case kStateExt1:
            case kStateExt2: {
                static const char* const kKeys[3]   = { "ext0", "ext1", "ext2" };
                static const char* const kLabels[3] = {
                    "Key Ring Extension", "Minor Ring Extension",
                    "Dim Ring Extension"
                };
                const int r = index - kStateExt0;
                state.key = kKeys[r];
                state.label = kLabels[r];
                state.defaultValue = "0";
                break;
            }
            case kStateVoice0:
            case kStateVoice1:
            case kStateVoice2: {
                static const char* const kKeys[3]   = { "voice0", "voice1", "voice2" };
                static const char* const kLabels[3] = {
                    "Key Ring Voicing", "Minor Ring Voicing", "Dim Ring Voicing"
                };
                const int r = index - kStateVoice0;
                state.key = kKeys[r];
                state.label = kLabels[r];
                state.defaultValue = "0";
                break;
            }
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
            case kStateLatch:
                state.key = "latch";
                state.label = "Latch";
                state.defaultValue = "0";
                break;
            case kStateGlideMode:
                state.key = "glideMode";
                state.label = "Glide Mode";   /* 0 off, 1 on, 2 MPE */
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
            case kStateGesture:
                /* Transient UI -> DSP channel, not a persisted setting. */
                state.key = "gesture";
                state.label = "Gesture";
                state.defaultValue = "";
                break;
            case kStatePanic:
                /* All notes off, on demand. */
                state.key = "panic";
                state.label = "Panic";
                state.defaultValue = "";
                break;
        }
    }

    void setState(const char* key, const char* value) override
    {
        const int v = std::atoi(value);

        /* Per-ring keys: extN and voiceN, where N is the ring index. */
        if (std::strncmp(key, "ext", 3) == 0 && key[3] >= '0' && key[3] <= '2') {
            fRingExtension[key[3] - '0'] =
                static_cast<Extension>(((v % kExtCount) + kExtCount) % kExtCount);
        }
        else if (std::strncmp(key, "voice", 5) == 0 &&
                 key[5] >= '0' && key[5] <= '2') {
            fRingVoicing[key[5] - '0'] =
                static_cast<Voicing>(((v % kVoicingCount) + kVoicingCount)
                                     % kVoicingCount);
        }
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
        else if (std::strcmp(key, "latch") == 0)
            fLatchEnabled = (v != 0);
        else if (std::strcmp(key, "glideMode") == 0) {
            const GlideMode prev = fGlideMode;
            fGlideMode = static_cast<GlideMode>(
                ((v % kGlideModeCount) + kGlideModeCount) % kGlideModeCount);
            /* Entering MPE needs the bend range announced on the member
             * channels, which have never been told. */
            if (prev != fGlideMode && fGlideMode == kGlideMpe)
                fNeedsRpn = true;
        }
        else if (std::strcmp(key, "glideTimeMs") == 0)
            fGlideTimeMs = v;
        else if (std::strcmp(key, "bendRange") == 0)
            fBendRange = v;
        else if (std::strcmp(key, "panic") == 0)
            fPanic.store(true, std::memory_order_release);
        else if (std::strcmp(key, "gesture") == 0)
            queueGesture(value);

        /* A gesture in progress keeps the settings it started with, so that a
         * control change mid-drag cannot rewrite notes already emitted. */
    }

    /*
     * setState() runs on the main/UI thread, but MIDI may only be emitted from
     * run() on the audio thread. So a gesture is parsed here and handed over as a
     * single atomic word, which run() claims and acts on. One slot is enough: a
     * pointer gesture cannot outrun the audio callback, and if two arrive within a
     * block the later one is the current truth anyway.
     */
    void queueGesture(const char* value)
    {
        char verb[16] = {0};
        int  position = 0;
        int  ring     = 0;

        if (std::sscanf(value, "%15[^:]:%d:%d", verb, &position, &ring) != 3)
            return;
        if (position < 0 || position > 11 || ring < 0 || ring >= kRingCount)
            return;

        int kind;
        if (std::strcmp(verb, "press") == 0)        kind = kGesturePress;
        else if (std::strcmp(verb, "move") == 0)    kind = kGestureMove;
        else if (std::strcmp(verb, "release") == 0) kind = kGestureRelease;
        else return;

        fPendingGesture.store((kind << 16) | (ring << 8) | position,
                              std::memory_order_release);
    }

    String getState(const char* key) const override
    {
        char buf[16];
        int  v = 0;

        if (std::strncmp(key, "ext", 3) == 0 && key[3] >= '0' && key[3] <= '2')
            v = static_cast<int>(fRingExtension[key[3] - '0']);
        else if (std::strncmp(key, "voice", 5) == 0 && key[5] >= '0' && key[5] <= '2')
            v = static_cast<int>(fRingVoicing[key[5] - '0']);
        else if (std::strcmp(key, "octave") == 0)          v = fOctave;
        else if (std::strcmp(key, "velocity") == 0)        v = fVelocity;
        else if (std::strcmp(key, "velocityRandom") == 0)  v = fVelocityRandom;
        else if (std::strcmp(key, "noteLengthMs") == 0)    v = fNoteLengthMs;
        else if (std::strcmp(key, "holdToSustain") == 0)   v = fHoldToSustain ? 1 : 0;
        else if (std::strcmp(key, "latch") == 0)           v = fLatchEnabled ? 1 : 0;
        else if (std::strcmp(key, "glideMode") == 0)       v = static_cast<int>(fGlideMode);
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

        for (int i = 0; i < kMaxGroups; ++i)
            fGroup[i].active = false;
        std::memset(fHeld, 0, sizeof(fHeld));

        fDragSource  = -1;
        fGlideSource = -1;
        fPanic.store(true, std::memory_order_release);
    }

    /* The host may keep the plugin loaded across stops. Anything still sounding
     * when we are deactivated would hang until the host itself intervened, so
     * ask for a full silence on the next run(). */
    void deactivate() override
    {
        fPanic.store(true, std::memory_order_release);
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

        /* A panic silences everything, including anything the host or a previous
         * session left hanging. */
        if (fPanic.exchange(false, std::memory_order_acquire)) {
            stopAllGroups(0);
            zeroAllBends(0);
            fGlideActive = false;
        }

        /* Claim whatever gesture the UI left for us, if any. */
        const int32_t packed = fPendingGesture.exchange(kNoGesture,
                                                        std::memory_order_acquire);
        if (packed != kNoGesture)
            handleGesture(packed);

        if (fGlideActive)
            advanceGlide(frames);

        /* Fixed-length mode: the countdown, not the release, ends the note. */
        if (! fHoldToSustain && fNoteOffCountdown > 0) {
            fNoteOffCountdown -= static_cast<int32_t>(frames);
            if (fNoteOffCountdown <= 0)
                stopAllGroups(0);
        }
    }

private:
    static constexpr int kMaxVoices = 5;

    /* Gesture kinds, packed into the atomic handoff word as
     * (kind << 16) | (ring << 8) | position. */
    enum GestureKind {
        kGesturePress = 1,
        kGestureMove,
        kGestureRelease
    };
    static constexpr int32_t kNoGesture = -1;

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

        monitorLog(a, b, c);
    }

    /* Mirror every emitted message into the ring the UI drains for its event
     * log. See MonitorRing in CircleTheory.hpp for why this bypasses DPF's
     * state channel. */
    void monitorLog(uint8_t a, uint8_t b, uint8_t c)
    {
        fMonitor.push((static_cast<uint32_t>(a) << 16) |
                      (static_cast<uint32_t>(b) << 8)  |
                       static_cast<uint32_t>(c));
    }


    /* RPN 0,0 - pitch bend sensitivity, in semitones (spec section 7). */
    void sendBendRangeRpnOn(uint32_t frame, uint8_t channel)
    {
        const uint8_t cc = 0xB0 | (channel & 0x0F);
        sendRaw(frame, cc, 101, 0);                                  /* RPN MSB */
        sendRaw(frame, cc, 100, 0);                                  /* RPN LSB */
        sendRaw(frame, cc, 6, static_cast<uint8_t>(fBendRange));     /* data MSB */
        sendRaw(frame, cc, 38, 0);                                   /* data LSB */
        sendRaw(frame, cc, 101, 127);                                /* RPN null */
        sendRaw(frame, cc, 100, 127);
    }

    /* Announce the bend range everywhere it could be needed. In MPE mode each
     * member channel needs its own announcement, since a receiving instrument
     * tracks bend sensitivity per channel. */
    void sendBendRangeRpn(uint32_t frame)
    {
        sendBendRangeRpnOn(frame, fChannel);

        if (fGlideMode == kGlideMpe) {
            for (int i = 0; i < kMaxGroupNotes; ++i)
                sendBendRangeRpnOn(frame, mpeChannelFor(i));
        }
    }

    void sendPitchBend(uint32_t frame, uint8_t channel, float semitones)
    {
        const float norm = semitones / static_cast<float>(fBendRange);
        int value = 8192 + static_cast<int>(norm * 8191.0f);
        if (value < 0)     value = 0;
        if (value > 16383) value = 16383;

        sendRaw(frame, 0xE0 | (channel & 0x0F),
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

    /* ---- voice groups -----------------------------------------------------
     *
     * A "group" is one sounding selection. Several can overlap: two fingers down,
     * or a latched selection while a new one is pressed. fHeld counts how many
     * groups want each pitch, so a pitch is only silenced when the last group
     * that wanted it goes away - without that, releasing one chord would cut
     * notes another chord is still holding (C major and A minor share C and E).
     */

    /* Room for the doubling voicings, which add a voice. */
    static constexpr int kMaxGroupNotes = kMaxChordTones + 1;

    struct VoiceGroup {
        bool      active    = false;
        int       source    = -1;   /* packed position|ring that started it */
        int       root      = 0;
        ChordType type      = kChordMajor;
        Ring      ring      = kRingKey;
        uint8_t   velocity  = 100;
        int       count     = 0;
        uint8_t   note[kMaxGroupNotes] = {0};
        /* Per-voice channel. In MPE mode each voice gets its own so it can bend
         * independently; otherwise every voice shares the base channel. */
        uint8_t   chan[kMaxGroupNotes] = {0};
        /* Where each voice is heading during an MPE glide, and where it started,
         * so the ramp can interpolate per voice rather than uniformly. */
        uint8_t   target[kMaxGroupNotes] = {0};
        bool      mpe       = false;
    };

    static constexpr int kMaxGroups = 4;

    VoiceGroup fGroup[kMaxGroups];
    uint8_t    fHeld[128] = {0};

    VoiceGroup* findGroup(int source)
    {
        for (int i = 0; i < kMaxGroups; ++i)
            if (fGroup[i].active && fGroup[i].source == source)
                return &fGroup[i];
        return nullptr;
    }

    VoiceGroup* allocGroup()
    {
        for (int i = 0; i < kMaxGroups; ++i)
            if (! fGroup[i].active)
                return &fGroup[i];
        return nullptr;
    }

    int activeGroupCount() const
    {
        int n = 0;
        for (int i = 0; i < kMaxGroups; ++i)
            if (fGroup[i].active)
                ++n;
        return n;
    }

    /*
     * Start a chord as its own group.
     *
     * retriggerDuplicates decides what happens to a pitch another group already
     * holds. With glide off we resend it, because the fresh attack is the point of
     * a non-glide overlap. With glide on we skip it, since retriggering undercuts
     * the smooth motion. Either way the refcount is what governs note-off.
     */
    void startGroup(uint32_t frame, int source, int root, ChordType type,
                    Ring ring, uint8_t velocity, bool retriggerDuplicates)
    {
        VoiceGroup* g = allocGroup();
        if (g == nullptr) {
            /* All slots busy: reuse the oldest rather than dropping the gesture. */
            stopGroup(frame, &fGroup[0]);
            g = &fGroup[0];
        }

        uint8_t notes[kMaxGroupNotes];
        int n = buildChord(root, type, fOctave * 12, notes, kMaxChordTones);
        n = applyVoicing(notes, n, fRingVoicing[ring], kMaxGroupNotes);

        const bool mpe = (fGlideMode == kGlideMpe);

        g->active   = true;
        g->source   = source;
        g->root     = root;
        g->type     = type;
        g->ring     = ring;
        g->velocity = velocity;
        g->count    = n;
        g->mpe      = mpe;

        for (int i = 0; i < n; ++i) {
            const uint8_t note = notes[i];
            const bool    dup  = (fHeld[note] > 0);

            g->note[i]   = note;
            g->target[i] = note;
            g->chan[i]   = mpe ? mpeChannelFor(i) : fChannel;

            /* In MPE every voice owns its channel, so a duplicate pitch on a
             * different channel is not really a duplicate - always send it. */
            if (mpe || ! dup || retriggerDuplicates)
                sendRaw(frame, 0x90 | g->chan[i], note, velocity);

            ++fHeld[note];
        }
    }

    /* Release one group, silencing only the pitches no other group still wants. */
    void stopGroup(uint32_t frame, VoiceGroup* g)
    {
        if (g == nullptr || ! g->active)
            return;

        for (int i = 0; i < g->count; ++i) {
            const uint8_t note = g->note[i];

            if (g->mpe) {
                /* Dedicated channel: nothing else can be relying on this note. */
                sendRaw(frame, 0x80 | g->chan[i], note, 0);
                if (fHeld[note] > 0) --fHeld[note];
            } else if (fHeld[note] > 0 && --fHeld[note] == 0) {
                sendRaw(frame, 0x80 | g->chan[i], note, 0);
            }
        }

        g->active = false;
        g->count  = 0;
        g->source = -1;
    }

    /* MPE member channels start at 2 (channel 1 is the master zone), wrapping
     * within the 15 available. */
    uint8_t mpeChannelFor(int voiceIndex) const
    {
        return static_cast<uint8_t>(1 + (voiceIndex % 15));
    }

    void stopAllGroups(uint32_t frame)
    {
        for (int i = 0; i < kMaxGroups; ++i)
            stopGroup(frame, &fGroup[i]);
        fNoteOffCountdown = 0;

        /*
         * Belt and braces: with every group gone, nothing may still be counted
         * as held. If a refcount survived - an eviction, a chord rebuilt with a
         * different voice count, any bookkeeping slip - that pitch would sound
         * forever, because the count could never reach zero again. Sweep the
         * table and silence anything left over.
         */
        for (int n = 0; n < 128; ++n) {
            if (fHeld[n] != 0) {
                fHeld[n] = 0;
                /* The stranded note could be on any channel we use, and a
                 * spurious note-off is harmless, so cover them all. */
                sendRaw(frame, 0x80 | fChannel, static_cast<uint8_t>(n), 0);
                if (fGlideMode == kGlideMpe) {
                    for (int i = 0; i < kMaxGroupNotes; ++i)
                        sendRaw(frame, 0x80 | mpeChannelFor(i),
                                static_cast<uint8_t>(n), 0);
                }
            }
        }
    }

    /* Return every channel that might carry a bend to centre. */
    void zeroAllBends(uint32_t frame)
    {
        sendPitchBend(frame, fChannel, 0.0f);
        if (fGlideMode == kGlideMpe) {
            for (int i = 0; i < kMaxGroupNotes; ++i)
                sendPitchBend(frame, mpeChannelFor(i), 0.0f);
        }
    }

    /* ---- glide ------------------------------------------------------------ */

    /* Ramp the bend toward the target, then resolve with snap-and-reset (spec 6.2
     * step 4a): re-trigger at true pitches and zero the bend, so the recorded clip
     * holds real, editable note numbers rather than permanently bent ones. */
    void advanceGlide(uint32_t frames)
    {
        VoiceGroup* g = findGroup(fGlideSource);
        if (g == nullptr) {
            /* The gliding group went away underneath us. */
            fGlideActive = false;
            sendPitchBend(0, fChannel, 0.0f);
            return;
        }

        fGlideElapsed += frames;

        const float progress = fGlideDuration > 0
            ? static_cast<float>(fGlideElapsed) / static_cast<float>(fGlideDuration)
            : 1.0f;

        if (progress < 1.0f) {
            if (g->mpe) {
                /* Each voice travels its own distance, which is the whole point
                 * of MPE mode: it makes shape changes glidable. */
                for (int i = 0; i < g->count; ++i) {
                    const float delta = static_cast<float>(g->target[i]) -
                                        static_cast<float>(g->note[i]);
                    sendPitchBend(0, g->chan[i], progress * delta);
                }
            } else {
                sendPitchBend(0, fChannel,
                              progress * static_cast<float>(fGlideTargetSemis));
            }
            return;
        }

        /* Snap-and-reset (spec 6.2 step 4a): land on the bend, retire the bent
         * notes, restate the true ones, zero the bend. The recorded clip then
         * holds real, editable pitches rather than permanently bent ones. */
        if (g->mpe) {
            for (int i = 0; i < g->count; ++i) {
                const float delta = static_cast<float>(g->target[i]) -
                                    static_cast<float>(g->note[i]);
                sendPitchBend(0, g->chan[i], delta);
            }
        } else {
            sendPitchBend(0, fChannel, static_cast<float>(fGlideTargetSemis));
        }

        const uint8_t vel    = g->velocity;
        const int     source = g->source;
        const bool    wasMpe = g->mpe;
        uint8_t       chans[kMaxGroupNotes];
        const int     nchan  = g->count;
        for (int i = 0; i < nchan; ++i)
            chans[i] = g->chan[i];

        stopGroup(0, g);
        startGroup(0, source, fGlideTargetRoot, fGlideTargetType,
                   fGlideTargetRing, vel, /* retriggerDuplicates */ false);

        /* Zero every channel that carried a bend, not just the base one. */
        if (wasMpe) {
            for (int i = 0; i < nchan; ++i)
                sendPitchBend(0, chans[i], 0.0f);
        } else {
            sendPitchBend(0, fChannel, 0.0f);
        }

        fGlideActive      = false;
        fGlideTargetSemis = 0;
    }

    /*
     * Act on a gesture claimed from the UI. Settings are read here, once, at the
     * moment the gesture arrives - this is the point where spec section 5's rule
     * takes physical effect, because everything downstream is concrete MIDI.
     */
    void handleGesture(int32_t packed)
    {
        const int kind     = (packed >> 16) & 0xFF;
        const int ring     = (packed >> 8) & 0xFF;
        const int position = packed & 0xFF;

        const Ring      r    = static_cast<Ring>(ring);
        const int       root = rootForPosition(position, r);
        const ChordType type = chordTypeForRing(r);

        const int source = (ring << 8) | position;

        switch (kind) {
            case kGesturePress: {
                /*
                 * Latch mode: a selection keeps sounding after the pointer is
                 * released. Clicking it again turns it off; clicking a different
                 * one replaces it. So exactly one latched selection sounds at a
                 * time, and a press is a toggle rather than a start.
                 */
                if (fLatchEnabled) {
                    VoiceGroup* same = findGroup(source);
                    if (same != nullptr) {
                        if (fGlideActive && fGlideSource == source) {
                            fGlideActive = false;
                            sendPitchBend(0, fChannel, 0.0f);
                        }
                        stopGroup(0, same);
                        break;
                    }

                    fGlideActive = false;
                    stopAllGroups(0);
                    fGestureVelocity = pickVelocity();
                    startGroup(0, source, root, type, r, fGestureVelocity, true);
                    fDragSource = source;
                    break;
                }

                /*
                 * Momentary mode: the selection sounds while held. Pressing a
                 * second one while the first is down is an overlap, which is
                 * where harmonising happens - the earlier group is left alone.
                 */
                fGestureVelocity = pickVelocity();
                /* With glide off, a duplicate pitch is resent so the new chord
                 * attacks; with glide on it is left alone so nothing retriggers
                 * mid-movement. */
                startGroup(0, source, root, type, r, fGestureVelocity,
                           /* retriggerDuplicates */ fGlideMode == kGlideOff);
                fDragSource = source;

                fNoteOffCountdown = fHoldToSustain
                    ? 0
                    : static_cast<int32_t>(fNoteLengthMs * fSampleRate / 1000.0);
                break;
            }

            case kGestureMove: {
                /* A drag moves the group it started, identified by the source it
                 * currently carries. */
                VoiceGroup* g = findGroup(fDragSource);
                if (g == nullptr)
                    break;

                /*
                 * Whether a glide is possible at all depends on the mode and on
                 * whether the chord SHAPE changes:
                 *
                 *   off  never glide.
                 *   on   a single bend moves every voice by one interval, which
                 *        only works when the shape is unchanged. Rings have
                 *        uniform shapes, so this means "within a ring".
                 *   mpe  each voice has its own channel and bends independently,
                 *        so any chord can reach any other.
                 */
                const bool shapeKept = sameShape(g->type, type);
                const bool canGlide  = (fGlideMode == kGlideMpe) ||
                                       (fGlideMode == kGlideOn && shapeKept);

                if (! canGlide) {
                    /* Retrigger cleanly (spec 6.2 step 5). Shared pitches are
                     * resent so the new chord attacks properly. */
                    const uint8_t vel = g->velocity;
                    stopGroup(0, g);
                    startGroup(0, source, root, type, r, vel, true);
                    fDragSource = source;
                    break;
                }

                fGlideTargetSemis = shortestSemitoneDelta(g->root, root);
                fGlideTargetRoot  = root;
                fGlideTargetType  = type;
                fGlideTargetRing  = r;
                fGlideSource      = g->source;
                fGlideElapsed     = 0;
                fGlideDuration    = static_cast<uint32_t>(
                    fGlideTimeMs * fSampleRate / 1000.0);

                if (fGlideMode == kGlideMpe) {
                    /* Work out where each voice must land, pairing by index. A
                     * voice with no counterpart (the chords differ in size) stays
                     * where it is and is resolved by the snap at the end. */
                    uint8_t want[kMaxGroupNotes];
                    int n = buildChord(root, type, fOctave * 12, want,
                                       kMaxChordTones);
                    n = applyVoicing(want, n, fRingVoicing[r], kMaxGroupNotes);

                    for (int i = 0; i < g->count; ++i)
                        g->target[i] = (i < n) ? want[i] : g->note[i];

                    /* Something must actually move for a glide to be worth it. */
                    bool moves = false;
                    for (int i = 0; i < g->count && ! moves; ++i)
                        moves = (g->target[i] != g->note[i]);
                    fGlideActive = moves;
                } else {
                    fGlideActive = (fGlideTargetSemis != 0);
                }

                /* Nothing to travel, but the voicing or shape may still differ. */
                if (! fGlideActive && (type != g->type || r != g->ring)) {
                    const uint8_t vel = g->velocity;
                    stopGroup(0, g);
                    startGroup(0, source, root, type, r, vel, false);
                    fDragSource = source;
                }
                break;
            }

            case kGestureRelease: {
                /* Latched selections ignore the release - that is the point. */
                if (fLatchEnabled)
                    break;

                /*
                 * Release EVERYTHING, not just the group the drag thinks it is
                 * holding.
                 *
                 * A drag retires and starts groups as it crosses cells, so more
                 * than one can be live by the time the pointer comes up, and any
                 * group the release misses sounds forever. Chasing the exact
                 * group here was the bug: the pointer is up, so nothing this
                 * plugin generated should still be sounding. Silence it all and
                 * the class of stuck-note bugs goes with it.
                 */
                if (fGlideActive) {
                    fGlideActive = false;
                    zeroAllBends(0);
                }

                if (fHoldToSustain) {
                    stopAllGroups(0);
                    fDragSource = -1;
                    fGlideSource = -1;
                }
                /* In fixed-length mode the countdown owns the note-off. */
                break;
            }
        }
    }

    /* A ring's quality is fixed; the per-ring extension builds on it. */
    ChordType chordTypeForRing(Ring ring) const
    {
        return extendChord(defaultChordForRing(ring), fRingExtension[ring]);
    }


    /* ---- settings: live state, never automation (spec section 5) ---------- */
    /*
     * Per-ring settings. A ring's quality is fixed by the wheel (major, minor,
     * diminished); the user chooses an extension and a voicing for the ring as a
     * whole, never per cell. Uniformity is what keeps within-ring glide legal:
     * every cell in a ring yields the same interval pattern, which is exactly
     * what spec 6.1 requires for a single pitch bend to carry all voices.
     */
    Extension fRingExtension[kRingCount] = { kExtNone, kExtNone, kExtNone };
    Voicing   fRingVoicing[kRingCount]   = {
        kVoicingRegular, kVoicingRegular, kVoicingRegular
    };
    int       fOctave         = 4;
    uint8_t   fVelocity       = 100;
    int       fVelocityRandom = 0;
    int       fNoteLengthMs   = 500;
    bool      fHoldToSustain  = true;
    /* Latch: a selection keeps sounding after the pointer is released, until it
     * is clicked again or another selection replaces it. */
    bool      fLatchEnabled   = false;
    GlideMode fGlideMode      = kGlideOn;
    int       fGlideTimeMs    = 120;
    int       fBendRange      = 12;

    /* ---- runtime voicing state -------------------------------------------- */
    uint8_t  fChannel     = 0;
    double   fSampleRate  = 48000.0;
    bool     fNeedsRpn    = true;
    int32_t  fNoteOffCountdown = 0;
    /* The group a drag is currently moving, as a packed position|ring. */
    int      fDragSource  = -1;
    int      fGlideSource = -1;

    /* Written by setState() on the UI thread, claimed by run() on the audio
     * thread. See queueGesture(). */
    std::atomic<int32_t> fPendingGesture { kNoGesture };
    uint8_t   fGestureVelocity = 100;

    /* DSP -> UI event log. Always built; the UI reads it via direct access. */
    MonitorRing fMonitor;

    /* Set from the UI or on (de)activate; consumed by run() to silence
     * everything. */
    std::atomic<bool> fPanic { false };


    bool      fGlideActive      = false;
    uint32_t  fGlideElapsed     = 0;
    uint32_t  fGlideDuration    = 0;
    int       fGlideTargetSemis = 0;
    int       fGlideTargetRoot  = 0;
    ChordType fGlideTargetType  = kChordMajor;
    Ring      fGlideTargetRing  = kRingKey;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FortyFifthPlugin)
};

Plugin* createPlugin()
{
    return new FortyFifthPlugin();
}

END_NAMESPACE_DISTRHO
