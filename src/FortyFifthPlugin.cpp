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
#include <cstdarg>
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
    std::map<void*, ActiveCells*> gCellsRegistry;
    std::map<void*, LogRing*>     gLogRegistry;
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
    gCellsRegistry.erase(instance);
    gLogRegistry.erase(instance);
}

void registerActiveCells(void* instance, ActiveCells* cells)
{
    const std::lock_guard<std::mutex> lock(gRegistryMutex);
    gCellsRegistry[instance] = cells;
}

ActiveCells* activeCellsFor(void* instance)
{
    const std::lock_guard<std::mutex> lock(gRegistryMutex);
    const std::map<void*, ActiveCells*>::const_iterator it =
        gCellsRegistry.find(instance);
    return (it != gCellsRegistry.end()) ? it->second : nullptr;
}

MonitorRing* monitorRingFor(void* instance)
{
    const std::lock_guard<std::mutex> lock(gRegistryMutex);
    const std::map<void*, MonitorRing*>::const_iterator it = gRegistry.find(instance);
    return (it != gRegistry.end()) ? it->second : nullptr;
}

void registerLogRing(void* instance, LogRing* ring)
{
    const std::lock_guard<std::mutex> lock(gRegistryMutex);
    gLogRegistry[instance] = ring;
}

LogRing* logRingFor(void* instance)
{
    const std::lock_guard<std::mutex> lock(gRegistryMutex);
    const std::map<void*, LogRing*>::const_iterator it =
        gLogRegistry.find(instance);
    return (it != gLogRegistry.end()) ? it->second : nullptr;
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
        std::memcpy(fKeyMap, kDefaultKeyMap, sizeof(fKeyMap));
        registerMonitorRing(this, &fMonitor);
        registerActiveCells(this, &fCells);
        registerLogRing(this, &fLog);
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
    /* This project's own licence, not the framework's. DPF is ISC and that is
     * recorded in LICENSE; what a host shows should be the plugin's terms. */
    const char* getLicense() const override { return "MIT"; }
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
        kStateSelectedKey,
        kStateSingleNotes,
        kStateKeyMap,
        kStatePedalAction,
        /*
         * The sequencer.
         *
         * These MUST be declared here, not merely handled in setState(). DPF
         * routes only the keys initState() declares; anything else is dropped
         * before it reaches the plugin. That is why PLAY appeared to do
         * nothing in a host - the button sent "progRunning" and the DSP never
         * heard it, so fProgRunning stayed false and runSequencer() returned
         * immediately, every block, forever.
         */
        kStateProgression,
        kStateProgRunning,
        kStateProgLegato,
        kStateUiScreen,
        kStateStorageMode,
        kStateMergeWindowMs,
        kStateLogEnabled,
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
            case kStateSelectedKey:
                /* Which key the wheel is centred on. Previously a pure UI
                 * concern, but the keyboard mapping is by degree, so the DSP
                 * must know the key to resolve an incoming note to a cell. */
                state.key = "selectedKey";
                state.label = "Selected Key";
                state.defaultValue = "0";
                break;
            case kStateSingleNotes:
                /* Bypass chord generation: sound the root alone. */
                state.key = "singleNotes";
                state.label = "Single Notes";
                state.defaultValue = "0";
                break;
            case kStateKeyMap:
                /* All twelve bindings in one value, "action:value" per key,
                 * comma separated - twelve separate state keys would bloat the
                 * host's saved state for no benefit. */
                state.key = "keyMap";
                state.label = "Keyboard Map";
                state.defaultValue = defaultKeyMapString();
                break;
            case kStatePedalAction:
                state.key = "pedalAction";
                state.label = "Sustain Pedal";
                state.defaultValue = "0";
                break;
            case kStateProgression:
                /* The whole grid as one string - see parseProgression(). */
                state.key = "progression";
                state.label = "Progression";
                state.defaultValue = "";
                break;
            case kStateProgRunning:
                /* Not saved as running: a project that reopened already
                 * playing would surprise, and the transport is the user's to
                 * start. */
                state.key = "progRunning";
                state.label = "Sequencer Running";
                state.defaultValue = "0";
                break;
            case kStateProgLegato:
                state.key = "progLegato";
                state.label = "Sequencer Legato";
                state.defaultValue = "1";
                break;
            case kStateUiScreen:
                /* The DSP gates incoming MIDI on this: the keyboard is inert
                 * while the sequencer screen is showing. */
                state.key = "uiScreen";
                state.label = "Editor Screen";
                state.defaultValue = "1";
                break;
            case kStateStorageMode:
                state.key = "storageMode";
                state.label = "Storage Location";
                state.defaultValue = "0";
                break;
            case kStateMergeWindowMs:
                state.key = "mergeWindowMs";
                state.label = "Merge Window (ms)";
                state.defaultValue = "20";
                break;
            case kStateLogEnabled:
                /* Off by default: this is for chasing a fault, not for
                 * running. A log left on writes a file forever. */
                state.key = "logEnabled";
                state.label = "Write Diagnostic Log";
                state.defaultValue = "0";
                break;
        }
    }

    /* "action:value" per key, comma separated, twelve entries. Plain text so a
     * saved session stays readable and diffable. */
    static String defaultKeyMapString()
    {
        char buf[128] = {0};
        for (int i = 0; i < 12; ++i) {
            char one[16];
            std::snprintf(one, sizeof(one), "%s%d:%d", i ? "," : "",
                          static_cast<int>(kDefaultKeyMap[i].action),
                          kDefaultKeyMap[i].value);
            std::strncat(buf, one, sizeof(buf) - std::strlen(buf) - 1);
        }
        return String(buf);
    }

    void parseKeyMap(const char* value) { decodeKeyMap(value, fKeyMap); }

    /*
     * Take a grid from the editor or from a restored session.
     *
     * Handed to the audio thread through a second buffer and a flag it checks
     * between beats, so a grid can never change part-way through a chord.
     * fSavedProg is the copy getState() reads: fProg belongs to the audio
     * thread and only updates between beats, so reading it there could hand
     * the host a grid one block out of date - or, if the transport never ran,
     * the grid the plugin started with rather than the one the user built.
     */
    void parseProgression(const char* value)
    {
        Progression parsed;
        if (! decodeProgression(value, parsed))
            return;   /* nothing usable - keep playing what we have */

        fPendingProg = parsed;
        fProgDirty.store(true, std::memory_order_release);
        fSavedProg = parsed;
    }

    void setState(const char* key, const char* value) override
    {
        /* Before the numeric parse: this one is a structured string, and
         * atoi would read only its first field. */
        if (std::strcmp(key, "keyMap") == 0) {
            parseKeyMap(value);
            return;
        }
        if (std::strcmp(key, "progression") == 0) {
            parseProgression(value);
            return;
        }

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
        else if (std::strcmp(key, "octave") == 0) {
            if (v != fOctave) {
                fOctave = v;
                /* Ask the audio thread to move anything already sounding. The
                 * slider is a performance control - dragging it while a chord
                 * is held must carry that chord with it, not merely change
                 * where the next one starts. */
                fOctaveMoved.store(true, std::memory_order_release);
            }
        }
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

            /* The mode decides whether voice leading applies at all, so a
             * change makes the stored reference chord describe a voicing the
             * new mode would not have produced. Drop it and start fresh. */
            if (prev != fGlideMode)
                fLastChordCount = 0;
        }
        else if (std::strcmp(key, "glideTimeMs") == 0)
            fGlideTimeMs = v;
        else if (std::strcmp(key, "bendRange") == 0)
            fBendRange = v;
        else if (std::strcmp(key, "selectedKey") == 0)
            fSelectedKey.store(((v % 12) + 12) % 12, std::memory_order_release);
        else if (std::strcmp(key, "singleNotes") == 0)
            fSingleNotes = (v != 0);
        else if (std::strcmp(key, "pedalAction") == 0)
            fPedalAction = static_cast<PedalAction>(
                ((v % kPedalActionCount) + kPedalActionCount) % kPedalActionCount);
        else if (std::strcmp(key, "progLegato") == 0)
            fProgLegato = (v != 0);
        else if (std::strcmp(key, "uiScreen") == 0)
            fUiScreen.store(v, std::memory_order_release);
        else if (std::strcmp(key, "storageMode") == 0)
            fStorageMode = static_cast<StorageMode>(
                ((v % kStorageModeCount) + kStorageModeCount)
                % kStorageModeCount);
        else if (std::strcmp(key, "mergeWindowMs") == 0)
            fMergeWindowMs = (v < 0) ? 0
                           : (v > kMergeWindowMsMax) ? kMergeWindowMsMax : v;
        else if (std::strcmp(key, "logEnabled") == 0)
            fLogEnabled.store(v != 0, std::memory_order_release);
        else if (std::strcmp(key, "progRunning") == 0) {
            const bool want = (v != 0);
            if (want != fProgRunning.load(std::memory_order_acquire)) {
                fProgRunning.store(want, std::memory_order_release);
                /* Stopping must silence the sequencer's own chord, or a legato
                 * one would hang for good - there is no next trigger coming to
                 * replace it. Left to the audio thread, which owns the voices. */
                if (! want)
                    fProgStopping.store(true, std::memory_order_release);
            }
        }
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
        if (ring < 0 || ring >= kRingCount)
            return;

        /* Validate against the ring's OWN cell count, not a fixed 12. The minor
         * ring has 24, so a hardcoded upper bound silently discarded half of it
         * - cells 12-23 produced no sound at all, because the gesture was
         * dropped here before the audio thread ever saw it. */
        if (position < 0 || position >= segmentsInRing(static_cast<Ring>(ring)))
            return;

        int kind;
        if (std::strcmp(verb, "press") == 0)          kind = kGesturePress;
        else if (std::strcmp(verb, "move") == 0)      kind = kGestureMove;
        else if (std::strcmp(verb, "release") == 0)   kind = kGestureRelease;
        else if (std::strcmp(verb, "retrigger") == 0) kind = kGestureRetrigger;
        else return;

        fPendingGesture.store((kind << 16) | (ring << 8) | position,
                              std::memory_order_release);
    }

    String getState(const char* key) const override
    {
        /* Structured, not numeric - handled before the integer path below,
         * where atoi would read only the first field. Both encoders are shared
         * with the editor, so what is saved is exactly what was sent. */
        if (std::strcmp(key, "progression") == 0) {
            char out[kProgStringMax];
            encodeProgression(fSavedProg, out, sizeof out);
            return String(out);
        }
        if (std::strcmp(key, "keyMap") == 0) {
            char out[kKeyMapStringMax];
            encodeKeyMap(fKeyMap, out, sizeof out);
            return String(out);
        }

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
        else if (std::strcmp(key, "selectedKey") == 0)
            v = fSelectedKey.load(std::memory_order_acquire);
        else if (std::strcmp(key, "singleNotes") == 0)  v = fSingleNotes ? 1 : 0;
        else if (std::strcmp(key, "pedalAction") == 0)
            v = static_cast<int>(fPedalAction);
        else if (std::strcmp(key, "progLegato") == 0)   v = fProgLegato ? 1 : 0;
        else if (std::strcmp(key, "progRunning") == 0)
            v = fProgRunning.load(std::memory_order_acquire) ? 1 : 0;
        else if (std::strcmp(key, "uiScreen") == 0)
            v = fUiScreen.load(std::memory_order_acquire);
        else if (std::strcmp(key, "storageMode") == 0)
            v = static_cast<int>(fStorageMode);
        else if (std::strcmp(key, "mergeWindowMs") == 0) v = fMergeWindowMs;
        else if (std::strcmp(key, "logEnabled") == 0)
            v = fLogEnabled.load(std::memory_order_acquire) ? 1 : 0;

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
        /* A pedal or note left down across a restart would defer releases
         * forever, so clear the input state too. */
        fPedalDown        = false;
        fLastKeyboardNote = -1;
        fHeldKeyCount     = 0;
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
        /* A fresh block means a fresh output buffer. DPF forbids writing again
         * after a refusal until now, so this is where that latch clears. */
        fOutputFull = false;

        /*
         * A refused note-off left the instrument holding a note nothing on
         * this side will release. Retry it now that the buffer is fresh, since
         * an all-notes-off is cheap and a stuck note is not.
         */
        if (fStuckNotes) {
            fStuckNotes = false;
            for (uint8_t ch = 0; ch < 16; ++ch)
                sendRaw(0, 0xB0 | ch, 123, 0);   /* all notes off */
            std::memset(fHeld, 0, sizeof(fHeld));
            logLine("recovered from refused note-offs: all notes off");
        }

        /* Announce the bend range once, before any glide can need it (spec 6.2
         * step 2). Doing it here rather than in activate() guarantees the host has
         * a real event buffer to receive it. */
        /* One channel per block; sendBendRangeRpn() clears fNeedsRpn when it
         * has worked through them all. */
        if (fNeedsRpn)
            sendBendRangeRpn(0);

        /* A panic silences everything, including anything the host or a previous
         * session left hanging. */
        if (fPanic.exchange(false, std::memory_order_acquire)) {
            /*
             * An UNCONDITIONAL all-notes-off, before anything else.
             *
             * stopAllGroups() can only silence notes the refcount says are
             * held, so it cannot recover from a refcount that is itself wrong
             * - which is exactly the state a dropped note-on leaves behind.
             * Panic was therefore useless against the one failure it most
             * needed to fix, and the user found that toggling glide worked
             * where panic did not.
             *
             * This asks the instrument to release everything regardless of
             * what this side believes, then resets the bookkeeping to match.
             */
            for (uint8_t ch = 0; ch < 16; ++ch) {
                sendRaw(0, 0xB0 | ch, 123, 0);   /* all notes off */
                sendRaw(0, 0xB0 | ch, 121, 0);   /* reset controllers */
            }
            std::memset(fHeld, 0, sizeof(fHeld));

            stopAllGroups(0);
            zeroAllBends(0);
            fGlideActive  = false;
            /* A panic is a full reset: forget the pedal and the keys too, or a
             * pedal believed to be down would defer every later release. */
            fPedalDown    = false;
            fHeldKeyCount = 0;

            fStuckNotes    = false;
            fDroppedEvents = 0;
            fCells.dropped.store(0, std::memory_order_release);
            logLine("panic: all notes off on every channel");
        }

        /* Incoming MIDI, before gestures: a controller note triggers a cell just
         * as a click does, so it must be handled by the same machinery. */
        for (uint32_t i = 0; i < midiEventCount; ++i)
            handleMidiIn(midiEvents[i]);

        /* The octave slider moved: carry sounding chords with it. */
        if (fOctaveMoved.exchange(false, std::memory_order_acquire))
            retuneToOctave(0);

        /* Claim whatever gesture the UI left for us, if any. */
        const int32_t packed = fPendingGesture.exchange(kNoGesture,
                                                        std::memory_order_acquire);
        if (packed != kNoGesture)
            handleGesture(packed);

        /* The sequencer, before the glide: a beat that lands this block should
         * be sounding by the time the glide is advanced over it. */
        runSequencer();

        if (fGlideActive)
            advanceGlide(frames);

        /* Age every sounding group, for the merge window. Counted in frames
         * rather than from a clock so it follows the host's own timeline, and
         * saturating rather than wrapping - a group held for half an hour must
         * not suddenly look newly started. */
        for (int i = 0; i < kMaxGroups; ++i) {
            if (! fGroup[i].active)
                continue;
            if (fGroup[i].age < 0xFFFFFFFFu - frames)
                fGroup[i].age += frames;
        }

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
        kGestureRelease,
        /*
         * Same cell, different settings - a drag within one Slide Mode column,
         * where the section changed the octave or the extension but not the
         * chord's position. A "move" would be a no-op there, since the DSP
         * would see the same cell; this says "rebuild it" in one gesture,
         * which matters because the handoff holds a single slot and a
         * release-then-press pair would lose the release.
         */
        kGestureRetrigger
    };
    static constexpr int32_t kNoGesture = -1;

    /* ---- MIDI emission helpers ------------------------------------------- */

    /*
     * Emit one MIDI message, and say whether the host actually took it.
     *
     * writeMidiEvent() returns false when the host's output buffer is full,
     * and DPF is explicit that nothing more may be written until the next
     * run(). Ignoring that return was the cause of a real and confusing bug:
     * a chord is up to ten messages in one block (note-offs then note-ons), so
     * a buffer filling part-way through silently dropped the rest. What
     * reached the instrument was one note, or a fragment of a chord, while the
     * monitor and the piano roll happily showed the whole thing - because they
     * logged what was ATTEMPTED, not what was accepted.
     *
     * Now the monitor logs only what the host took, so the UI stops lying, and
     * callers can keep their own bookkeeping honest. fDroppedEvents counts the
     * rest, because a count of dropped messages is the one number that
     * explains this class of symptom.
     */
    bool sendRaw(uint32_t frame, uint8_t a, uint8_t b, uint8_t c,
                 uint8_t size = 3)
    {
        /* Once the buffer has refused an event this block, DPF says not to try
         * again until the next run(). Trying anyway is undefined. */
        if (fOutputFull) {
            ++fDroppedEvents;
        fCells.dropped.store(fDroppedEvents, std::memory_order_release);
            logLine("DROP (buffer full) %02X %02X %02X", a, b, c);
            return false;
        }

        MidiEvent ev;
        ev.frame   = frame;
        ev.size    = size;
        ev.data[0] = a;
        ev.data[1] = b;
        ev.data[2] = c;
        ev.data[3] = 0;
        ev.dataExt = nullptr;

        if (! writeMidiEvent(ev)) {
            fOutputFull = true;
            ++fDroppedEvents;
        fCells.dropped.store(fDroppedEvents, std::memory_order_release);
            logLine("DROP (host refused) %02X %02X %02X", a, b, c);
            return false;
        }

        monitorLog(a, b, c);
        logLine("out %02X %02X %02X", a, b, c);
        return true;
    }

    /*
     * Write one line to the diagnostic log, if it is enabled.
     *
     * Formats on the audio thread, which is fine - vsnprintf into a stack
     * buffer allocates nothing - and hands the line to a ring the UI drains to
     * a file. The audio thread never touches the filesystem.
     *
     * Costs nothing when logging is off: one atomic load and a return.
     */
    void logLine(const char* fmt, ...)
    {
        if (! fLogEnabled.load(std::memory_order_relaxed))
            return;

        char buf[LogRing::kLineMax];

        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, args);
        va_end(args);

        fLog.push(buf);
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

    /*
     * Announce the bend range everywhere it could be needed. In MPE mode each
     * member channel needs its own announcement, since a receiving instrument
     * tracks bend sensitivity per channel.
     *
     * ONE CHANNEL PER BLOCK. The full announcement is six messages per
     * channel, and in MPE that is thirty-six in a burst - easily enough to
     * fill a host's output buffer on its own. When that happened, every
     * note-on for the chord being played at the same moment was silently
     * dropped, and the chord arrived in pieces or not at all.
     *
     * Spreading the announcement over consecutive blocks costs a few
     * milliseconds before the first bend is accurate, which is inaudible, and
     * removes a burst that was corrupting real playing.
     */
    void sendBendRangeRpn(uint32_t frame)
    {
        if (fRpnChannel == 0) {
            sendBendRangeRpnOn(frame, fChannel);
            ++fRpnChannel;

            /* Non-MPE needs only the base channel. */
            if (fGlideMode != kGlideMpe) {
                fRpnChannel = 0;
                fNeedsRpn   = false;
            }
            return;
        }

        const int i = fRpnChannel - 1;
        sendBendRangeRpnOn(frame, mpeChannelFor(i));

        if (++fRpnChannel > kMaxGroupNotes) {
            fRpnChannel = 0;
            fNeedsRpn   = false;
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
        /* The octave this group was built at. A pointer gesture takes the
         * slider's value, but a played note takes the octave it was played in,
         * so the two cannot share one global. Stored per group because a glide
         * rebuilds the chord and must land in the same octave it started in. */
        int       octave    = 4;
        /* Note number that started this group, or -1 for a pointer gesture.
         * Note-off has to find the group its own note began, which source
         * alone cannot identify once two octaves play the same cell. */
        int       midiNote  = -1;
        /* Frames of audio since this group started, counted in run(). Used to
         * decide whether a following trigger is close enough to merge with it
         * rather than replace it. */
        uint32_t  age       = 0;
        /* Key released, but the sustain pedal is holding the sound on. */
        bool      deferred  = false;
    };

    /* Enough for a two-handed chord on the keyboard with the pedal down, which
     * is the realistic worst case now that notes can trigger cells. */
    static constexpr int kMaxGroups = 10;

    VoiceGroup fGroup[kMaxGroups];
    uint8_t    fHeld[128] = {0};

    /* A source packs the cell as (ring << 8) | position. Decoded here rather
     * than open-coded at each use, so the two halves cannot drift apart. */
    static int position_from_source(int source) { return source & 0xFF; }
    static Ring ring_from_source(int source)
    {
        return static_cast<Ring>((source >> 8) & 0xFF);
    }

    VoiceGroup* findGroup(int source)
    {
        for (int i = 0; i < kMaxGroups; ++i)
            if (fGroup[i].active && fGroup[i].source == source)
                return &fGroup[i];
        return nullptr;
    }

    /* Find the group a specific played note started. Two octaves of the same
     * key map to the same cell, so source is ambiguous and the note number is
     * the only thing that identifies the group to release. */
    VoiceGroup* findGroupByNote(int midiNote)
    {
        for (int i = 0; i < kMaxGroups; ++i)
            if (fGroup[i].active && fGroup[i].midiNote == midiNote)
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
                    Ring ring, uint8_t velocity, bool retriggerDuplicates,
                    int octave, int midiNote = -1)
    {
        VoiceGroup* g = allocGroup();
        if (g == nullptr) {
            /* All slots busy. Evict a pointer-triggered group first: a keyboard
             * group has a key physically down, and stealing it would leave the
             * player holding a silent note with no way to retrigger it. */
            for (int i = 0; i < kMaxGroups && g == nullptr; ++i)
                if (fGroup[i].midiNote < 0)
                    g = &fGroup[i];
            if (g == nullptr)
                g = &fGroup[0];
            stopGroup(frame, g);
        }

        uint8_t notes[kMaxGroupNotes];
        const int n = buildCellChord(root, type, ring, octave, notes);

        const bool mpe = (fGlideMode == kGlideMpe);

        g->active   = true;
        g->source   = source;
        g->root     = root;
        g->type     = type;
        g->ring     = ring;
        g->velocity = velocity;
        g->count    = n;
        g->mpe      = mpe;
        g->octave   = octave;
        g->midiNote = midiNote;
        g->age      = 0;

        int written = 0;

        for (int i = 0; i < n; ++i) {
            const uint8_t note = notes[i];
            const bool    dup  = (fHeld[note] > 0);

            g->note[i]   = note;
            g->target[i] = note;
            g->chan[i]   = mpe ? mpeChannelFor(i) : fChannel;

            /* In MPE every voice owns its channel, so a duplicate pitch on a
             * different channel is not really a duplicate - always send it. */
            const bool needsSend = (mpe || ! dup || retriggerDuplicates);

            /*
             * The refcount must track what is SOUNDING, not what was asked
             * for. Counting a note whose note-on the host refused left
             * fHeld[note] claiming a note was down that never started: its
             * note-off was then suppressed as a duplicate, and a note that had
             * never sounded held a phantom reference for the rest of the
             * session. That is the stuck-and-missing-note behaviour.
             *
             * A voice that could not be sent is dropped from the group
             * entirely, so stopGroup() will not later try to silence it.
             */
            if (needsSend && ! sendRaw(frame, 0x90 | g->chan[i], note, velocity))
                continue;

            g->note[written] = note;
            g->target[written] = note;
            g->chan[written] = g->chan[i];
            ++written;

            ++fHeld[note];
        }

        /* A group that lost voices is still a group - the ones that did sound
         * must still be released properly. A group that lost ALL of them is
         * not, and would otherwise sit active and silent forever. */
        g->count = written;
        if (written == 0) {
            g->active = false;
            logLine("group dropped: no voices reached the host");
            return;
        }

        /* Light the cell. Driven from the group rather than from the gesture,
         * so the UI shows what is actually SOUNDING - including notes played
         * from a MIDI keyboard, which never pass through the UI at all. */
        fCells.set(ring, position_from_source(source), true);

        /* Remember this chord as the reference the next one leads from. Kept
         * even after the chord stops, so a gap between chords still leads
         * smoothly rather than resetting to root position. */
        if (! fSingleNotes) {
            fLastChordCount = (n > kMaxGroupNotes) ? kMaxGroupNotes : n;
            for (int i = 0; i < fLastChordCount; ++i)
                fLastChord[i] = g->note[i];
        }
    }

    /* Release one group, silencing only the pitches no other group still wants. */
    void stopGroup(uint32_t frame, VoiceGroup* g)
    {
        if (g == nullptr || ! g->active)
            return;

        for (int i = 0; i < g->count; ++i) {
            const uint8_t note = g->note[i];

            /*
             * The refcount drops only when the note-off is actually accepted.
             * Decrementing on a refused note-off would leave the instrument
             * holding a note that this side believes is already released - a
             * stuck note with nothing left to release it.
             *
             * A refused note-off is recorded, and the next panic clears it.
             * There is no better recovery inside one block: DPF forbids
             * writing again until the next run(), and the group is going away
             * now.
             */
            if (g->mpe) {
                /* Dedicated channel: nothing else can be relying on this note. */
                if (sendRaw(frame, 0x80 | g->chan[i], note, 0)) {
                    if (fHeld[note] > 0) --fHeld[note];
                } else {
                    fStuckNotes = true;
                }
            } else if (fHeld[note] == 1) {
                /* Last holder: this note-off really silences the pitch. */
                if (sendRaw(frame, 0x80 | g->chan[i], note, 0))
                    --fHeld[note];
                else
                    fStuckNotes = true;
            } else if (fHeld[note] > 1) {
                /* Another group still wants this pitch: no message needed, and
                 * the refcount drops with no risk of a drop. */
                --fHeld[note];
            }
        }

        /*
         * Unlight the cell only when no OTHER group still holds it. Two groups
         * can share a cell - a latched chord and a new press on the same one -
         * and clearing unconditionally would darken a cell that is still
         * sounding, which is the highlight telling a lie.
         */
        const int  src  = g->source;
        const Ring r    = ring_from_source(src);
        const int  pos  = position_from_source(src);

        g->active = false;
        g->count  = 0;
        g->source = -1;

        if (src >= 0) {
            bool stillHeld = false;
            for (int i = 0; i < kMaxGroups && ! stillHeld; ++i)
                stillHeld = (fGroup[i].active && fGroup[i].source == src);
            if (! stillHeld)
                fCells.set(r, pos, false);
        }
        /* Clear the identity too. A recycled slot that kept a stale note number
         * would be found by the next lookup for that note and silence the wrong
         * chord - the same class of bookkeeping slip that stranded notes before. */
        g->midiNote = -1;
        g->deferred = false;
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

        /* Everything stopped, so nothing may still be lit. Clearing outright
         * rather than relying on the per-group unlight, which cannot know
         * about the stranded pitches this function also sweeps. */
        fCells.clear();

        /* Nothing is sounding, so nothing may still be referenced. A stale
         * last-note would make the next keypress try to glide from a group
         * that no longer exists.
         *
         * The held-key stack is deliberately NOT cleared here: a pointer
         * release calls this too, and the player's fingers are still on the
         * keyboard. Only a genuine reset - panic, activate, All Notes Off -
         * may forget which keys are down. */
        fLastKeyboardNote = -1;

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
        /* Carry the originating note across the rebuild, or the key holding
         * this chord could no longer release it. The octave comes from
         * fGlideTargetOct, since a glide may have crossed octaves. */
        const int     note   = g->midiNote;
        uint8_t       chans[kMaxGroupNotes];
        const int     nchan  = g->count;
        for (int i = 0; i < nchan; ++i)
            chans[i] = g->chan[i];

        stopGroup(0, g);
        startGroup(0, source, fGlideTargetRoot, fGlideTargetType,
                   fGlideTargetRing, vel, /* retriggerDuplicates */ false,
                   fGlideTargetOct, note);

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
        const ChordType type = chordTypeForRing(r, position);

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
                    startGroup(0, source, root, type, r, fGestureVelocity, true,
                               fOctave);
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
                           /* retriggerDuplicates */ fGlideMode == kGlideOff,
                           fOctave);
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

                /* See canGlideBetween() for why a bend is or is not enough. */
                const bool canGlide =
                    canGlideBetween(g, type, root, r, g->octave);

                if (! canGlide) {
                    /*
                     * Retrigger cleanly (spec 6.2 step 5). Shared pitches are
                     * resent so the new chord attacks properly. Keep the
                     * group's own octave: a move must not relocate a chord the
                     * keyboard placed in a particular octave.
                     *
                     * Unless the previous chord only just started. Two
                     * triggers within the merge window are one gesture - a
                     * drag crossing a cell boundary, or two fingers landing
                     * together - and stopping the first would clip a note a
                     * few milliseconds old. Inside the window the new chord
                     * JOINS the old rather than replacing it, and the
                     * instrument sorts out the overlap.
                     */
                    const uint8_t vel  = g->velocity;
                    const int     oct  = g->octave;
                    const int     note = g->midiNote;

                    if (! withinMergeWindow(g)) {
                        stopGroup(0, g);
                    } else {
                        /* Left sounding, so its source must stop naming this
                         * drag or the next move would try to move it again. */
                        g->source = kMergedSource;
                    }

                    startGroup(0, source, root, type, r, vel, true, oct, note);
                    fDragSource = source;
                    break;
                }

                fGlideTargetSemis = shortestSemitoneDelta(g->root, root);
                fGlideTargetRoot  = root;
                fGlideTargetType  = type;
                fGlideTargetRing  = r;
                /* A pointer drag stays in the octave the group already has. */
                fGlideTargetOct   = g->octave;
                fGlideSource      = g->source;
                fGlideElapsed     = 0;
                fGlideDuration    = static_cast<uint32_t>(
                    fGlideTimeMs * fSampleRate / 1000.0);

                if (fGlideMode == kGlideMpe) {
                    /* Work out where each voice must land, pairing by index. A
                     * voice with no counterpart (the chords differ in size) stays
                     * where it is and is resolved by the snap at the end. */
                    /* Same builder startGroup uses, so the ramp heads exactly
                     * where the snap will land. */
                    uint8_t want[kMaxGroupNotes];
                    const int n = buildCellChord(root, type, r, g->octave, want);

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
                    const uint8_t vel  = g->velocity;
                    const int     oct  = g->octave;
                    const int     note = g->midiNote;
                    stopGroup(0, g);
                    startGroup(0, source, root, type, r, vel, false, oct, note);
                    fDragSource = source;
                }
                break;
            }

            case kGestureRetrigger: {
                /*
                 * Rebuild whatever the pointer is holding, with the settings
                 * as they are now. Every group is stopped rather than just the
                 * drag's, because a latched chord on the same cell must not be
                 * left sounding with the old voicing beside the new one.
                 */
                if (fGlideActive) {
                    fGlideActive = false;
                    zeroAllBends(0);
                }

                stopAllGroups(0);

                fGestureVelocity = pickVelocity();
                startGroup(0, source, root, type, r, fGestureVelocity,
                           /* retriggerDuplicates */ true, fOctave);
                fDragSource = source;

                fNoteOffCountdown = fHoldToSustain
                    ? 0
                    : static_cast<int32_t>(fNoteLengthMs * fSampleRate / 1000.0);
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

    /*
     * Move sounding pointer-triggered chords to the current octave.
     *
     * An octave shift moves every voice by exactly twelve semitones, so the
     * chord shape is unchanged - which is precisely the condition spec 6.1
     * requires for a single uniform pitch bend. So the slider glides for free
     * in ordinary glide mode, with no MPE needed.
     *
     * Keyboard groups are deliberately left alone: a played note's octave is
     * determined by the key that was struck, and having the slider drag it
     * out from under the player's hand would be wrong.
     */
    void retuneToOctave(uint32_t frame)
    {
        VoiceGroup* g = nullptr;
        for (int i = 0; i < kMaxGroups; ++i) {
            if (fGroup[i].active && fGroup[i].midiNote < 0) {
                g = &fGroup[i];
                break;
            }
        }
        if (g == nullptr || g->octave == fOctave)
            return;

        const int delta = (fOctave - g->octave) * 12;

        /* Glide off, or a glide already running: move immediately rather than
         * queueing a second ramp on top of the first. */
        if (fGlideMode == kGlideOff || fGlideActive) {
            const uint8_t vel  = g->velocity;
            const int     src  = g->source;
            const int     root = g->root;
            const ChordType ty = g->type;
            const Ring      rg = g->ring;

            if (fGlideActive) {
                fGlideActive = false;
                zeroAllBends(frame);
            }

            stopGroup(frame, g);
            startGroup(frame, src, root, ty, rg, vel, false, fOctave);
            return;
        }

        fGlideTargetSemis = delta;
        fGlideTargetRoot  = g->root;
        fGlideTargetType  = g->type;
        fGlideTargetRing  = g->ring;
        fGlideTargetOct   = fOctave;
        fGlideSource      = g->source;
        fGlideElapsed     = 0;
        fGlideDuration    = static_cast<uint32_t>(
            fGlideTimeMs * fSampleRate / 1000.0);

        if (fGlideMode == kGlideMpe) {
            /* Every voice travels the same octave, so the targets are simply
             * the current notes shifted. */
            for (int i = 0; i < g->count; ++i) {
                const int t = g->note[i] + delta;
                g->target[i] = (t >= 0 && t <= 127)
                    ? static_cast<uint8_t>(t) : g->note[i];
            }
        }

        fGlideActive = true;
    }

    /* ---- MIDI input: the keyboard plays the wheel -------------------------
     *
     * A controller note selects a cell by DEGREE, so the same physical key
     * plays I in whatever key is selected and the keyboard transposes with the
     * wheel. The octave played is the octave sounded - the keyboard behaves
     * like an instrument, not like a bank of switches.
     *
     * Mapped notes are CONSUMED rather than passed through: forwarding them
     * would sound the raw note alongside the chord it triggered. Everything
     * else is forwarded untouched, so the plugin can still sit in a chain.
     */
    void handleMidiIn(const MidiEvent& ev)
    {
        if (ev.size < 2) {
            writeMidiEvent(ev);
            return;
        }

        /*
         * The sequencer screen takes the keyboard out of circuit entirely.
         *
         * There, chords come from the grid and the transport; a key press
         * would sound a chord the grid did not ask for and, worse, would fight
         * the sequencer for the same voices. Nothing is passed through either:
         * the plugin is a generator, and forwarding raw notes would put the
         * played key into the output alongside the sequenced chord.
         *
         * The pedal is included. Its actions - glide, legato, panic - all act
         * on hand playing that cannot happen here.
         */
        if (fUiScreen.load(std::memory_order_acquire) == kUiScreenProgressions)
            return;

        const uint8_t status = ev.data[0] & 0xF0;
        const uint8_t d1     = ev.data[1];
        const uint8_t d2     = (ev.size > 2) ? ev.data[2] : 0;

        /* Sustain pedal (CC 64). Held, it defers releases instead of blocking
         * them: notes keep starting and stopping as played, but nothing is
         * actually silenced until the pedal comes up. That is what makes the
         * playing legato rather than merely latched. */
        if (status == 0xB0 && d1 == 64) {
            const bool down = (d2 >= 64);
            const bool wasDown = fPedalDown;
            fPedalDown = down;

            switch (fPedalAction) {
                case kPedalSustain:
                    /* Defer releases while held, rather than blocking them -
                     * that is what makes the playing legato, not latched. */
                    if (wasDown && ! down)
                        releaseDeferred(ev.frame);
                    /* Downstream instruments may want the pedal too. */
                    writeMidiEvent(ev);
                    break;

                case kPedalGlide:
                    /* Momentary: glide for as long as the foot is down. */
                    if (down && ! wasDown) {
                        fGlideWasOn = (fGlideMode != kGlideOff)
                            ? fGlideMode : fGlideWasOn;
                        fGlideMode = fGlideWasOn;
                        if (fGlideMode == kGlideMpe)
                            fNeedsRpn = true;
                    } else if (! down && wasDown) {
                        fGlideMode = kGlideOff;
                    }
                    fLastChordCount = 0;
                    fSettingsEcho.store(true, std::memory_order_release);
                    break;

                case kPedalLatch:
                    if (down && ! wasDown) {
                        fLatchEnabled = ! fLatchEnabled;
                        if (! fLatchEnabled)
                            stopAllGroups(ev.frame);
                        fSettingsEcho.store(true, std::memory_order_release);
                    }
                    break;

                case kPedalSingle:
                    if (down && ! wasDown) {
                        fSingleNotes = ! fSingleNotes;
                        fLastChordCount = 0;
                        fSettingsEcho.store(true, std::memory_order_release);
                    }
                    break;

                case kPedalPanic:
                    if (down && ! wasDown)
                        fPanic.store(true, std::memory_order_release);
                    break;

                case kPedalNone:
                default:
                    break;
            }
            return;
        }

        /* All Notes Off / All Sound Off: honour them ourselves as well, or our
         * groups would outlive the host's own silence request. */
        if (status == 0xB0 && (d1 == 120 || d1 == 123)) {
            fPedalDown    = false;
            fHeldKeyCount = 0;
            stopAllGroups(ev.frame);
            zeroAllBends(ev.frame);
            fGlideActive = false;
            writeMidiEvent(ev);
            return;
        }

        const bool isNoteOn  = (status == 0x90 && d2 > 0);
        const bool isNoteOff = (status == 0x80) || (status == 0x90 && d2 == 0);

        if (! isNoteOn && ! isNoteOff) {
            writeMidiEvent(ev);
            return;
        }

        /*
         * Every key on the controller belongs to this plugin now: white keys
         * play degrees, black keys are real-time controls, and anything bound
         * to nothing is SILENT rather than forwarded. Passing an unbound key
         * through would sound the raw note underneath the chords, which is
         * exactly what the black keys used to do before they had jobs.
         */
        const int pc = ((d1 % 12) + 12) % 12;
        const KeyMapEntry& e = fKeyMap[pc];

        switch (e.action) {
            case kKeyDegree: {
                int  position;
                Ring ring;
                if (! cellForMidiNote(fKeyMap, d1,
                                      fSelectedKey.load(std::memory_order_acquire),
                                      position, ring))
                    return;

                if (isNoteOn)
                    noteOnCell(ev.frame, d1, position, ring, d2);
                else
                    noteOffCell(ev.frame, d1);
                return;
            }

            /*
             * Controls act on press only. They change settings for the NEXT
             * chord and never rewrite one already sounding - spec section 5's
             * rule, which is what keeps a control press from editing MIDI that
             * has already gone out.
             */
            case kKeyExtension:
                if (isNoteOn) {
                    const Extension x = static_cast<Extension>(
                        ((e.value % kExtCount) + kExtCount) % kExtCount);
                    for (int r = 0; r < kRingCount; ++r)
                        fRingExtension[r] = x;
                    fSettingsEcho.store(true, std::memory_order_release);
                }
                return;

            case kKeyGlideToggle:
                if (isNoteOn) {
                    /* Remember what "on" meant, so a player using MPE gets MPE
                     * back rather than being demoted to plain glide. */
                    if (fGlideMode == kGlideOff) {
                        fGlideMode = fGlideWasOn;
                        if (fGlideMode == kGlideMpe)
                            fNeedsRpn = true;
                    } else {
                        fGlideWasOn = fGlideMode;
                        fGlideMode  = kGlideOff;
                    }
                    fLastChordCount = 0;
                    fSettingsEcho.store(true, std::memory_order_release);
                }
                return;

            case kKeyLatchToggle:
                if (isNoteOn) {
                    fLatchEnabled = ! fLatchEnabled;
                    /* Leaving latch with chords held would strand them. */
                    if (! fLatchEnabled)
                        stopAllGroups(ev.frame);
                    fSettingsEcho.store(true, std::memory_order_release);
                }
                return;

            case kKeySingleToggle:
                if (isNoteOn) {
                    fSingleNotes = ! fSingleNotes;
                    fLastChordCount = 0;
                    fSettingsEcho.store(true, std::memory_order_release);
                }
                return;

            case kKeyPanic:
                if (isNoteOn)
                    fPanic.store(true, std::memory_order_release);
                return;

            case kKeyNone:
            default:
                /* Bound to nothing: swallow it. Silence is the point. */
                return;
        }
    }

    /*
     * A played note starts its own group. Unlike a pointer gesture there is no
     * drag, but a second note while the first is held is still an overlap - and
     * that is how chords stack up under the fingers.
     */
    void noteOnCell(uint32_t frame, int midiNote, int position, Ring ring,
                    uint8_t velocity)
    {
        pushHeldKey(midiNote);

        /* Retriggering the same key: retire the old group first, or its notes
         * would be orphaned by the new one taking the same identity. */
        VoiceGroup* existing = findGroupByNote(midiNote);
        if (existing != nullptr)
            stopGroup(frame, existing);

        const int       root = rootForPosition(position, ring);
        const ChordType type = chordTypeForRing(ring, position);
        const int       oct  = octaveForMidiNote(midiNote);
        const int    source  = (static_cast<int>(ring) << 8) | position;

        /*
         * Velocity comes from the key, not from the velocity setting. A played
         * note carries the performer's intent and overriding it would make the
         * keyboard feel dead; the randomise amount still applies as a spread
         * around what was played.
         */
        uint8_t vel = velocity;
        if (fVelocityRandom > 0) {
            const int spread = fVelocityRandom * 2 + 1;
            int r = vel - fVelocityRandom + (std::rand() % spread);
            if (r < 1)   r = 1;
            if (r > 127) r = 127;
            vel = static_cast<uint8_t>(r);
        }

        /*
         * Glide between overlapping played notes, so a legato line moves rather
         * than restarts. The most recent keyboard group is the one to move,
         * which mirrors how a monosynth's glide follows the last key down.
         */
        VoiceGroup* from = fLastKeyboardNote >= 0
            ? findGroupByNote(fLastKeyboardNote) : nullptr;

        if (from != nullptr && from->midiNote != midiNote &&
            fGlideMode != kGlideOff) {

            const bool canGlide = canGlideBetween(from, type, root, ring, oct);

            /* Octave changes are exactly what the vertical slider produces when
             * dragged, and a glide across them is the instrumental gesture the
             * feature exists for - so distance is measured in real semitones,
             * not just pitch class. */
            if (canGlide) {
                const int semis = (root - from->root) +
                                  (oct - from->octave) * 12;

                fGlideTargetSemis = semis;
                fGlideTargetRoot  = root;
                fGlideTargetType  = type;
                fGlideTargetRing  = ring;
                fGlideTargetOct   = oct;
                fGlideSource      = from->source;
                fGlideElapsed     = 0;
                fGlideDuration    = static_cast<uint32_t>(
                    fGlideTimeMs * fSampleRate / 1000.0);

                /* The gliding group becomes this note's group: the key that is
                 * now down owns what is sounding, so its release ends it. */
                from->midiNote = midiNote;

                if (fGlideMode == kGlideMpe) {
                    uint8_t want[kMaxGroupNotes];
                    const int n = buildCellChord(root, type, ring, oct, want);
                    for (int i = 0; i < from->count; ++i)
                        from->target[i] = (i < n) ? want[i] : from->note[i];

                    bool moves = false;
                    for (int i = 0; i < from->count && ! moves; ++i)
                        moves = (from->target[i] != from->note[i]);
                    fGlideActive = moves;
                } else {
                    fGlideActive = (semis != 0);
                }

                if (fGlideActive) {
                    fLastKeyboardNote = midiNote;
                    return;
                }

                /* Nothing to travel: fall through and start normally. */
                from->midiNote = fLastKeyboardNote;
            }
        }

        startGroup(frame, source, root, type, ring, vel,
                   /* retriggerDuplicates */ fGlideMode == kGlideOff,
                   oct, midiNote);

        fLastKeyboardNote = midiNote;
    }

    /* Release the group a played note started - deferring while the pedal is
     * down, which is what turns held notes into a legato phrase. */
    void noteOffCell(uint32_t frame, int midiNote)
    {
        removeHeldKey(midiNote);

        VoiceGroup* g = findGroupByNote(midiNote);
        if (g == nullptr) {
            /* This key handed its group to a later one during a glide. Nothing
             * of ours to stop, but the sounding group may now belong to a key
             * that is no longer down - resolved below by the fallback. */
            if (fLastKeyboardNote == midiNote)
                fLastKeyboardNote = topHeldKey();
            return;
        }

        /*
         * A glide gave this group to the key that is lifting, but an earlier key
         * may still be held. Hand the group back to it rather than silencing it:
         * the player still has a finger down, so the phrase continues. This is
         * what makes overlapping legato behave like an instrument instead of
         * cutting out whenever the newer of two keys is released.
         */
        const int fallback = topHeldKey();
        if (! fPedalDown && fallback >= 0 && fallback != midiNote &&
            findGroupByNote(fallback) == nullptr) {
            g->midiNote       = fallback;
            fLastKeyboardNote = fallback;
            return;
        }

        if (fPedalDown) {
            g->deferred = true;
        } else {
            if (fGlideActive && fGlideSource == g->source) {
                fGlideActive = false;
                zeroAllBends(frame);
            }
            stopGroup(frame, g);
        }

        if (fLastKeyboardNote == midiNote)
            fLastKeyboardNote = topHeldKey();
    }

    /* Pedal up: everything whose key was already released now stops. */
    void releaseDeferred(uint32_t frame)
    {
        for (int i = 0; i < kMaxGroups; ++i) {
            if (fGroup[i].active && fGroup[i].deferred) {
                if (fGlideActive && fGlideSource == fGroup[i].source) {
                    fGlideActive = false;
                    zeroAllBends(frame);
                }
                stopGroup(frame, &fGroup[i]);
            }
        }
    }

    /*
     * Is voice leading in force right now? Always, since plain glide went.
     *
     * It used to be suspended during single-bend glide, because leading
     * re-inverts a chord and a re-inversion moves voices by DIFFERENT
     * intervals - which one channel-wide pitch bend cannot express. With that
     * mode gone the conflict is gone: glide off retriggers anyway, and MPE
     * gives every voice its own channel, so any voicing is reachable.
     *
     * Kept as a function rather than inlined at its call sites because it
     * names the question, and because the answer was genuinely conditional
     * until recently.
     */
    bool leadingAppliesNow() const { return true; }

    /*
     * The one definition of what notes a cell produces.
     *
     * Build, lead, voice - in that order, every time. Four places need this
     * answer (starting a group, the two MPE glide-target calculations, and the
     * glide eligibility test) and if any of them assembled the chord slightly
     * differently the glide would ramp toward pitches the snap never lands on.
     * So they all call here instead.
     */
    int buildCellChord(int root, ChordType type, Ring ring, int octave,
                       uint8_t* out) const
    {
        const int n = buildChord(root, type, octave * 12, out, kMaxChordTones);

        /*
         * Build, then voice. That is the whole chain now.
         *
         * It used to be build, LEAD, voice, then apply a separate bass note -
         * four steps, of which three were arguing about the same thing.
         * Voice leading chose an inversion automatically, the voicing list
         * offered inversions of its own, and the bass note forced a third
         * answer on top, so the code had to suppress two of them to let the
         * third through.
         *
         * Voicing is now the single axis for how a chord is arranged, set per
         * cell, and nothing overrides it.
         *
         * Single-note mode skips it: there is nothing to rearrange, and a
         * doubling voicing would quietly make it two notes.
         */
        if (fSingleNotes)
            return n;

        return applyVoicing(out, n, fRingVoicing[ring], kMaxGroupNotes);
    }

    /*
     * ---- the sequencer -----------------------------------------------------
     *
     * Drive the progression from the host's musical position.
     *
     * Position, not elapsed frames. DPF documents TimePosition::frame as not
     * necessarily monotonic - a host is free to loop, relocate or scrub - so
     * counting samples would drift out of step with the bar lines the moment
     * anyone touched the transport. Reading bar/beat/tick instead means the
     * sequencer is wherever the host says it is, and a loop or a jump lands on
     * the right chord with no resynchronising.
     *
     * When the host offers no BBT at all (bbt.valid false, which some hosts
     * report while stopped) the sequencer simply does not advance. Inventing a
     * tempo would put the plugin in a different place from everything else in
     * the session.
     *
     * Takes no frame count for that reason: nothing here is measured in
     * samples. A block is only an opportunity to ask the host where it is.
     */
    void runSequencer()
    {
        /* Take a pending grid while nothing is mid-beat. Done first so a grid
         * edited during playback takes effect on the next trigger rather than
         * a bar later. */
        if (fProgDirty.exchange(false, std::memory_order_acquire))
            fProg = fPendingProg;

        /* A stop must silence the sequencer's chord even under legato, where
         * no later trigger would ever replace it. */
        if (fProgStopping.exchange(false, std::memory_order_acquire)) {
            stopSequencerGroup(0);
            fProgLastBeat = -1;
            fCells.clearPlayhead();
        }

        if (! fProgRunning.load(std::memory_order_acquire))
            return;

        const TimePosition& t = getTimePosition();

        /* Following the host's transport: a stopped transport holds the
         * sequence rather than running it free. */
        if (! t.playing || ! t.bbt.valid) {
            if (fProgLastBeat >= 0) {
                stopSequencerGroup(0);
                fProgLastBeat = -1;
                fCells.clearPlayhead();
            }
            return;
        }

        /*
         * The running beat count, from the top of the timeline.
         *
         * bar and beat are 1-based in DPF, hence the subtractions. The tick
         * fraction is carried so that a block starting part-way through a beat
         * is placed correctly rather than rounded to the beat it began in.
         */
        const double ticksPerBeat = (t.bbt.ticksPerBeat > 0.0)
                                  ? t.bbt.ticksPerBeat : 1920.0;
        const double beatsPerBar  = (t.bbt.beatsPerBar > 0.0f)
                                  ? static_cast<double>(t.bbt.beatsPerBar) : 4.0;

        const double beatNow =
            (static_cast<double>(t.bbt.bar) - 1.0) * beatsPerBar +
            (static_cast<double>(t.bbt.beat) - 1.0) +
            (t.bbt.tick / ticksPerBeat);

        const long long beat = static_cast<long long>(std::floor(beatNow));

        /* Nothing to do until the beat changes. Comparing beats rather than
         * timing from frames is what makes a repeated run() inside one beat
         * harmless, and a skipped beat - from a jump - land correctly. */
        if (beat == fProgLastBeat)
            return;

        int section = 0, step = 0;
        if (! fProg.locate(beat, section, step)) {
            /* An empty progression: stop rather than hold a stale chord. */
            stopSequencerGroup(0);
            fCells.clearPlayhead();
            fProgLastBeat = beat;
            return;
        }

        fProgLastBeat = beat;
        fCells.setPlayhead(section, step);

        const ProgCell& cell = fProg.section[section].cell[step];

        /*
         * A rest.
         *
         * Under legato it does NOT cut the chord - legato means a trigger
         * stays live until the NEXT trigger, and a rest is the absence of one.
         * Without legato the beat is where the chord ends, so it stops.
         */
        if (! cell.filled) {
            if (! fProgLegato)
                stopSequencerGroup(0);
            return;
        }

        /* Resolve the degree in whatever key is selected, through the same
         * call the keyboard and Slide Mode make - so a sequenced chord is
         * identical to the one you would get by playing that cell. */
        int  position = 0;
        Ring ring     = kRingKey;
        cellForDegree(cell.degree,
                      fSelectedKey.load(std::memory_order_acquire),
                      position, ring);

        const int       root = rootForPosition(position, ring);
        const ChordType type = chordTypeFor(ring, cell.ext, cell.degree);

        /* The previous chord goes first: two chords sounding at once would be
         * a harmony the grid never asked for. */
        stopSequencerGroup(0);

        /* The cell's octave is an OFFSET from the instrument's own, so moving
         * the plugin up or down carries the progression with it and a chord
         * written low stays low relative to its neighbours. */
        startGroup(0, kProgSource, root, type, ring, pickVelocity(), true,
                   fOctave + cell.octave);
    }

    /* Silence the sequencer's group, leaving hand-played ones alone. */
    void stopSequencerGroup(uint32_t frame)
    {
        for (int i = 0; i < kMaxGroups; ++i)
            if (fGroup[i].active && fGroup[i].source == kProgSource)
                stopGroup(frame, &fGroup[i]);
    }

    /*
     * Can a move from one chord to another be carried by a glide?
     *
     *   off  never - a retrigger is the behaviour.
     *   mpe  always. Every voice has its own channel and bends independently,
     *        so any chord reaches any other regardless of shape or inversion.
     *
     * This used to carry a third case, and most of its length: a single
     * channel-wide bend moves every voice by the same interval, so it could
     * only express a move that was itself uniform, and the function had to
     * build the target chord and verify that every voice moved by the same
     * delta. That mode is gone, and the check with it.
     */
    bool canGlideBetween(const VoiceGroup* from, ChordType, int, Ring, int) const
    {
        return fGlideMode == kGlideMpe && from != nullptr;
    }

    /*
     * Did this group start recently enough to treat the next trigger as part
     * of the same gesture?
     *
     * Two chords a few milliseconds apart are one musical event - a drag
     * crossing a cell boundary, two fingers landing together - and stopping
     * the first to start the second clips a note that has barely sounded.
     * Inside the window both are left to sound and the instrument resolves
     * the overlap.
     */
    bool withinMergeWindow(const VoiceGroup* g) const
    {
        if (g == nullptr || fMergeWindowMs <= 0)
            return false;

        const uint32_t windowFrames = static_cast<uint32_t>(
            fMergeWindowMs * fSampleRate / 1000.0);
        return g->age < windowFrames;
    }

    /* A group left sounding by a merge. It no longer answers to the drag that
     * created it - a later move must not try to move it again - but it is
     * still a live group and is released with everything else. */
    static constexpr int kMergedSource = -2;

    /*
     * A ring's quality is fixed; the per-ring extension builds on it.
     *
     * Single-note mode bypasses chord generation entirely and sounds only the
     * root, turning the wheel into a note selector - useful for basslines and
     * melodies that should follow the same key-relative layout. It is applied
     * here because this is the one place every trigger path passes through:
     * pointer, keyboard and glide all ask this question, so the override
     * cannot be missed by one of them.
     */
    /*
     * The position is needed, not just the ring: V takes a DOMINANT seventh
     * while I and IV take major ones, and all three are major triads on the
     * same ring. Without it, G in the key of C came out as Gmaj7 and sounded
     * an F# that is not in the key.
     */
    /*
     * The position is needed, not just the ring: V takes a DOMINANT seventh
     * while I and IV take major ones, and all three are major triads on the
     * same ring.
     *
     * Falls back to the plain triad where the extension is not in the key, so
     * the DSP always has something to sound. The editor's job is to stop such
     * a combination being chosen at all; this is the backstop for a state
     * restored from an older session, or a key changed under a held setting.
     */
    ChordType chordTypeForRing(Ring ring, int position) const
    {
        if (fSingleNotes)
            return kChordSingleNote;

        const int key = fSelectedKey.load(std::memory_order_acquire);
        return extendChordOrTriad(defaultChordForRing(ring),
                                  fRingExtension[ring],
                                  cellIsDominant(position, ring, key),
                                  semitoneForCell(position, ring, key));
    }

    /* As above, but with the extension given rather than taken from the ring.
     * The sequencer carries an extension per CELL - a ii-V-I wants sevenths on
     * the ii and V and a plain I - so it cannot use the ring-wide setting. */
    ChordType chordTypeFor(Ring ring, Extension ext, Degree degree) const
    {
        if (fSingleNotes)
            return kChordSingleNote;
        return extendChordOrTriad(defaultChordForRing(ring), ext,
                                  degreeIsDominant(degree),
                                  semitoneForDegree(degree));
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
    /* Bypass chord generation and sound the root alone. */
    bool      fSingleNotes    = false;

    /*
     * ---- the sequencer ---------------------------------------------------
     *
     * fProg is owned by the audio thread. The UI writes fPendingProg from
     * setState and raises fProgDirty; the audio thread takes the copy at a
     * point where no chord is mid-flight. Double-buffering rather than a lock
     * because the audio thread must never wait on the UI.
     */
    Progression       fProg;
    Progression       fPendingProg;

    /* The grid as last set, for getState(). Written and read on the UI thread,
     * so saving a project never races the audio thread's copy. */
    Progression       fSavedProg;
    std::atomic<bool> fProgDirty    { false };
    std::atomic<bool> fProgRunning  { false };
    std::atomic<bool> fProgStopping { false };

    /* Legato: a chord rings until the next one replaces it, rather than
     * stopping at the end of its beat. */
    bool fProgLegato = true;

    /* Where saved progressions and preferences will live. Held so the choice
     * survives a session; nothing reads it until there is a store to open. */
    StorageMode fStorageMode = kStorageUser;

    /*
     * ---- MIDI output health -------------------------------------------------
     *
     * The host's output buffer can fill mid-chord. DPF says nothing more may
     * be written until the next run(), so this latches for the rest of the
     * block and clears at the top of the next one.
     */
    bool     fOutputFull    = false;

    /* Messages the host would not take. Shown in the UI, because a non-zero
     * count is the one number that explains a chord arriving in pieces. */
    uint32_t fDroppedEvents = 0;

    /* A note-off was refused, so something downstream may still be sounding.
     * Cleared by an all-notes-off at the top of the next block. */
    bool     fStuckNotes    = false;

    /* Which channel's bend range is announced next. The announcement is spread
     * over blocks so it cannot flood the output buffer - see
     * sendBendRangeRpn(). Zero means the base channel. */
    int      fRpnChannel    = 0;

    /* The diagnostic log. Written by the audio thread, drained to a file by
     * the UI. Off by default: it is for chasing a fault, not for running. */
    LogRing           fLog;
    std::atomic<bool> fLogEnabled { false };

    /* The beat last triggered, so a beat fires once however many times run()
     * is called inside it. -1 means nothing has played yet. */
    long long fProgLastBeat = -1;

    /* The sequencer's own voice group, kept apart from the wheel's so that
     * playing along by hand does not cut the sequence. */
    static constexpr int kProgSource = 0xF000;

    /*
     * Which screen the editor is showing.
     *
     * The DSP is otherwise screen-agnostic by design - a strip and a wedge
     * send the same gesture - but the sequencer screen genuinely changes what
     * incoming MIDI means, so this one fact has to cross. Only the value the
     * gate needs is defined, rather than mirroring the UI's whole enum.
     */
    static constexpr int kUiScreenProgressions = 3;
    std::atomic<int32_t> fUiScreen { 1 };   /* circle, as the UI starts */

    /* The chord most recently started, as the reference the next one leads
     * from. Kept after it stops, so a gap between chords still leads smoothly
     * rather than resetting to root position. */
    uint8_t   fLastChord[kMaxGroupNotes] = {0};
    int       fLastChordCount = 0;
    GlideMode fGlideMode      = kGlideMpe;
    int       fGlideTimeMs    = 120;
    /* How close two triggers must be to sound together rather than one
     * replacing the other. See withinMergeWindow(). */
    int       fMergeWindowMs  = kMergeWindowMsDefault;
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

    /* Which cells are sounding, for the UI highlight. Separate from the ring
     * because the highlight must work whether or not the monitor is open. */
    ActiveCells fCells;

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
    /* Octave to resolve at. A keyboard glide can cross octaves, so the snap at
     * the end must know where it is landing rather than assuming it stayed. */
    int       fGlideTargetOct   = 4;

    /* ---- MIDI input state -------------------------------------------------- */

    /* Which key the wheel is centred on. Written by the UI thread through
     * setState, read by the audio thread to resolve an incoming note. */
    std::atomic<int> fSelectedKey { 0 };

    /* Sustain pedal (CC 64). While down, releases are deferred rather than
     * ignored, so the phrase sustains without the notes losing their identity. */
    bool fPedalDown = false;

    /* What the pedal does, and what each key does. Both are editable from the
     * Keyboard Setup tab; these are the factory defaults. */
    PedalAction fPedalAction = kPedalSustain;
    KeyMapEntry fKeyMap[12];

    /* Glide's last on-state, so a toggle restores MPE rather than demoting a
     * player to plain glide. */
    GlideMode fGlideWasOn = kGlideMpe;

    /* Set when a key or pedal changed a setting, so the UI can re-read it and
     * keep its buttons honest. */
    std::atomic<bool> fSettingsEcho { false };

    /*
     * Keys physically down, in the order they were pressed.
     *
     * A glide hands one group from key to key, so the group belongs to whichever
     * key claimed it last. That alone is not enough: releasing that key while an
     * earlier one is still held would end the sound with a finger still on the
     * keyboard. The stack lets the group fall back to the key underneath, which
     * is how a legato monosynth behaves.
     */
    static constexpr int kMaxHeldKeys = 16;
    int fHeldKey[kMaxHeldKeys] = {0};
    int fHeldKeyCount = 0;

    void pushHeldKey(int note)
    {
        for (int i = 0; i < fHeldKeyCount; ++i)
            if (fHeldKey[i] == note)
                return;
        if (fHeldKeyCount < kMaxHeldKeys)
            fHeldKey[fHeldKeyCount++] = note;
    }

    void removeHeldKey(int note)
    {
        for (int i = 0; i < fHeldKeyCount; ++i) {
            if (fHeldKey[i] == note) {
                for (int j = i; j < fHeldKeyCount - 1; ++j)
                    fHeldKey[j] = fHeldKey[j + 1];
                --fHeldKeyCount;
                return;
            }
        }
    }

    /* Most recently pressed key still down, or -1. */
    int topHeldKey() const
    {
        return fHeldKeyCount > 0 ? fHeldKey[fHeldKeyCount - 1] : -1;
    }

    /* The most recent played note still sounding, for glide between
     * overlapping keys. -1 when none. */
    int  fLastKeyboardNote = -1;

    /* Set by the UI when the octave slider moves, consumed by run() so held
     * chords travel with it. */
    std::atomic<bool> fOctaveMoved { false };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FortyFifthPlugin)
};

Plugin* createPlugin()
{
    return new FortyFifthPlugin();
}

END_NAMESPACE_DISTRHO
