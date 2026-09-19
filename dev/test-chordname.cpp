/*
 * Chord namer used by the MIDI monitor, checked against known voicings.
 *
 *   g++ -std=c++17 -I src dev/test-chordname.cpp -o /tmp/t && /tmp/t
 *
 * The logic is duplicated from FortyFifthUI::nameChord rather than included,
 * because that file needs the whole DPF UI stack to compile. Keep the two in
 * step; a mismatch here should be read as a failing test, not as drift.
 */

#include "CircleTheory.hpp"

#include <cstdio>
#include <cstring>

using namespace fortyfifth;

/* Copy of FortyFifthUI::nameChord - kept in step by this test. */
static bool nameChord(const bool* sounding, char* out, size_t outSize)
{
    int  pcs[12];
    int  npc = 0;
    int  lowest = -1;
    bool seen[12] = { false };

    for (int n = 0; n < 128; ++n) {
        if (! sounding[n]) continue;
        if (lowest < 0) lowest = n;
        if (! seen[n % 12]) { seen[n % 12] = true; if (npc < 12) pcs[npc++] = n % 12; }
    }
    if (npc == 0) return false;

    static const char* const kPC[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };

    if (npc == 1) { std::snprintf(out, outSize, "%s", kPC[pcs[0]]); return true; }

    for (int r = 0; r < npc; ++r) {
        const int root = pcs[r];
        int rel[12]; int nrel = 0;
        for (int i = 0; i < npc; ++i) rel[nrel++] = ((pcs[i]-root)%12+12)%12;
        for (int i = 1; i < nrel; ++i) {
            const int v = rel[i]; int j = i-1;
            while (j >= 0 && rel[j] > v) { rel[j+1] = rel[j]; --j; }
            rel[j+1] = v;
        }
        for (int t = 0; t < kChordTypeCount; ++t) {
            const ChordShape& sh = kChordShape[t];
            int want[12]; int nwant = 0; bool wseen[12] = { false };
            for (int i = 0; i < sh.count; ++i) {
                const int pc = ((sh.interval[i]%12)+12)%12;
                if (!wseen[pc]) { wseen[pc]=true; want[nwant++]=pc; }
            }
            for (int i = 1; i < nwant; ++i) {
                const int v = want[i]; int j = i-1;
                while (j >= 0 && want[j] > v) { want[j+1]=want[j]; --j; }
                want[j+1]=v;
            }
            if (nwant != nrel) continue;
            bool same = true;
            for (int i = 0; i < nrel && same; ++i) same = (rel[i]==want[i]);
            if (!same) continue;
            if (lowest >= 0 && (lowest%12) != root)
                std::snprintf(out, outSize, "%s%s/%s", kPC[root], sh.suffix, kPC[lowest%12]);
            else
                std::snprintf(out, outSize, "%s%s", kPC[root], sh.suffix);
            return true;
        }
    }
    std::snprintf(out, outSize, "(%d notes)", npc);
    return true;
}

static int fails = 0;

static void t(const char* what, const int* notes, int n, const char* want)
{
    bool s[128] = { false };
    for (int i = 0; i < n; ++i) s[notes[i]] = true;
    char got[32] = {0};
    nameChord(s, got, sizeof got);
    const bool ok = std::strcmp(got, want) == 0;
    std::printf("  %s  %-26s got %-10s want %s\n", ok?"ok  ":"FAIL", what, got, want);
    if (!ok) ++fails;
}

int main()
{
    std::printf("=== chord naming ===\n");
    { int n[] = {48,52,55};    t("C E G",        n,3,"C");     }
    { int n[] = {57,60,64};    t("A C E",        n,3,"Am");    }
    { int n[] = {52,55,59};    t("E G B",        n,3,"Em");    }
    { int n[] = {50,53,57};    t("D F A",        n,3,"Dm");    }
    { int n[] = {59,62,65};    t("B D F",        n,3,"Bdim");  }
    { int n[] = {48,52,55,59}; t("C E G B",      n,4,"Cmaj7"); }
    { int n[] = {55,59,62,65}; t("G B D F",      n,4,"G7");    }
    { int n[] = {60};          t("single note",  n,1,"C");     }
    { int n[] = {48,55};       t("C G power",    n,2,"C5");    }

    std::printf("\n=== inversions name the bass ===\n");
    { int n[] = {52,55,60};    t("E G C (1st inv)", n,3,"C/E"); }
    { int n[] = {55,60,64};    t("G C E (2nd inv)", n,3,"C/G"); }

    std::printf("\n=== octave doubling is not a new chord ===\n");
    { int n[] = {48,52,55,60}; t("C E G + C oct",  n,4,"C");   }

    std::printf("\n%s (%d failure%s)\n", fails?"FAIL":"PASS", fails, fails==1?"":"s");
    return fails ? 1 : 0;
}
