/*
 * Layout arithmetic for the UI chrome, checked without a display.
 *
 *   g++ -std=c++17 dev/test-layout.cpp -o /tmp/test-layout && /tmp/test-layout
 *
 * The octave slider claims space down the left edge and the wheel is sized from
 * what is left; get that wrong and the two overlap, or the slider collapses to
 * nothing. Screenshotting to check is unreliable - the plugin draws to a
 * composited OpenGL surface that capture APIs hand back as black - so the
 * geometry is verified numerically instead, at several window sizes and with
 * the monitor panel both open and closed.
 *
 * The constants are duplicated from FortyFifthUI.cpp rather than included,
 * because that file needs the whole DPF UI stack to compile. Keep them in step
 * with it: a mismatch here should be read as a failing test, not as drift.
 */
#include <cstdio>
#include <cstring>

static const float kTabH    = 28.0f;   /* tab bar above the control rows */
static const float kDropY   = kTabH + 36.0f;
static const float kDropH   = 22.0f;
static const float kHeaderH = 22.0f;
static const float kSliderW = 46.0f;
static const float kOctaveMin = 1.0f;
static const float kOctaveMax = 7.0f;
static const float kHubFraction = 0.34f;
static const float kRingWeight[3] = { 1.00f, 0.80f, 0.50f };

static float W = 640.0f, H = 760.0f;
static bool  monitorOpen = false;
static const int kLogLines = 14;

static float chromeTop()    { return kDropY + kDropH + 10.0f; }
static float chromeLeft()   { return kSliderW + 16.0f; }
/* Two stacked panels, each with its own header and collapsed state. */
static const float kRollH = 54.0f;
static bool rollOpen = true;

static float chromeBottom()
{
    return kHeaderH * 2.0f
         + (monitorOpen ? kLogLines * 14.0f + 10.0f : 0.0f)
         + (rollOpen    ? kRollH : 0.0f);
}

static float wheelCentreX() { return chromeLeft() + (W - chromeLeft()) * 0.5f; }
static float wheelCentreY() { return chromeTop() + (H - chromeTop() - chromeBottom()) * 0.5f; }
static float wheelRadius()
{
    const float uh = H - chromeTop() - chromeBottom();
    const float uw = W - chromeLeft();
    return (uw < uh ? uw : uh) * 0.46f;
}

struct B { float x, y, w, h; };
static B octaveSlider()
{
    const float top = chromeTop();
    return { 8.0f, top, kSliderW, H - top - chromeBottom() - 8.0f };
}

static int fails = 0;
static void ok(const char* what, bool cond, const char* detail)
{
    std::printf("  %s  %-44s %s\n", cond ? "ok  " : "FAIL", what, detail);
    if (!cond) ++fails;
}

static void report(const char* title)
{
    std::printf("\n=== %s (%gx%g, monitor %s) ===\n",
                title, W, H, monitorOpen ? "open" : "collapsed");

    const B s = octaveSlider();
    char d[160];

    std::snprintf(d, sizeof d, "y=%.0f h=%.0f (bottom %.0f)", s.y, s.h, s.y + s.h);
    ok("slider spans the usable height", s.h > 200.0f, d);

    const int span = (int)(kOctaveMax - kOctaveMin);
    const float rowH = s.h / (span + 1);
    std::snprintf(d, sizeof d, "%d rows of %.1fpx", span + 1, rowH);
    ok("each octave row is touch-sized", rowH >= 24.0f, d);

    ok("slider clears the control rows", s.y >= kDropY + kDropH, "below row 2");

    std::snprintf(d, sizeof d, "slider bottom %.0f vs monitor top %.0f",
                  s.y + s.h, H - chromeBottom());
    ok("slider clears the monitor header", s.y + s.h <= H - chromeBottom(), d);

    const float cx = wheelCentreX(), cy = wheelCentreY(), r = wheelRadius();
    std::snprintf(d, sizeof d, "wheel left edge %.0f vs slider right %.0f",
                  cx - r, s.x + s.w);
    ok("wheel never overlaps the slider", (cx - r) >= (s.x + s.w), d);

    std::snprintf(d, sizeof d, "top %.0f vs chrome %.0f", cy - r, chromeTop());
    ok("wheel clears the controls", (cy - r) >= chromeTop() - 1.0f, d);

    std::snprintf(d, sizeof d, "bottom %.0f vs %.0f", cy + r, H - chromeBottom());
    ok("wheel clears the monitor", (cy + r) <= H - chromeBottom() + 1.0f, d);

    std::snprintf(d, sizeof d, "radius %.0f", r);
    ok("wheel is still usably large", r > 120.0f, d);
}

/* Slide bank geometry, mirroring FortyFifthUI::slideRect(). */
static B slideArea()
{
    const float top = chromeTop();
    return { chromeLeft(), top,
             W - chromeLeft() - 10.0f, H - top - chromeBottom() - 8.0f };
}

static B slideRect(int index, int n)
{
    const B a = slideArea();
    const float w = a.w / n;
    return { a.x + index * w, a.y, w, a.h };
}

static void reportSlides(const char* title, int n, int sections)
{
    std::printf("\n=== slide bank: %s (%d strips, %d sections) ===\n",
                title, n, sections);
    char d[160];

    const B first = slideRect(0, n);
    const B last  = slideRect(n - 1, n);

    std::snprintf(d, sizeof d, "%.1fpx wide", first.w);
    ok("strips are wide enough to hit", first.w >= 40.0f, d);

    const float rowH = first.h / sections;
    std::snprintf(d, sizeof d, "%.1fpx tall", rowH);
    ok("sections are tall enough to hit", rowH >= 40.0f, d);

    /*
     * The strips must TOUCH. A gap would end a drag as the finger crossed it,
     * which is precisely what breaks glide between chords.
     */
    bool touching = true;
    for (int i = 1; i < n; ++i) {
        const B prev = slideRect(i - 1, n);
        const B cur  = slideRect(i, n);
        const float gap = cur.x - (prev.x + prev.w);
        if (gap > 0.01f || gap < -0.01f) touching = false;
    }
    ok("strips share borders (glide can cross)", touching, "no gaps");

    std::snprintf(d, sizeof d, "bank left %.0f vs slider right %.0f",
                  first.x, 8.0f + kSliderW);
    ok("bank clears the octave slider", first.x >= 8.0f + kSliderW, d);

    std::snprintf(d, sizeof d, "bank right %.0f vs window %.0f",
                  last.x + last.w, W);
    ok("bank fits the window", last.x + last.w <= W, d);
}

/*
 * The ROOT button's four-state cycle, and the label that reports it.
 *
 * Transcribed from FortyFifthUI.cpp for the same reason as the constants
 * above: that file needs the whole DPF UI stack to compile.
 *
 * This exists because of a real bug. Both the label and the cycle keyed off
 * "is voice leading in force right now" rather than "is it switched on", and
 * plain glide suspends leading without switching it off. So with glide on the
 * button read FIRST while leading was still set, producing B-E-G for an Em;
 * and clicking that button advanced to SECOND, because the handler trusted its
 * own label and skipped the state it claimed to be in. FIRST was unreachable.
 */
static void checkRootCycle()
{
    std::printf("\n-- ROOT cycle --\n");

    enum { kFirst = 0, kSecond, kThird, kBassCount };
    static const char* kName[kBassCount] = { "FIRST", "SECOND", "THIRD" };

    /* Run the cycle with glide ON, where leading is suspended - the case that
     * was broken. Every state must be reachable and labelled for what it is. */
    for (int glideOn = 0; glideOn <= 1; ++glideOn) {
        bool leading = true;   /* the plugin's default */
        int  bass    = kFirst;

        static const char* kWant[4] = { "AUTO", "FIRST", "SECOND", "THIRD" };
        bool allRight = true;
        char trail[128] = {0};

        for (int step = 0; step < 4; ++step) {
            /* The label, as drawn. */
            const char* label = leading ? "AUTO" : kName[bass];

            std::snprintf(trail + std::strlen(trail),
                          sizeof trail - std::strlen(trail),
                          "%s%s", step ? " -> " : "", label);

            if (std::strcmp(label, kWant[step]) != 0)
                allRight = false;

            /* The click, as handled. */
            if (leading) {
                leading = false;
                bass    = kFirst;
            } else if (bass == kBassCount - 1) {
                leading = true;
                bass    = kFirst;
            } else {
                ++bass;
            }
        }

        /* A fifth click must return to where it started. */
        const bool wrapped = leading && bass == kFirst;

        char d[192];
        std::snprintf(d, sizeof d, "%s (glide %s)", trail, glideOn ? "on" : "off");
        ok("cycle reaches all four states", allRight && wrapped, d);
    }
}

/*
 * Every tab must be hit-testable, and they must not overlap.
 *
 * The click loop was hardcoded to two tabs while four were being drawn, so
 * SLIDE and PROGRESSIONS were painted, labelled and completely dead - the bar
 * looked finished and simply did not respond. Geometry alone would not have
 * caught it, so this walks the same range the handler walks and checks that a
 * click at each tab's centre resolves to that tab.
 */
static void checkTabs()
{
    std::printf("\n-- tab bar --\n");

    const float kTabH     = 28.0f;
    const float kSetupTabW = 34.0f;
    const float kTabW      = 104.0f;
    const int   kScreens   = 4;

    struct R { float x, y, w, h; };
    auto tab = [&](int i) -> R {
        if (i == 0)
            return { 10.0f, 4.0f, kSetupTabW, kTabH - 8.0f };
        const float x = 10.0f + kSetupTabW + 4.0f + (i - 1) * (kTabW + 4.0f);
        return { x, 4.0f, kTabW, kTabH - 8.0f };
    };

    /* Each tab's centre must resolve to that tab and no other. */
    bool allHit = true;
    for (int i = 0; i < kScreens; ++i) {
        const R t = tab(i);
        const float cx = t.x + t.w * 0.5f;
        const float cy = t.y + t.h * 0.5f;

        int landed = -1;
        for (int j = 0; j < kScreens; ++j) {
            const R u = tab(j);
            if (cx >= u.x && cx <= u.x + u.w && cy >= u.y && cy <= u.y + u.h) {
                if (landed >= 0) allHit = false;   /* overlapping tabs */
                landed = j;
            }
        }
        if (landed != i) allHit = false;
    }

    char d[96];
    std::snprintf(d, sizeof d, "%d tabs, each hit at its centre", kScreens);
    ok("every tab is reachable", allHit, d);

    /* And the bar must fit the narrowest window the layout is checked at. */
    const R last = tab(kScreens - 1);
    std::snprintf(d, sizeof d, "bar ends at %.0f vs window %.0f",
                  last.x + last.w, 520.0f);
    ok("the tab bar fits the window", last.x + last.w <= 520.0f, d);
}

int main()
{
    report("default size");
    reportSlides("diatonic, octave sections", 8, 5);
    reportSlides("pentatonic, variation sections", 6, 4);

    /* Both panels open is the tightest the wheel ever gets - if the layout
     * survives this it survives everything. */
    monitorOpen = true;
    rollOpen    = true;
    report("log + keyboard both open");

    /* Keyboard alone: the combination the roll exists to support. */
    monitorOpen = false;
    rollOpen    = true;
    report("keyboard only");

    monitorOpen = true;
    rollOpen    = false;
    report("log only");

    monitorOpen = false;
    W = 520.0f; H = 640.0f;
    report("smaller host window");

    checkRootCycle();
    checkTabs();

    std::printf("\n%s (%d failure%s)\n", fails ? "FAIL" : "PASS",
                fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
