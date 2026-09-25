/*
 * The binding menu's row arithmetic.
 *
 * One flat list covers silent + 7 degrees + 7 extensions + 4 toggles. An
 * off-by-one anywhere in that decode would bind a key to the wrong action
 * silently - the menu would say one thing and the keyboard do another - so
 * every row is checked, and the round trip through bindingRowFor() too.
 */
#include "CircleTheory.hpp"
#include <cstdio>
#include <cstring>

using namespace fortyfifth;

static constexpr int kBindingRows =
    1 + (int)kDegreeCount + 4;

static void bindingForRow(int row, KeyAction& a, int& v)
{
    if (row == 0) { a = kKeyNone; v = 0; return; }
    --row;
    if (row < (int)kDegreeCount) { a = kKeyDegree; v = row; return; }
    row -= (int)kDegreeCount;
    switch (row) {
        case 0:  a = kKeyGlideToggle;  break;
        case 1:  a = kKeyLatchToggle;  break;
        case 2:  a = kKeySingleToggle; break;
        default: a = kKeyPanic;        break;
    }
    v = 0;
}

static int bindingRowFor(const KeyMapEntry& e)
{
    for (int r = 0; r < kBindingRows; ++r) {
        KeyAction a; int v;
        bindingForRow(r, a, v);
        if (a != e.action) continue;
        if (a == kKeyDegree) { if (v == e.value) return r; }
        else return r;
    }
    return 0;
}

static int fails = 0;
static void ok(const char* what, bool c, const char* d)
{
    std::printf("  %s  %-40s %s\n", c ? "ok  " : "FAIL", what, d);
    if (!c) ++fails;
}

int main()
{
    std::printf("=== every row decodes, and round-trips ===\n");
    char d[120];

    /* Each row must produce a binding that maps back to that same row. */
    bool roundTrip = true;
    for (int r = 0; r < kBindingRows; ++r) {
        KeyAction a; int v;
        bindingForRow(r, a, v);
        KeyMapEntry e { a, v };
        if (bindingRowFor(e) != r) {
            std::printf("     row %d does not round-trip\n", r);
            roundTrip = false;
        }
    }
    std::snprintf(d, sizeof d, "%d rows", kBindingRows);
    ok("every row round-trips", roundTrip, d);

    /*
     * Every degree must be reachable exactly once - the seven diatonic ones
     * plus the two secondary dominants.
     *
     * NO EXTENSION ROW MAY EXIST. A "chord type" binding used to set the
     * extension on every ring at once, which is the same state the wheel's
     * cells drive, so a key press silently rewrote what the circle showed.
     * Chord type belongs to the cell; if a row ever offers it again this
     * fails.
     */
    int degSeen[kDegreeCount] = {0};
    int toggles = 0, silent = 0, extRows = 0;
    for (int r = 0; r < kBindingRows; ++r) {
        KeyAction a; int v;
        bindingForRow(r, a, v);
        switch (a) {
            case kKeyNone:       ++silent; break;
            case kKeyDegree:     if (v >= 0 && v < (int)kDegreeCount) ++degSeen[v]; break;
            case kKeyRetiredExt: ++extRows; break;
            default:             ++toggles; break;
        }
    }
    bool allDeg = true;
    for (int i = 0; i < (int)kDegreeCount; ++i) if (degSeen[i] != 1) allDeg = false;

    ok("all 9 degrees reachable, once each", allDeg, "I..vii, II, III");
    ok("no chord-type row is offered", extRows == 0, "cell owns chord type");

    std::snprintf(d, sizeof d, "%d found", toggles);
    ok("4 toggles present", toggles == 4, d);
    std::snprintf(d, sizeof d, "%d found", silent);
    ok("exactly one silent row", silent == 1, d);

    /* The factory map must round-trip through the row decode too, or the
     * editor would open showing the wrong row highlighted. */
    std::printf("\n=== factory bindings highlight the right row ===\n");
    bool factoryOk = true;
    for (int pc = 0; pc < 12; ++pc) {
        const int r = bindingRowFor(kDefaultKeyMap[pc]);
        KeyAction a; int v;
        bindingForRow(r, a, v);
        const bool match = (a == kDefaultKeyMap[pc].action) &&
            (a != kKeyDegree || v == kDefaultKeyMap[pc].value);
        if (!match) { std::printf("     %s mismatched\n", kPitchName[pc]); factoryOk = false; }
    }
    ok("all 12 keys", factoryOk, "map to their own row");

    std::printf("\n%s (%d failure%s)\n", fails ? "FAIL" : "PASS",
                fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
