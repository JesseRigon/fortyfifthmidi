/*
 * Octave routing.
 *
 * One reported symptom: in Circle Mode the octave fell by one on every click
 * until the chord was inaudible.
 *
 * The cause was that the "octave" state key carried two different things - the
 * slider's BASE octave, which the editor must remember, and the EFFECTIVE
 * octave of one chord, which Slide Mode computed by adding its strip and
 * section shifts. The DSP stores one number, so the base was overwritten every
 * time a shifted chord played. DPF then replays the whole state map into
 * stateChanged() whenever the UI attaches, the editor read its own overwritten
 * value back as a new base, and the drift compounded.
 *
 * The fix: "octave" is the base and nothing else. Per-chord shifts travel with
 * the gesture, as a fourth field, and the DSP adds base + shift in one place.
 *
 * These tests cover the wire format and the arithmetic. They deliberately do
 * not link the plugin - that would need a host - so the parse and the clamp are
 * transcribed here and checked against the real source by test-progression.cpp's
 * source-scanning approach at the bottom of this file.
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>

static int failures = 0;

static void check(const char* what, int got, int want)
{
    const bool ok = (got == want);
    if (! ok) ++failures;
    std::printf("%-62s got %-5d want %-5d %s\n", what, got, want,
                ok ? "ok" : "FAIL");
}

static void checkBool(const char* what, bool got, bool want)
{
    const bool ok = (got == want);
    if (! ok) ++failures;
    std::printf("%-62s %-9s %-9s %s\n", what, got ? "true" : "false",
                want ? "true" : "false", ok ? "ok" : "FAIL");
}

/* ------------------------------------------------------------------ */
/* Mirrors FortyFifthPlugin.cpp: clampOctave() and the gesture parse.  */

static constexpr int kOctaveLow     = 1;
static constexpr int kOctaveHigh    = 7;
static constexpr int kGestureOctMin = -4;
static constexpr int kGestureOctMax =  4;

static int clampOctave(int oct)
{
    return oct < kOctaveLow ? kOctaveLow : (oct > kOctaveHigh ? kOctaveHigh : oct);
}

/* Returns false when the gesture is malformed and must be dropped. */
static bool parseGesture(const char* value, char* verb, int& position,
                         int& ring, int& octShift)
{
    position = 0; ring = 0; octShift = 0;
    const int fields = std::sscanf(value, "%15[^:]:%d:%d:%d",
                                   verb, &position, &ring, &octShift);
    if (fields < 3) return false;
    if (fields < 4) octShift = 0;
    if (ring < 0 || ring >= 3) return false;
    if (octShift < kGestureOctMin) octShift = kGestureOctMin;
    if (octShift > kGestureOctMax) octShift = kGestureOctMax;
    return true;
}

/* ------------------------------------------------------------------ */
/* Mirrors FortyFifthUI.cpp: the editor's base and its echo guard.      */

struct Editor {
    int base   = 4;
    int pushed = 4;

    /* stateChanged("octave") */
    void stateChanged(int v)
    {
        if (v != pushed) {
            base   = (v < 1) ? 1 : (v > 7) ? 7 : v;
            pushed = base;
        }
    }
};

int main()
{
    char verb[16];
    int  pos, ring, oct;

    /* --- wire format ------------------------------------------------ */
    std::printf("=== gesture wire format ===\n");

    checkBool("four-field gesture parses", parseGesture("press:3:1:2", verb, pos, ring, oct), true);
    check("  position", pos, 3);
    check("  ring",     ring, 1);
    check("  shift",    oct, 2);

    /* Three fields must still parse: the wheel sends no shift, and a saved
     * gesture from an older build would otherwise be dropped. */
    checkBool("three-field gesture still parses", parseGesture("press:5:2", verb, pos, ring, oct), true);
    check("  shift defaults to none", oct, 0);

    checkBool("negative shift parses", parseGesture("move:0:0:-1", verb, pos, ring, oct), true);
    check("  shift", oct, -1);

    checkBool("two fields is malformed", parseGesture("press:5", verb, pos, ring, oct), false);
    checkBool("bad ring is rejected",    parseGesture("press:5:9:0", verb, pos, ring, oct), false);

    /* A wild shift is clamped, not dropped: the chord should sound at the edge
     * of the range rather than vanish. */
    checkBool("absurd shift still plays", parseGesture("press:0:0:99", verb, pos, ring, oct), true);
    check("  clamped to max", oct, kGestureOctMax);
    checkBool("absurd negative still plays", parseGesture("press:0:0:-99", verb, pos, ring, oct), true);
    check("  clamped to min", oct, kGestureOctMin);

    /* --- the clamp -------------------------------------------------- */
    std::printf("\n=== octave clamp ===\n");
    check("base 4 + no shift", clampOctave(4 + 0), 4);
    check("base 4 + octave-up strip", clampOctave(4 + 1), 5);
    check("base 7 + octave-up strip clamps", clampOctave(7 + 1), 7);
    check("base 1 + two down clamps", clampOctave(1 - 2), 1);
    check("base 7 is allowed", clampOctave(7), 7);
    check("base 1 is allowed", clampOctave(1), 1);

    /* --- the reported bug ------------------------------------------- */
    std::printf("\n=== the reported bug: clicking must not transpose ===\n");

    /* Every click in Circle Mode, with DPF replaying state each time. The
     * gesture carries shift 0, so the base is never rewritten. */
    {
        Editor e;
        for (int i = 0; i < 20; ++i) {
            parseGesture("press:0:0:0", verb, pos, ring, oct);
            /* the DSP sounds at base + shift, and never writes "octave" back */
            (void) clampOctave(e.base + oct);
            e.stateChanged(e.base);      /* DPF's replay of the unchanged base */
        }
        check("circle: 20 clicks leave the base alone", e.base, 4);
    }

    /* Slide Mode: the top strip is the tonic an octave up. Its shift must move
     * the chord without moving the base. */
    {
        Editor e;
        for (int i = 0; i < 20; ++i) {
            parseGesture("press:0:0:1", verb, pos, ring, oct);
            const int sounded = clampOctave(e.base + oct);
            if (i == 0) check("slide: octave-up strip sounds one higher", sounded, 5);
            e.stateChanged(e.base);
        }
        check("slide: 20 presses leave the base alone", e.base, 4);
    }

    /* The sequencer's per-cell offsets, alternating, over a long run. */
    {
        Editor e;
        const int cells[] = { 0, -1, 2, -2, 1, 0, -1, 1 };
        for (int i = 0; i < 64; ++i) {
            const int shift = cells[i % 8];
            char g[32];
            std::snprintf(g, sizeof g, "press:0:0:%d", shift);
            parseGesture(g, verb, pos, ring, oct);
            (void) clampOctave(e.base + oct);
            e.stateChanged(e.base);
        }
        check("sequencer: 64 cells leave the base alone", e.base, 4);
    }

    /* --- the base is still settable --------------------------------- */
    std::printf("\n=== the slider still works ===\n");
    {
        Editor e;
        e.stateChanged(6);                       /* user drags the slider */
        check("a genuine change applies", e.base, 6);

        parseGesture("press:0:0:1", verb, pos, ring, oct);
        check("and shifts apply on top of it", clampOctave(e.base + oct), 7);

        e.stateChanged(6);                       /* DPF replays it */
        check("the replay does not move it again", e.base, 6);
    }

    /* Moving the base while a shifted screen is in front must move both, and
     * keep their distance - that is what makes it one setter. */
    {
        Editor e;
        parseGesture("press:0:0:-1", verb, pos, ring, oct);
        check("base 4, shift -1 sounds at 3", clampOctave(e.base + oct), 3);
        e.stateChanged(2);
        check("base moved to 2", e.base, 2);
        check("same shift now sounds at 1", clampOctave(e.base + oct), 1);
    }


    /* --- a drag must land where a click does -------------------------- */
    /*
     * Reported: "selecting the first degree and gliding (click and drag) up to
     * the next 1st degree at the 8th col position plays the original octave not
     * the octave up. but clicking it works as it should."
     *
     * Slide Mode's last strip is the tonic AN OCTAVE UP - that is what makes it
     * worth having, since it closes the scale. Both gestures carry the shift:
     * a press sends it, and so does a move.
     *
     * The MOVE branch discarded it, keeping g->octave - the octave the group
     * already had - on the reasoning that a drag stays where it started. True
     * on the wheel, where every cell sits at the base octave and the two agree,
     * which is why the fault was invisible there and showed only on the screen
     * whose cells disagree.
     *
     * This is a source check rather than an arithmetic one: the arithmetic was
     * never wrong, the branch simply read the wrong variable. Asserting on the
     * numbers would have passed throughout.
     */
    std::printf("\n=== a move uses the gesture's octave, not the group's ===\n");
    {
        FILE* f = std::fopen("src/FortyFifthPlugin.cpp", "rb");
        if (f == nullptr) {
            std::printf("%-62s %s\n", "could not open src/FortyFifthPlugin.cpp", "FAIL");
            ++failures;
        } else {
            std::fseek(f, 0, SEEK_END);
            const long len = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            char* buf = static_cast<char*>(std::malloc(len + 1));
            const size_t rd = std::fread(buf, 1, len, f);
            buf[rd] = '\0';
            std::fclose(f);

            /* Isolate the move gesture: from its case label to the next one. */
            const char* start = std::strstr(buf, "case kGestureMove:");
            const char* end   = start ? std::strstr(start, "case kGestureRetrigger:")
                                      : nullptr;

            if (start == nullptr || end == nullptr) {
                std::printf("%-62s %s\n", "could not locate the move gesture", "FAIL");
                ++failures;
            } else {
                const size_t n = static_cast<size_t>(end - start);
                char* move = static_cast<char*>(std::malloc(n + 1));
                std::memcpy(move, start, n);
                move[n] = '\0';

                /*
                 * One g->octave is legitimate: the glide distance measures FROM
                 * where the group is TO where the gesture asks, so the group's
                 * octave is the starting point. Every other use would be the
                 * bug returning.
                 */
                int uses = 0;
                for (const char* q = move; (q = std::strstr(q, "g->octave")) != nullptr; ++q)
                    ++uses;

                std::printf("%-62s %d\n", "  g->octave appearances in the move branch", uses);
                checkBool("the gesture's octave is used",
                          std::strstr(move, "gestureOct") != nullptr, true);
                check("g->octave survives only as the distance's origin", uses, 1);

                /*
                 * And the one that remains must be the ORIGIN of the travel.
                 *
                 * The arithmetic moved into semitonesBetween(), which takes the
                 * two chord addresses and is unit-tested on its own below - so
                 * this no longer looks for the subtraction spelled out inline.
                 * What it still checks is the thing that was actually wrong:
                 * the group's octave appears only as the FROM address, and the
                 * destination is built from the gesture's octave.
                 *
                 * Asserting the old literal would only prove the expression had
                 * not been reworded, which is not the property anyone cares
                 * about.
                 */
                checkBool("  that one is the travel's origin",
                          std::strstr(move, "ChordTarget from(g->root, g->type, "
                                            "g->ring, g->octave)") != nullptr,
                          true);
                /*
                 * The DESTINATION specifically - the one the glide is started
                 * with, not merely the presence of that constructor somewhere in
                 * the branch. canGlideBetween() is called a few lines above with
                 * an identically spelled temporary, so searching for the
                 * constructor alone passes even with the bug reintroduced. The
                 * count check above does catch it, but an assertion that cannot
                 * fail is worse than no assertion: it reads as coverage.
                 *
                 * Now pinned to the named local that begin() is handed, since the
                 * destination stopped being a member the moment glide state
                 * became an object.
                 */
                /*
                 * Matched WITHOUT the closing paren, so adding a further
                 * argument - voicing did exactly this - does not break an
                 * assertion about the octave. The property is that the
                 * destination is built from gestureOct; what follows it is not
                 * this test's business.
                 */
                checkBool("  and the destination carries the gesture's octave",
                          std::strstr(move, "const ChordTarget to(root, type, r, "
                                            "gestureOct") != nullptr,
                          true);
                checkBool("  with the distance derived, not hand-rolled",
                          std::strstr(move, "semitonesBetween(from, to)")
                              != nullptr,
                          true);

                /*
                 * And the glide is started in ONE call, so the destination and
                 * the distance cannot disagree.
                 *
                 * This replaces what used to be the real risk here: ten separate
                 * assignments, of which a site could make nine. There is no
                 * spelling of begin() that sets the target without the distance.
                 */
                checkBool("  and both are handed to begin() together",
                          std::strstr(move, "fGlide.begin(g, to, "
                                            "semitonesBetween(from, to)") != nullptr,
                          true);

                std::free(move);
            }
            std::free(buf);
        }
    }

    /* ------------------------------------------------------------------ */
    /* Guard the invariant in the source itself: no screen may write an     */
    /* effective octave into the "octave" state key.                        */
    std::printf("\n=== source: only the base is written to \"octave\" ===\n");
    {
        FILE* f = std::fopen("src/FortyFifthUI.cpp", "rb");
        if (f == nullptr) {
            std::printf("%-62s %s\n", "could not open src/FortyFifthUI.cpp", "FAIL");
            ++failures;
        } else {
            std::fseek(f, 0, SEEK_END);
            const long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            char* buf = static_cast<char*>(std::malloc(n + 1));
            const size_t rd = std::fread(buf, 1, n, f);
            buf[rd] = '\0';
            std::fclose(f);

            /* Every setState("octave", ...) must be preceded closely by a plain
             * fOctave, never by an addition. Searching for the shapes that
             * caused the bug is more durable than counting call sites. */
            int offenders = 0;
            for (const char* p = buf; (p = std::strstr(p, "setState(\"octave\"")) != nullptr; ++p) {
                /* Look back a little for an arithmetic combination. */
                const char* start = (p - buf > 400) ? p - 400 : buf;
                for (const char* q = start; q < p; ++q) {
                    if (std::strncmp(q, "fOctave +", 9) == 0 ||
                        std::strncmp(q, "fOctave+",  8) == 0) {
                        ++offenders;
                        break;
                    }
                }
            }
            check("no combined octave is pushed as state", offenders, 0);

            /* And the shift must actually be on the wire. */
            checkBool("sendGesture carries an octave shift",
                      std::strstr(buf, "int octShift = 0") != nullptr, true);

            std::free(buf);
        }
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
