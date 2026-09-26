/*
 * Run the REAL plugin, with no host.
 *
 * Every suite before this one transcribed the logic it tested - a model of the
 * held-key stack, of the refcounts, of the groups. Those catch arithmetic and
 * they document intent, but they share one fatal weakness: when the model and
 * the plugin disagree, the MODEL passes. That is not hypothetical.
 * dev/test-stuck.cpp proves the stuck-note leak against the refcount rules, but
 * its group model cannot reproduce the leak at all, because the model
 * self-heals in a way the plugin does not.
 *
 * So the honest answer to "why can't we have automated coverage?" is that there
 * was no reason. The obstacle was assumed rather than measured. Measured, the
 * plugin needs nothing from DPF but a handful of base-class method BODIES -
 * the ones DPF keeps in its own implementation files, which is the only reason
 * a plugin normally cannot be linked on its own. The declarations, and every
 * type that matters (MidiEvent, State, String, TimePosition), come from DPF's
 * real headers here, so nothing can drift from what ships.
 *
 * WHAT THIS BUYS. The behaviours listed as unguarded in
 * docs/glide-refactor-plan.md section 5 - the three glide modes, all four glide
 * callers, snap-and-reset, ownership across the snap, refused-note-off recovery,
 * latch, pedal, panic and the merge window - are all reachable, because they are
 * all observable as emitted MIDI. None of them needed a host. They needed a way
 * to look at the output.
 *
 * WHAT IT DOES NOT BUY. This is not a host. There is no audio thread, no real
 * block scheduling, and the timing is exact where a DAW's is jittery. It proves
 * the plugin's logic, not its real-time safety or its behaviour in any
 * particular DAW - so a green suite here is necessary and not sufficient, and
 * the plan still calls for a listening check.
 *
 * Include this BEFORE the plugin: the base-class bodies have to be defined
 * ahead of the class that inherits them being instantiated.
 */
#ifndef FORTYFIFTH_HARNESS_HPP
#define FORTYFIFTH_HARNESS_HPP

#include "DistrhoPlugin.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/*
 * The test host, forward-declared so the plugin can befriend it under
 * FORTYFIFTH_TESTING. It lives here rather than in the test file so the friend
 * declaration has something to name whichever suite includes this header.
 */
class Host;

/* ------------------------------------------------------------------------ */
/* What the instrument would receive.                                        */

struct Sent {
    uint32_t frame;
    uint8_t  status;
    uint8_t  d1;
    uint8_t  d2;

    uint8_t  channel() const { return status & 0x0F; }
    uint8_t  kind()    const { return status & 0xF0; }
    bool     isNoteOn()  const { return kind() == 0x90 && d2 > 0; }
    bool     isNoteOff() const { return kind() == 0x80 || (kind() == 0x90 && d2 == 0); }
    bool     isBend()    const { return kind() == 0xE0; }
    bool     isCC()      const { return kind() == 0xB0; }
};

/* The harness's own state, reachable from the tests. */
namespace harness {

extern std::vector<Sent> sent;
extern int    refuseAfter;   /* -1: accept everything, else refuse past N */
extern int    refused;
extern double sampleRate;

inline void reset()
{
    sent.clear();
    refused     = 0;
    refuseAfter = -1;
}

/* Which pitches the instrument believes are down, by replaying the captured
 * stream. This is the ground truth a listener would hear: a note is sounding if
 * its last relevant message was a note-on.
 *
 * Per (channel, note), because in MPE the same pitch legitimately sounds on
 * several channels at once and collapsing them would hide exactly the bug the
 * refcount is there to prevent.
 */
inline int soundingCount()
{
    bool down[16][128];
    std::memset(down, 0, sizeof down);

    for (size_t i = 0; i < sent.size(); ++i) {
        const Sent& s = sent[i];
        if (s.isNoteOn())        down[s.channel()][s.d1] = true;
        else if (s.isNoteOff())  down[s.channel()][s.d1] = false;
        else if (s.isCC() && (s.d1 == 123 || s.d1 == 120)) {
            /* All Notes Off / All Sound Off on that channel. */
            for (int n = 0; n < 128; ++n) down[s.channel()][n] = false;
        }
    }

    int n = 0;
    for (int c = 0; c < 16; ++c)
        for (int p = 0; p < 128; ++p)
            if (down[c][p]) ++n;
    return n;
}

inline int countOf(bool (Sent::*pred)() const)
{
    int n = 0;
    for (size_t i = 0; i < sent.size(); ++i)
        if ((sent[i].*pred)()) ++n;
    return n;
}

inline void dump(const char* label)
{
    std::printf("    [%s] %zu events:", label, sent.size());
    for (size_t i = 0; i < sent.size() && i < 24; ++i)
        std::printf(" %02X:%02X:%02X", sent[i].status, sent[i].d1, sent[i].d2);
    std::printf("%s\n", sent.size() > 24 ? " ..." : "");
}

} /* namespace harness */

/* ------------------------------------------------------------------------ */
/* The Plugin base-class bodies DPF keeps in its own implementation files.    */

START_NAMESPACE_DISTRHO

Plugin::Plugin(uint32_t, uint32_t, uint32_t) : pData(nullptr) {}
Plugin::~Plugin() {}

double Plugin::getSampleRate() const noexcept { return harness::sampleRate; }

/*
 * The one that matters. Every assertion in every harness test comes back to
 * what passed through here.
 *
 * `refuseAfter` reproduces a host whose event buffer has filled - the condition
 * behind the refused-note-off recovery path, which no transcribed model could
 * reach, and which the plugin handles by latching fOutputFull and retrying an
 * all-notes-off on the next block.
 */
bool Plugin::writeMidiEvent(const MidiEvent& ev) noexcept
{
    if (harness::refuseAfter >= 0 &&
        static_cast<int>(harness::sent.size()) >= harness::refuseAfter) {
        ++harness::refused;
        return false;
    }

    Sent s;
    s.frame  = ev.frame;
    s.status = ev.data[0];
    s.d1     = ev.data[1];
    s.d2     = ev.data[2];
    harness::sent.push_back(s);
    return true;
}

/* Defaults for the non-pure virtuals the plugin does not override. */
void   Plugin::initAudioPort(bool, uint32_t, AudioPort&) {}
void   Plugin::initParameter(uint32_t, Parameter&) {}
void   Plugin::initPortGroup(uint32_t, PortGroup&) {}
void   Plugin::initState(uint32_t, State&) {}
float  Plugin::getParameterValue(uint32_t) const { return 0.0f; }
void   Plugin::setParameterValue(uint32_t, float) {}
String Plugin::getState(const char*) const { return String(); }
void   Plugin::setState(const char*, const char*) {}
void   Plugin::bufferSizeChanged(uint32_t) {}
void   Plugin::sampleRateChanged(double) {}
void   Plugin::ioChanged(uint16_t, uint16_t) {}

/* The sequencer asks the host for tempo and playhead. A stopped transport is
 * the right default: no test here drives the sequencer from the clock. */
static TimePosition gTimePosition;
const TimePosition& Plugin::getTimePosition() const noexcept { return gTimePosition; }

END_NAMESPACE_DISTRHO

#endif /* FORTYFIFTH_HARNESS_HPP */
