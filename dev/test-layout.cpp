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

    std::printf("\n%s (%d failure%s)\n", fails ? "FAIL" : "PASS",
                fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
