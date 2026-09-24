/*
 * The progression sequencer's data model, checked without a host.
 *
 *   g++ -std=c++17 -I src dev/test-progression.cpp -o /tmp/tp && /tmp/tp
 *
 * The thing worth testing here is locate(): it turns the host's running beat
 * count into a section and a step, and it is the only place that knows
 * sections chain rather than layer. Everything the sequencer plays comes from
 * its answer, so an off-by-one there is an off-by-one in the music.
 */

#include "CircleTheory.hpp"

#include <cstdio>
#include <cstring>

using namespace fortyfifth;

static int failures = 0;

static void ok(const char* what, bool good, const char* detail)
{
    if (good) {
        std::printf("  ok    %-36s %s\n", what, detail);
    } else {
        std::printf("  FAIL  %-36s %s\n", what, detail);
        ++failures;
    }
}

/* locate() for one beat, rendered as "B2" - section letter, step. */
static void checkBeat(const Progression& p, long long beat,
                      int wantSection, int wantStep)
{
    int s = -1, st = -1;
    const bool found = p.locate(beat, s, st);

    char detail[96];
    std::snprintf(detail, sizeof detail, "beat %lld -> %c%d",
                  beat, found ? sectionLetter(s) : '?', found ? st : -1);

    char what[64];
    std::snprintf(what, sizeof what, "beat %lld is %c%d",
                  beat, sectionLetter(wantSection), wantStep);

    ok(what, found && s == wantSection && st == wantStep, detail);
}

int main()
{
    std::printf("=== chaining: sections play one after another ===\n");
    {
        Progression p;
        p.count = 3;
        p.section[0].length = 4;   /* A: beats 0-3  */
        p.section[1].length = 2;   /* B: beats 4-5  */
        p.section[2].length = 3;   /* C: beats 6-8  */

        char d[64];
        std::snprintf(d, sizeof d, "%d beats", p.totalBeats());
        ok("total is the sum of the lengths", p.totalBeats() == 9, d);

        checkBeat(p, 0, 0, 0);
        checkBeat(p, 3, 0, 3);
        checkBeat(p, 4, 1, 0);   /* carries into B */
        checkBeat(p, 5, 1, 1);
        checkBeat(p, 6, 2, 0);   /* and into C     */
        checkBeat(p, 8, 2, 2);

        /* Wraps back to the top rather than running off the end. */
        checkBeat(p, 9, 0, 0);
        checkBeat(p, 13, 1, 0);

        /* A transport rolled back before zero must still land somewhere real,
         * not index backwards out of the array. */
        checkBeat(p, -1, 2, 2);
        checkBeat(p, -9, 0, 0);
    }

    std::printf("\n=== an empty progression plays nothing ===\n");
    {
        Progression p;
        p.count = 1;
        p.section[0].length = 0;

        int s, st;
        ok("locate declines rather than dividing by zero",
           ! p.locate(0, s, st), "no beats");
    }

    std::printf("\n=== duplicate: as next, and as last ===\n");
    {
        Progression p;
        p.count = 3;
        /* Mark each section so copies are identifiable. */
        for (int i = 0; i < 3; ++i) {
            p.section[i].length      = i + 1;
            p.section[i].cell[0].filled = true;
            p.section[i].cell[0].degree =
                static_cast<Degree>(kDegreeI + i);
        }

        /* Duplicating A "as next" must land at index 1 and push B, C along. */
        const int at = p.duplicate(0, true);
        char d[96];
        std::snprintf(d, sizeof d, "new section at %c, count %d",
                      sectionLetter(at), p.count);
        ok("as next inserts directly after", at == 1 && p.count == 4, d);

        ok("the copy matches the original",
           p.section[1].cell[0].degree == kDegreeI &&
           p.section[1].length == 1, "A == B");

        /* What was B is now C, and is unchanged. */
        ok("later sections shift, not overwrite",
           p.section[2].cell[0].degree == kDegreeII &&
           p.section[3].cell[0].degree == kDegreeIII,
           "old B, C intact at C, D");

        /* Duplicating "as last" appends without disturbing anything. */
        const int end = p.duplicate(0, false);
        std::snprintf(d, sizeof d, "new section at %c", sectionLetter(end));
        ok("as last appends to the end", end == 4 && p.count == 5, d);
    }

    std::printf("\n=== duplicate refuses when full ===\n");
    {
        Progression p;
        p.count = kMaxProgSections;
        ok("no silent overflow", p.duplicate(0, true) == -1, "declined");
    }

    std::printf("\n=== remove ===\n");
    {
        Progression p;
        p.count = 3;
        for (int i = 0; i < 3; ++i)
            p.section[i].cell[0].degree = static_cast<Degree>(kDegreeI + i);

        ok("removes the named section", p.remove(1) && p.count == 2, "B gone");
        ok("the rest close up",
           p.section[0].cell[0].degree == kDegreeI &&
           p.section[1].cell[0].degree == kDegreeIII, "A then old C");

        p.count = 1;
        ok("the last section is never removed", ! p.remove(0), "declined");
    }

    std::printf("\n=== presets load as degrees, so they transpose ===\n");
    {
        Progression p;
        loadPreset(p, 0, 0);   /* I-V-vi-IV */

        char d[96];
        std::snprintf(d, sizeof d, "length %d", p.section[0].length);
        ok("preset sets the section length", p.section[0].length == 4, d);

        const bool right =
            p.section[0].cell[0].degree == kDegreeI  &&
            p.section[0].cell[1].degree == kDegreeV  &&
            p.section[0].cell[2].degree == kDegreeVI &&
            p.section[0].cell[3].degree == kDegreeIV;
        ok("I-V-vi-IV is stored as those degrees", right, "I V vi IV");

        /* Beyond the preset's length the cells stay empty, so a shorter preset
         * loaded over a longer section does not leave stale chords behind. */
        ok("trailing cells are cleared", ! p.section[0].cell[4].filled,
           "rest after the fourth beat");

        /*
         * The real payoff: the same stored degrees resolve to different cells
         * in different keys. Resolve degree I in C and in G and check they
         * differ - that is transposition with no rewriting.
         */
        int posC, posG;
        Ring ringC, ringG;
        cellForDegree(kDegreeI, 0, posC, ringC);   /* C */
        cellForDegree(kDegreeI, 1, posG, ringG);   /* G, one step clockwise */

        std::snprintf(d, sizeof d, "cell %d in C, %d in G", posC, posG);
        ok("the same degree moves with the key", posC != posG, d);
    }

    std::printf("\n=== loading a preset at a chosen beat ===\n");
    {
        Progression p;
        loadPreset(p, 2, 0);            /* ii-V-I, three beats */

        char d[96];
        std::snprintf(d, sizeof d, "length %d", p.section[0].length);
        ok("a plain load starts at the first beat",
           p.section[0].length == 3 &&
           p.section[0].cell[0].degree == kDegreeII, d);

        /* Append the same three beats from beat four: the section grows to six
         * and the original three are untouched. */
        loadPresetAt(p, 2, 0, 3);

        std::snprintf(d, sizeof d, "length %d", p.section[0].length);
        ok("loading at a beat extends the section",
           p.section[0].length == 6, d);

        ok("what was already there is kept",
           p.section[0].cell[0].degree == kDegreeII &&
           p.section[0].cell[1].degree == kDegreeV  &&
           p.section[0].cell[2].degree == kDegreeI, "first phrase intact");

        ok("the preset lands at the chosen beat",
           p.section[0].cell[3].degree == kDegreeII &&
           p.section[0].cell[5].degree == kDegreeI, "second phrase at beat 4");

        /* Loading into the middle of a LONGER section must not truncate it. */
        Progression q;
        q.count = 1;
        q.section[0].length = 8;
        for (int i = 0; i < 8; ++i) {
            q.section[0].cell[i].filled = true;
            q.section[0].cell[i].degree = kDegreeVI;
        }
        loadPresetAt(q, 2, 0, 1);    /* three beats, from beat two */

        std::snprintf(d, sizeof d, "length %d", q.section[0].length);
        ok("a shorter preset does not shorten the section",
           q.section[0].length == 8, d);
        ok("the tail past the preset survives",
           q.section[0].cell[7].degree == kDegreeVI, "beat 8 still vi");
    }

    std::printf("\n=== dragging a chord between cells ===\n");
    {
        Progression p;
        loadPreset(p, 0, 0);    /* I-V-vi-IV */

        /* Move beat 1 to beat 3: beat 3 becomes I and beat 1 becomes a rest. */
        ok("a move succeeds", p.moveCell(0, 0, 0, 2, false), "beat 1 -> 3");
        ok("the chord arrives", p.section[0].cell[2].filled &&
           p.section[0].cell[2].degree == kDegreeI, "beat 3 is I");
        ok("a move leaves a rest behind",
           ! p.section[0].cell[0].filled, "beat 1 empty");

        /* A copy leaves the source alone. */
        Progression q;
        loadPreset(q, 0, 0);
        ok("a copy keeps the source", q.moveCell(0, 1, 0, 3, true) &&
           q.section[0].cell[1].filled &&
           q.section[0].cell[3].degree == kDegreeV, "beat 2 kept, beat 4 is V");

        /* Dropping past the end extends the section. */
        Progression r;
        loadPreset(r, 0, 0);       /* four beats */
        ok("dropping past the end extends",
           r.moveCell(0, 0, 0, 6, false) && r.section[0].length == 7,
           "length 7");

        /* Between sections. */
        Progression t;
        loadPreset(t, 0, 0);
        t.add();
        ok("a chord moves between sections",
           t.moveCell(0, 0, 1, 0, false) &&
           t.section[1].cell[0].degree == kDegreeI, "A beat 1 -> B beat 1");

        /* Dropping a cell on itself is not a move. */
        Progression u;
        loadPreset(u, 0, 0);
        ok("dropping on itself does nothing",
           ! u.moveCell(0, 1, 0, 1, false), "declined");

        /* Out-of-range drops are refused rather than corrupting the grid. */
        Progression v;
        loadPreset(v, 0, 0);
        ok("an out-of-range section is refused",
           ! v.moveCell(0, 0, 5, 0, false), "declined");
    }

    /*
     * Every state key setState() handles must also be DECLARED in
     * initState(), or DPF drops it before the plugin sees it.
     *
     * This was a real, silent bug: PLAY sent "progRunning", initState() had
     * never declared it, the host discarded the message, and the sequencer sat
     * there doing nothing with no error anywhere. The same was true of the
     * grid itself, so a saved project lost its progression too.
     *
     * Checked by scanning the source rather than by running the plugin,
     * because reproducing it needs a host.
     */
    std::printf("\n=== every handled state key is declared ===\n");
    {
        std::FILE* f = std::fopen("src/FortyFifthPlugin.cpp", "rb");
        if (f == nullptr) {
            std::printf("  FAIL  %-36s cannot open source\n", "state keys");
            ++failures;
        } else {
            static char src[512 * 1024];
            const size_t n = std::fread(src, 1, sizeof src - 1, f);
            src[n] = '\0';
            std::fclose(f);

            /* The keys the sequencer and the editor depend on. A key here must
             * appear as a state.key assignment, which is what initState()
             * does. */
            static const char* const kNeeded[] = {
                "progression", "progRunning", "progLegato",
                "uiScreen", "storageMode",
                "keyMap", "pedalAction", "bassNote", "voiceLeading",
            };

            for (const char* k : kNeeded) {
                char decl[64];
                std::snprintf(decl, sizeof decl, "state.key = \"%s\"", k);

                char what[64];
                std::snprintf(what, sizeof what, "%s is declared", k);

                ok(what, std::strstr(src, decl) != nullptr,
                   std::strstr(src, decl) ? "in initState" : "MISSING");
            }
        }
    }

    /*
     * The wire format must survive a round trip.
     *
     * Both sides of the plugin encode and decode grids - the editor sends one,
     * the DSP hands it back when the host saves a project - so a format that
     * loses anything loses it from every saved session.
     */
    std::printf("\n=== the grid survives encode and decode ===\n");
    {
        Progression p;
        p.count = 3;

        /* Something with every feature in it: rests, extensions, octaves
         * above and below, and sections of different lengths. */
        p.section[0].length = 5;
        p.section[0].cell[0] = { true,  kDegreeI,   kExtNone,  0 };
        p.section[0].cell[1] = { false, kDegreeI,   kExtNone,  0 };  /* rest */
        p.section[0].cell[2] = { true,  kDegreeV,   kExt7,    -2 };
        p.section[0].cell[3] = { true,  kDegreeVI,  kExtSus4,  2 };
        p.section[0].cell[4] = { true,  kDegreeVII, kExt9,    -1 };

        p.section[1].length = 1;
        p.section[1].cell[0] = { true,  kDegreeIV,  kExtAdd9,  1 };

        p.section[2].length = 2;
        p.section[2].cell[0] = { false, kDegreeI,   kExtNone,  0 };
        p.section[2].cell[1] = { true,  kDegreeII,  kExt6,     0 };

        char wire[kProgStringMax];
        encodeProgression(p, wire, sizeof wire);

        Progression back;
        const bool decoded = decodeProgression(wire, back);

        /* The wire string is bounded by kProgStringMax, which is far larger
         * than any detail line; print a prefix rather than widen the buffer to
         * five kilobytes for a label. */
        char d[160];
        std::snprintf(d, sizeof d, "%.150s", wire);
        ok("decodes at all", decoded, d);

        bool same = (back.count == p.count);
        for (int s = 0; s < p.count && same; ++s) {
            if (back.section[s].length != p.section[s].length) same = false;

            for (int i = 0; i < p.section[s].length && same; ++i) {
                const ProgCell& a = p.section[s].cell[i];
                const ProgCell& b = back.section[s].cell[i];

                if (a.filled != b.filled) { same = false; break; }
                if (! a.filled) continue;   /* a rest carries nothing else */

                if (a.degree != b.degree || a.ext != b.ext ||
                    a.octave != b.octave)
                    same = false;
            }
        }
        ok("every cell comes back identical", same, "3 sections, 8 beats");

        /* A negative octave is the case a naive format breaks on: "-1" and the
         * rest marker "-" begin the same way. */
        ok("negative octaves survive",
           back.section[0].cell[2].octave == -2 &&
           back.section[0].cell[4].octave == -1, "-2 and -1");

        /* Garbage must leave the target alone rather than blanking it. */
        Progression keep = p;
        ok("garbage is refused", ! decodeProgression("not a grid", keep),
           "declined");
        ok("the target is untouched after a refusal",
           keep.count == p.count, "3 sections");
    }

    std::printf("\n=== the key map survives encode and decode ===\n");
    {
        KeyMapEntry map[12];
        std::memcpy(map, kDefaultKeyMap, sizeof map);

        /* Rebind a few, including to actions with no argument. */
        map[0]  = { kKeyLatchToggle, 0 };
        map[3]  = { kKeyExtension,   kExtSus2 };
        map[7]  = { kKeyPanic,       0 };
        map[11] = { kKeyNone,        0 };

        char wire[kKeyMapStringMax];
        encodeKeyMap(map, wire, sizeof wire);

        KeyMapEntry back[12];
        decodeKeyMap(wire, back);

        bool same = true;
        for (int i = 0; i < 12; ++i)
            if (back[i].action != map[i].action ||
                back[i].value  != map[i].value)
                same = false;

        ok("every binding comes back identical", same, wire);

        /* A malformed entry falls back to the FACTORY binding for that key,
         * not to silence - a keyboard that stops responding is worse than one
         * that ignores a bad setting. */
        KeyMapEntry fallback[12];
        decodeKeyMap("", fallback);
        ok("an empty map is the factory map",
           std::memcmp(fallback, kDefaultKeyMap, sizeof fallback) == 0,
           "defaults restored");
    }

    /*
     * Every SETTING the plugin declares must also be read back by the
     * editor's stateChanged().
     *
     * Without that the editor never learns what a restored project holds: the
     * controls show factory defaults while the DSP plays the saved values, and
     * the first click on any control pushes its default back over the restored
     * setting. Opening a project and touching one button would silently
     * discard the rest of the session.
     *
     * gesture and panic are excluded deliberately: they are commands, not
     * settings, and there is nothing in the editor for them to restore.
     */
    std::printf("\n=== the editor reads back every setting ===\n");
    {
        std::FILE* f = std::fopen("src/FortyFifthUI.cpp", "rb");
        if (f == nullptr) {
            std::printf("  FAIL  %-36s cannot open source\n", "stateChanged");
            ++failures;
        } else {
            static char src[512 * 1024];
            const size_t n = std::fread(src, 1, sizeof src - 1, f);
            src[n] = '\0';
            std::fclose(f);

            const char* sc = std::strstr(src, "void stateChanged(");
            ok("the editor overrides stateChanged", sc != nullptr,
               sc ? "present" : "MISSING");

            if (sc != nullptr) {
                static const char* const kSettings[] = {
                    "keyMap", "progression", "octave", "latch", "glideMode",
                    "selectedKey", "singleNotes", "voiceLeading", "bassNote",
                    "pedalAction", "storageMode", "progLegato", "progRunning",
                    "uiScreen",
                };

                for (const char* k : kSettings) {
                    char quoted[64];
                    std::snprintf(quoted, sizeof quoted, "\"%s\"", k);

                    char what[64];
                    std::snprintf(what, sizeof what, "%s is read back", k);

                    /* Searched from stateChanged onward, so a mention
                     * elsewhere in the file does not count as handling. */
                    ok(what, std::strstr(sc, quoted) != nullptr,
                       std::strstr(sc, quoted) ? "in stateChanged" : "MISSING");
                }

                /* The per-ring keys are matched by prefix rather than by
                 * name, since there are three of each. */
                ok("per-ring extensions are read back",
                   std::strstr(sc, "\"ext\"") != nullptr ||
                   std::strstr(sc, "\"ext\", 3") != nullptr ||
                   std::strstr(sc, "key, \"ext\"") != nullptr ||
                   std::strstr(sc, "\"ext\", 3) == 0") != nullptr ||
                   std::strstr(sc, "strncmp(key, \"ext\"") != nullptr,
                   "prefix match");
                ok("per-ring voicings are read back",
                   std::strstr(sc, "strncmp(key, \"voice\"") != nullptr,
                   "prefix match");
            }
        }
    }

    std::printf("\n=== every preset is well formed ===\n");
    {
        bool allGood = true;
        for (int i = 0; i < kPresetProgressionCount; ++i) {
            const NamedProgression& n = kPresetProgression[i];
            if (n.length <= 0 || n.length > kMaxPresetSteps)
                allGood = false;
            for (int s = 0; s < n.length; ++s)
                if (n.degree[s] < 0 || n.degree[s] >= kDegreeCount)
                    allGood = false;
        }
        char d[64];
        std::snprintf(d, sizeof d, "%d presets", kPresetProgressionCount);
        ok("lengths and degrees are in range", allGood, d);
    }

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "PASS" : "FAIL",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
