/*
 * Keyboard scope, and the settings round trip.
 *
 * Three reported faults, all one root cause: the keyboard and the UI were two
 * sources of truth for the same state.
 *
 *   "pressing too many keys changes the key set when playing even though the
 *    circle doesn't get updated"
 *   "highlighted cells that shouldn't be played get played"
 *   keyboard mode breaking on screens where it had nothing to address
 *
 * The keyboard resolves a key to a DEGREE and a degree to a CELL ON THE WHEEL,
 * so the circle screen is the only one whose contents it can address. On the
 * others it sounded chords unrelated to what was on screen. And its chord-type
 * keys wrote the per-ring extension that the wheel's cells also drive, so a key
 * press silently rewrote the circle without the circle redrawing.
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "CircleTheory.hpp"

using namespace fortyfifth;

static int failures = 0;

static void ok(const char* what, bool cond, const char* detail)
{
    if (cond) {
        std::printf("  ok    %-46s %s\n", what, detail);
    } else {
        std::printf("  FAIL  %-46s %s\n", what, detail);
        ++failures;
    }
}

/* Mirrors the gate at the top of FortyFifthPlugin::handleMidiIn(). */
static constexpr int kScreenSetup = 0, kScreenCircle = 1,
                     kScreenSlide = 2, kScreenProg   = 3;

static bool keyboardPlays(int screen) { return screen == kScreenCircle; }

/* Mirrors ActiveCells::packSettings / readSettings. */
static uint32_t packSettings(uint32_t seq, int glide, bool latch, bool single)
{
    return ((seq & 0xFFFFu) << 16)
         | ((static_cast<uint32_t>(glide) & 0xFFu) << 8)
         | (latch ? 0x2u : 0u) | (single ? 0x1u : 0u);
}

static bool readSettings(uint32_t word, uint32_t& lastSeq,
                         int& glide, bool& latch, bool& single)
{
    const uint32_t seq = (word >> 16) & 0xFFFFu;
    if (seq == lastSeq)
        return false;
    lastSeq = seq;
    glide  = static_cast<int>((word >> 8) & 0xFFu);
    latch  = (word & 0x2u) != 0;
    single = (word & 0x1u) != 0;
    return true;
}

int main()
{
    char d[96];

    /* --- scope ------------------------------------------------------- */
    std::printf("=== the keyboard plays on the circle screen only ===\n");
    ok("circle",       keyboardPlays(kScreenCircle), "plays");
    ok("slide",      ! keyboardPlays(kScreenSlide),  "inert - strips own their settings");
    ok("progressions", ! keyboardPlays(kScreenProg), "inert - the grid owns the voices");
    ok("setup",      ! keyboardPlays(kScreenSetup),  "inert - nothing to address");

    /* --- no key sets a chord type ------------------------------------ */
    std::printf("\n=== chord type comes from the cell, never a key ===\n");
    {
        int offenders = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (kDefaultKeyMap[pc].action == kKeyRetiredExt)
                ++offenders;
        std::snprintf(d, sizeof d, "%d keys bound to it", offenders);
        ok("no factory key selects a chord type", offenders == 0, d);

        /* The retired slot must still decode, so an old saved map does not
         * shift every binding after it by one. */
        KeyMapEntry restored[12];
        decodeKeyMap("3:0,1:0,1:1,2:5,1:2,1:3,2:2,1:4,0:0,1:5,0:0,1:6", restored);
        ok("an old map with chord-type bindings still decodes",
           restored[0].action == kKeyGlideToggle &&
           restored[1].action == kKeyDegree      &&
           restored[11].action == kKeyDegree,
           "bindings either side keep their meaning");
        ok("and the retired ones are inert",
           restored[3].action == kKeyRetiredExt &&
           restored[6].action == kKeyRetiredExt,
           "silent, not misread as a degree");
    }

    /* --- the secondary dominants ------------------------------------- */
    std::printf("\n=== D# and F# play II and III ===\n");
    {
        int  pos;
        Ring ring;

        const bool dSharp = cellForMidiNote(kDefaultKeyMap, 3, 0, pos, ring);
        const int  dRoot  = dSharp ? rootForPosition(pos, ring) : -1;
        /* In C, II is built on D. */
        ok("D# sounds a chord rooted on D", dSharp && dRoot == 2, "V-of-V");

        const bool fSharp = cellForMidiNote(kDefaultKeyMap, 6, 0, pos, ring);
        const int  fRoot  = fSharp ? rootForPosition(pos, ring) : -1;
        /* In C, III is built on E. */
        ok("F# sounds a chord rooted on E", fSharp && fRoot == 4, "V-of-vi");

        ok("both are dominants",
           degreeIsDominant(kDegreeSecII) && degreeIsDominant(kDegreeSecIII),
           "a major seventh would kill the pull");

        /* They must NOT be confused with the diatonic ii and iii. */
        int  pos2; Ring ring2;
        cellForMidiNote(kDefaultKeyMap, 2, 0, pos2, ring2);   /* D -> ii */
        const bool distinct = ! (pos2 == pos && ring2 == ring);
        ok("D# and D address different cells", distinct, "II is not ii");
    }

    /* --- the silent keys --------------------------------------------- */
    std::printf("\n=== G# and A# are silent but rebindable ===\n");
    {
        int pos; Ring ring;
        ok("G# sounds nothing", ! cellForMidiNote(kDefaultKeyMap, 8, 0, pos, ring),
           "silent");
        ok("A# sounds nothing", ! cellForMidiNote(kDefaultKeyMap, 10, 0, pos, ring),
           "silent");

        /* Rebinding one must take, or "programmable" is a lie. */
        KeyMapEntry m[12];
        std::memcpy(m, kDefaultKeyMap, sizeof m);
        m[8] = { kKeyDegree, kDegreeV };
        ok("G# can be rebound to a degree",
           cellForMidiNote(m, 8, 0, pos, ring), "and then it plays");

        char wire[kKeyMapStringMax];
        encodeKeyMap(m, wire, sizeof wire);
        KeyMapEntry back[12];
        decodeKeyMap(wire, back);
        ok("and the rebinding survives a save",
           back[8].action == kKeyDegree && back[8].value == kDegreeV,
           "round-trips");
    }

    /* --- the settings round trip ------------------------------------- */
    std::printf("\n=== a keyboard toggle reaches the editor ===\n");
    {
        /* The editor starts agreeing with the engine. */
        uint32_t seen  = 0;
        int      glide = kGlideMpe;
        bool     latch = false, single = false;

        uint32_t word = packSettings(0, kGlideMpe, false, false);
        ok("nothing to adopt before anything happens",
           ! readSettings(word, seen, glide, latch, single), "no spurious change");

        /* C# toggles glide on the audio thread. */
        word = packSettings(1, kGlideOff, false, false);
        const bool got = readSettings(word, seen, glide, latch, single);
        ok("the editor is told", got, "C# reached the UI");
        ok("and adopts the new mode", glide == kGlideOff, "one source of truth");

        /* Polling again must not re-apply it - that was the trap the octave
         * key fell into, where the UI could not tell a change from an echo. */
        ok("polling again changes nothing",
           ! readSettings(word, seen, glide, latch, single), "not re-applied");

        /* Latch and single travel the same way. */
        word = packSettings(2, kGlideOff, true, true);
        readSettings(word, seen, glide, latch, single);
        ok("latch and single travel too", latch && single, "all three");

        /* A long run of presses must stay in step - the reported symptom was
         * that pressing many keys drifted the two apart. */
        bool drifted = false;
        int  engineGlide = kGlideOff;
        for (uint32_t i = 3; i < 200; ++i) {
            engineGlide = (engineGlide == kGlideOff) ? kGlideMpe : kGlideOff;
            word = packSettings(i, engineGlide, true, true);
            readSettings(word, seen, glide, latch, single);
            if (glide != engineGlide) { drifted = true; break; }
        }
        ok("197 toggles stay in step", ! drifted, "no drift");

        /* The sequence counter wraps at 16 bits; it must still work after. */
        seen = 65534;
        word = packSettings(65535, kGlideMpe, false, false);
        ok("works at the top of the counter",
           readSettings(word, seen, glide, latch, single), "65535");
        word = packSettings(0, kGlideOff, false, false);
        ok("and across the wrap",
           readSettings(word, seen, glide, latch, single), "wrapped to 0");
    }


    /* --- the pinned wedge -------------------------------------------- */
    /*
     * Pinning the wedge pins the KEY, not merely the drawing.
     *
     * It used to pin only the highlight, on the reasoning that the wedge is a
     * reading aid while the keyboard's mapping is a separate question. That
     * split the plugin against itself: clicking F on the key ring with the
     * wedge pinned to C left the screen showing C's diatonic set, with F
     * labelled IV, while the keyboard remapped to F - so pressing C played F
     * major. Every label said one key and the keys played another.
     */
    std::printf("\n=== a pinned wedge pins the key ===\n");
    {
        /* Mirrors FortyFifthUI: selectKey() refuses while pinned, setKey()
         * does not. Both keep fSelectedKey and fLockedKey in step. */
        struct Wheel {
            int  selected = 0;      /* C */
            int  locked   = 0;
            bool pinned   = false;

            void setKey(int k) {
                k = ((k % 12) + 12) % 12;
                if (k == selected) return;
                selected = k;
                locked   = k;
            }
            void selectKey(int k) { if (! pinned) setKey(k); }
            int  highlightKey() const { return pinned ? locked : selected; }
        };

        Wheel w;

        /* Unpinned, a wheel click still picks the key - that must not regress. */
        w.selectKey(5);                       /* click F on the key ring */
        ok("unpinned: a wheel click picks the key", w.selected == 5, "F");
        ok("and the wedge follows it", w.highlightKey() == 5, "F");

        /* Pin on C, then click F. */
        w.setKey(0);
        w.pinned = true;
        w.selectKey(5);                       /* the reported gesture */
        ok("pinned: clicking F leaves the key alone", w.selected == 0, "still C");
        ok("and the wedge stays put",  w.highlightKey() == 0, "still C");

        /* The exact disagreement that was reported: the keyboard maps against
         * fSelectedKey, the labels against highlightKey(). Pinned, they must
         * never differ, or pressing C plays a chord the screen calls IV. */
        bool diverged = false;
        for (int click = 0; click < 12; ++click) {
            w.selectKey(click);
            if (w.selected != w.highlightKey()) { diverged = true; break; }
        }
        ok("12 clicks never split label from mapping", ! diverged,
           "one key everywhere");

        /* What the keyboard actually resolves to has to stay in the pinned
         * key. C is bound to I, so it must keep sounding C major. */
        int  pos; Ring ring;
        cellForMidiNote(kDefaultKeyMap, 0, w.selected, pos, ring);
        ok("pressing C still plays the pinned key's I",
           rootForPosition(pos, ring) == 0, "C major");

        /* The dropdown is a deliberate statement, so it overrides the pin -
         * otherwise the only way to change key would be to unpin first. */
        w.setKey(7);
        ok("the KEY dropdown still works while pinned", w.selected == 7, "G");
        ok("and the wedge moves with it", w.highlightKey() == 7, "G");

        /* Unpinning must not snap back to some older key. */
        w.pinned = false;
        ok("unpinning keeps the key it was on", w.selected == 7, "G");
        ok("and the wedge agrees", w.highlightKey() == 7, "G");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
