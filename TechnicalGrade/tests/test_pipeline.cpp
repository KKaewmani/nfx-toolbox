// Standalone checks on the colour pipeline. No OFX, no GPU: this exercises the
// exact source the Metal kernel compiles, so a pass here means the maths is
// right and only the plumbing is left to verify inside Resolve.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../ColorMath.h"
#include "../KernelParams.h"
#include "../WhiteBalance.h"

namespace
{
    int g_Failures = 0;
    int g_Checks = 0;

    void check(bool condition, const std::string& what)
    {
        ++g_Checks;
        if (!condition)
        {
            ++g_Failures;
            printf("  FAIL  %s\n", what.c_str());
        }
    }

    void checkClose(double actual, double expected, double tolerance, const std::string& what)
    {
        ++g_Checks;
        const double error = fabs(actual - expected);
        if (!(error <= tolerance))
        {
            ++g_Failures;
            printf("  FAIL  %s: got %.9g, expected %.9g, error %.3g > %.3g\n",
                   what.c_str(), actual, expected, error, tolerance);
        }
    }

    void checkCloseRelative(double actual, double expected, double tolerance, const std::string& what)
    {
        const double scale = fabs(expected) > 1e-6 ? fabs(expected) : 1.0;
        checkClose(actual / scale, expected / scale, tolerance, what);
    }

    void section(const char* name)
    {
        printf("%s\n", name);
    }

    // A frame to hang the falloff geometry off. Not square, so a test that
    // confuses the two axes has somewhere to fail.
    const double kFrameWidth = 1920.0;
    const double kFrameHeight = 1080.0;

    KernelParams defaultParams()
    {
        KernelParams p;
        wb::computeMatrix(0.0, 0.0, true, p.matrix);
        p.pivot = 0.18f;
        p.slope = 1.0f;
        p.shadowEnable = 0.0f;
        p.shadowLimit = -8.0f;
        p.shadowSoftness = 0.5f;
        p.highlightEnable = 0.0f;
        p.highlightLimit = 8.0f;
        p.highlightSoftness = 0.5f;
        p.workingSpace = CM_SPACE_ACESCCT;
        setVignetteGeometry(p, 0.0, kFrameWidth, kFrameHeight, 1.0);
        return p;
    }

    void processAt(const KernelParams& p, double x, double y, float r, float g, float b,
                   float* outR, float* outG, float* outB)
    {
        cmProcessPixel(r, g, b, static_cast<float>(x), static_cast<float>(y),
                       reinterpret_cast<const float*>(&p), outR, outG, outB);
    }

    // Colour behaviour is read at the centre of the frame, where the falloff is
    // one whatever it is set to, so these tests stay about colour.
    void process(const KernelParams& p, float r, float g, float b, float* outR, float* outG, float* outB)
    {
        processAt(p, 0.5 * kFrameWidth, 0.5 * kFrameHeight, r, g, b, outR, outG, outB);
    }

    // A spread of linear values covering deep shadow, middle grey, highlights
    // and the negatives that AP1 legitimately produces.
    std::vector<double> linearSamples()
    {
        std::vector<double> v;
        v.push_back(-0.05);
        v.push_back(-0.001);
        v.push_back(0.0);
        v.push_back(1e-6);
        v.push_back(0.0001);
        v.push_back(0.0078125);
        v.push_back(0.045);
        v.push_back(0.18);
        v.push_back(0.5);
        v.push_back(0.72);
        v.push_back(1.0);
        v.push_back(4.0);
        v.push_back(46.08);
        v.push_back(1000.0);
        return v;
    }
}

// ---------------------------------------------------------------------------

static void testTransferFunctions()
{
    section("Working space transfer functions");

    const std::vector<double> samples = linearSamples();

    for (size_t i = 0; i < samples.size(); ++i)
    {
        const float lin = static_cast<float>(samples[i]);

        // The tolerance is the float32 floor: encoding divides the exponent by
        // 17.52 and decoding multiplies it back, and neither constant is exactly
        // representable. A few parts per million is roughly 3e-6 of a stop.
        const float cct = cmLinearToACEScct(lin);
        checkCloseRelative(cmACEScctToLinear(cct), lin, 1e-5, "ACEScct linear round trip");

        if (lin > 0.0f)
        {
            const float cc = cmLinearToACEScc(lin);
            checkCloseRelative(cmACESccToLinear(cc), lin, 1e-5, "ACEScc linear round trip");
        }
    }

    // And the other direction, over the encoded domain below the half-float ceiling.
    for (int i = 0; i <= 100; ++i)
    {
        const float encoded = -0.35f + (1.46f - -0.35f) * (i / 100.0f);
        checkClose(cmLinearToACEScct(cmACEScctToLinear(encoded)), encoded, 1e-5, "ACEScct encoded round trip");
        checkClose(cmLinearToACEScc(cmACESccToLinear(encoded)), encoded, 1e-5, "ACEScc encoded round trip");
    }

    // Anchors from the ACES specifications.
    checkClose(cmLinearToACEScct(0.18), 0.4135884, 1e-6, "ACEScct of middle grey");
    checkClose(cmLinearToACEScc(0.18), 0.4135884, 1e-6, "ACEScc of middle grey");
    checkClose(cmLinearToACEScct(0.0), 0.0729055, 1e-6, "ACEScct of zero sits on the toe");
    checkClose(cmACEScctToLinear(cmLinearToACEScct(0.0078125f)), 0.0078125, 1e-7, "ACEScct is continuous at the toe break");

    // The toe and the log segment must agree at the break, or the curve kinks.
    const float below = cmLinearToACEScct(0.0078125f * 0.9999f);
    const float above = cmLinearToACEScct(0.0078125f * 1.0001f);
    checkClose(above - below, 0.0, 2e-5, "ACEScct has no step at the toe break");
}

static void testIdentity()
{
    section("Identity at default settings");

    const KernelParams p = defaultParams();
    const std::vector<double> samples = linearSamples();

    for (size_t i = 0; i < samples.size(); ++i)
    {
        const float encoded = cmLinearToACEScct(static_cast<float>(samples[i]));

        float r, g, b;
        process(p, encoded, encoded, encoded, &r, &g, &b);

        checkClose(r, encoded, 1e-5, "ACEScct passes through untouched");
        checkClose(g, encoded, 1e-5, "ACEScct passes through untouched");
        checkClose(b, encoded, 1e-5, "ACEScct passes through untouched");
    }

    // Including the negatives, which a clamping implementation would crush.
    KernelParams linearSpace = defaultParams();
    linearSpace.workingSpace = CM_SPACE_LINEAR;

    float r, g, b;
    process(linearSpace, -0.02f, -0.005f, 0.3f, &r, &g, &b);
    checkClose(r, -0.02, 1e-6, "negative linear survives the log round trip");
    checkClose(g, -0.005, 1e-6, "negative linear survives the log round trip");
    checkClose(b, 0.3, 1e-6, "positive linear survives the log round trip");

    // ACEScc as well.
    KernelParams cc = defaultParams();
    cc.workingSpace = CM_SPACE_ACESCC;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        if (samples[i] <= 0.0) continue;
        const float encoded = cmLinearToACEScc(static_cast<float>(samples[i]));
        process(cc, encoded, encoded, encoded, &r, &g, &b);
        checkClose(r, encoded, 1e-5, "ACEScc passes through untouched");
    }
}

static void testExposure()
{
    section("Exposure");

    for (int stops = -3; stops <= 3; ++stops)
    {
        KernelParams p = defaultParams();
        p.workingSpace = CM_SPACE_LINEAR;

        const float gain = exp2f(static_cast<float>(stops));
        for (int i = 0; i < 9; ++i)
        {
            p.matrix[i] *= gain;
        }

        float r, g, b;
        process(p, 0.18f, 0.18f, 0.18f, &r, &g, &b);

        char label[128];
        snprintf(label, sizeof(label), "%+d EV scales linear by 2^%d", stops, stops);
        checkCloseRelative(r, 0.18 * gain, 1e-6, label);
        checkCloseRelative(g, 0.18 * gain, 1e-6, label);
        checkCloseRelative(b, 0.18 * gain, 1e-6, label);
    }

    // Exposure must survive the encode/decode sandwich too.
    KernelParams p = defaultParams();
    for (int i = 0; i < 9; ++i)
    {
        p.matrix[i] *= 2.0f;
    }

    float r, g, b;
    const float encoded = cmLinearToACEScct(0.18f);
    process(p, encoded, encoded, encoded, &r, &g, &b);
    checkCloseRelative(cmACEScctToLinear(r), 0.36, 1e-5, "+1 EV doubles linear through ACEScct");
}

static void testLensFalloff()
{
    section("Lens falloff");

    const double cx = 0.5 * kFrameWidth;
    const double cy = 0.5 * kFrameHeight;

    // Corners of a 1920x1080 frame in pixel centres.
    const double cornerX[] = { 0.5, kFrameWidth - 0.5, 0.5, kFrameWidth - 0.5 };
    const double cornerY[] = { 0.5, 0.5, kFrameHeight - 0.5, kFrameHeight - 0.5 };

    const double evs[] = { -4.0, -2.0, -0.5, 0.5, 2.0, 4.0 };

    for (int ei = 0; ei < 6; ++ei)
    {
        KernelParams p = defaultParams();
        p.workingSpace = CM_SPACE_LINEAR;
        setVignetteGeometry(p, evs[ei], kFrameWidth, kFrameHeight, 1.0);

        float r, g, b;
        char label[160];

        processAt(p, cx, cy, 0.18f, 0.18f, 0.18f, &r, &g, &b);
        snprintf(label, sizeof(label), "the centre is untouched at %+.1f EV", evs[ei]);
        checkCloseRelative(r, 0.18, 1e-6, label);

        // The outermost pixel centre is half a sample short of the geometric
        // corner, so it lands a shade under the full amount rather than on it.
        const double slack = (fabs(evs[ei]) * 0.005) + 1e-4;
        for (int c = 0; c < 4; ++c)
        {
            processAt(p, cornerX[c], cornerY[c], 0.18f, 0.18f, 0.18f, &r, &g, &b);
            snprintf(label, sizeof(label), "corner %d reaches %+.1f EV", c, evs[ei]);
            checkClose(log2(r / 0.18), evs[ei], slack, label);
        }

        // Grey in means grey out: the falloff is a single gain on all three
        // channels and must not tint anything.
        processAt(p, 0.25 * kFrameWidth, 0.25 * kFrameHeight, 0.18f, 0.18f, 0.18f, &r, &g, &b);
        checkCloseRelative(g, r, 1e-6, "the falloff is neutral across channels");
        checkCloseRelative(b, r, 1e-6, "the falloff is neutral across channels");
    }

    // Off by default, and exactly so: no drift anywhere in the frame.
    {
        KernelParams p = defaultParams();
        p.workingSpace = CM_SPACE_LINEAR;

        for (int i = 0; i <= 16; ++i)
        {
            const double t = static_cast<double>(i) / 16.0;
            float r, g, b;
            processAt(p, t * kFrameWidth, t * kFrameHeight, 0.18f, 0.18f, 0.18f, &r, &g, &b);
            check(r == 0.18f, "0 EV leaves the frame bit-exact");
        }
    }

    // Monotonic from centre to corner, and flat at the centre rather than
    // creased, which is what the r^2 profile is there for.
    {
        KernelParams p = defaultParams();
        p.workingSpace = CM_SPACE_LINEAR;
        setVignetteGeometry(p, -2.0, kFrameWidth, kFrameHeight, 1.0);

        double previous = 1e30;
        for (int i = 0; i <= 64; ++i)
        {
            const double t = static_cast<double>(i) / 64.0;
            float r, g, b;
            processAt(p, cx + t * cx, cy, 0.18f, 0.18f, 0.18f, &r, &g, &b);
            check(r <= previous + 1e-9, "a negative falloff darkens monotonically outwards");
            previous = r;
        }

        float centre, nearCentre;
        float g, b;
        processAt(p, cx, cy, 0.18f, 0.18f, 0.18f, &centre, &g, &b);
        processAt(p, cx + 4.0, cy, 0.18f, 0.18f, 0.18f, &nearCentre, &g, &b);
        checkCloseRelative(nearCentre, centre, 1e-4, "the falloff is flat at the centre");
    }

    // The falloff is circular in real geometry, so a non-square pixel has to
    // stretch it in samples. On a 2:1 anamorphic frame, the point half way to
    // the right edge sits at the same radius as the point that is half way to
    // the top edge scaled by the same physical distance.
    {
        KernelParams square = defaultParams();
        square.workingSpace = CM_SPACE_LINEAR;
        setVignetteGeometry(square, -3.0, 1000.0, 1000.0, 1.0);

        KernelParams wide = defaultParams();
        wide.workingSpace = CM_SPACE_LINEAR;
        setVignetteGeometry(wide, -3.0, 1000.0, 1000.0, 2.0);

        float r0, r1, g, b;

        // A pixel 250 samples right of centre is 250 units away on square
        // pixels and 500 on 2:1 pixels, so it must match the square frame's
        // pixel at 500 samples out once both are put on the same radius.
        processAt(square, 500.0 + 250.0, 500.0, 0.18f, 0.18f, 0.18f, &r0, &g, &b);
        processAt(wide, 500.0 + 250.0, 500.0, 0.18f, 0.18f, 0.18f, &r1, &g, &b);
        check(r1 < r0, "a wide pixel pushes the horizontal falloff in");

        // Vertically nothing changes with pixel aspect except the corner
        // distance the radius is normalised by, so the two differ but stay
        // ordered the same way.
        processAt(square, 500.0, 500.0 + 250.0, 0.18f, 0.18f, 0.18f, &r0, &g, &b);
        processAt(wide, 500.0, 500.0 + 250.0, 0.18f, 0.18f, 0.18f, &r1, &g, &b);
        check(r1 > r0, "a wide pixel stretches the vertical falloff out");
    }
}

static void testContrastPivot()
{
    section("Contrast about the pivot");

    const double pivots[] = { 0.045, 0.18, 0.5, 0.72 };
    const double slopes[] = { 0.25, 0.5, 1.0, 1.5, 2.0, 4.0 };

    for (int pi = 0; pi < 4; ++pi)
    {
        for (int si = 0; si < 6; ++si)
        {
            KernelParams p = defaultParams();
            p.workingSpace = CM_SPACE_LINEAR;
            p.pivot = static_cast<float>(pivots[pi]);
            p.slope = static_cast<float>(slopes[si]);

            float r, g, b;
            process(p, p.pivot, p.pivot, p.pivot, &r, &g, &b);
            checkCloseRelative(r, pivots[pi], 1e-5, "the pivot is a fixed point of the contrast slope");

            // A slope of s must turn n stops from the pivot into n*s stops.
            const float twoStopsUp = static_cast<float>(pivots[pi] * 4.0);
            process(p, twoStopsUp, twoStopsUp, twoStopsUp, &r, &g, &b);
            const double expectedEV = 2.0 * slopes[si];
            checkClose(log2(r / pivots[pi]), expectedEV, 1e-4, "contrast scales distance from the pivot in stops");
        }
    }

    // The tone curve is only defined above zero, so contrast must leave a
    // negative exactly where it found it rather than dragging it anywhere.
    const double negatives[] = { -1.0, -0.18, -0.01, -0.001, -1e-5, -1e-9 };
    for (int si = 0; si < 6; ++si)
    {
        KernelParams p = defaultParams();
        p.workingSpace = CM_SPACE_LINEAR;
        p.slope = static_cast<float>(slopes[si]);

        for (int ni = 0; ni < 6; ++ni)
        {
            const float in = static_cast<float>(negatives[ni]);

            float r, g, b;
            process(p, in, 0.5f, 0.5f, &r, &g, &b);

            char label[160];
            snprintf(label, sizeof(label), "contrast %.2f leaves %g untouched", slopes[si], negatives[ni]);
            check(r == in, label);
        }
    }
}

// The bug this guards against: applying the tone to the magnitude and putting
// the sign back looks like it preserves negatives, but a contrast below 1 or a
// shadow floor pulls magnitudes towards the pivot, which drives a negative away
// from zero instead of towards it. It turned an ACEScct 0.0 into -0.77.
static void testNegativesAreNeverMadeWorse()
{
    section("Negatives");

    const double slopes[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0 };
    const double pivots[] = { 0.045, 0.18, 0.72 };
    const double softnesses[] = { 0.0, 1e-7, 1e-4, 0.01, 0.1, 0.5, 1.0 };

    int negativeFromNonNegative = 0;
    double worstOut = 0.0;
    double worstIn = 0.0;

    for (int si = 0; si < 7; ++si)
    {
        for (int pi = 0; pi < 3; ++pi)
        {
            for (int fi = 0; fi < 7; ++fi)
            {
                for (int shadowOn = 0; shadowOn < 2; ++shadowOn)
                {
                    for (int highlightOn = 0; highlightOn < 2; ++highlightOn)
                    {
                        KernelParams p = defaultParams();
                        p.workingSpace = CM_SPACE_ACESCCT;
                        p.slope = static_cast<float>(slopes[si]);
                        p.pivot = static_cast<float>(pivots[pi]);
                        p.shadowEnable = shadowOn ? 1.0f : 0.0f;
                        p.shadowLimit = -8.0f;
                        p.shadowSoftness = static_cast<float>(softnesses[fi]);
                        p.highlightEnable = highlightOn ? 1.0f : 0.0f;
                        p.highlightLimit = 6.0f;
                        p.highlightSoftness = static_cast<float>(softnesses[fi]);

                        // Every legal ACEScct code value is non-negative, so no
                        // combination of controls may produce a negative one.
                        for (int i = 0; i <= 400; ++i)
                        {
                            const float in = i * (1.46f / 400.0f);

                            float r, g, b;
                            process(p, in, in, in, &r, &g, &b);

                            // 1e-8 of slack for the float rounding in the toe,
                            // where a true zero can land a hair under.
                            if (r < -1e-8f)
                            {
                                ++negativeFromNonNegative;
                                if (r < worstOut)
                                {
                                    worstOut = r;
                                    worstIn = in;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    char label[200];
    snprintf(label, sizeof(label),
             "no control combination turns a legal ACEScct value negative (worst %g from %g)",
             worstOut, worstIn);
    check(negativeFromNonNegative == 0, label);

    // A shadow floor must lift a negative onto the floor, not push it further down.
    for (int fi = 0; fi < 7; ++fi)
    {
        KernelParams p = defaultParams();
        p.workingSpace = CM_SPACE_LINEAR;
        p.shadowEnable = 1.0f;
        p.shadowLimit = -8.0f;
        p.shadowSoftness = static_cast<float>(softnesses[fi]);

        const double floorLinear = 0.18 * exp2(-8.0);
        const double probes[] = { -10.0, -1.0, -0.01, -1e-4, -1e-9, 0.0 };

        for (int ni = 0; ni < 6; ++ni)
        {
            float r, g, b;
            process(p, static_cast<float>(probes[ni]), 0.5f, 0.5f, &r, &g, &b);
            checkCloseRelative(r, floorLinear, 1e-5, "the shadow floor lifts a negative onto the floor");
        }
    }

    // With the floor off, negatives are none of the tone stage's business.
    for (int si = 0; si < 7; ++si)
    {
        KernelParams p = defaultParams();
        p.workingSpace = CM_SPACE_LINEAR;
        p.slope = static_cast<float>(slopes[si]);
        p.highlightEnable = 1.0f;
        p.highlightLimit = 3.0f;
        p.highlightSoftness = 0.3f;

        float r, g, b;
        process(p, -0.05f, 0.5f, 0.5f, &r, &g, &b);
        check(r == -0.05f, "with the floor off a negative passes straight through");
    }

    // Continuity across zero, in both states of the shadow limiter.
    for (int shadowOn = 0; shadowOn < 2; ++shadowOn)
    {
        KernelParams p = defaultParams();
        p.workingSpace = CM_SPACE_LINEAR;
        p.slope = 1.8f;
        p.shadowEnable = shadowOn ? 1.0f : 0.0f;
        p.shadowLimit = -8.0f;
        p.shadowSoftness = 0.5f;

        float rBelow, rAbove, g, b;
        process(p, -1e-7f, 0.5f, 0.5f, &rBelow, &g, &b);
        process(p, 1e-7f, 0.5f, 0.5f, &rAbove, &g, &b);

        checkClose(rAbove - rBelow, 0.0, 1e-5,
                   shadowOn ? "the curve is continuous at zero with the floor on"
                            : "the curve is continuous at zero with the floor off");
    }
}

static void testWhiteBalance()
{
    section("White balance");

    // Zero offset must be exactly the identity matrix.
    float m[9];
    wb::computeMatrix(0.0, 0.0, true, m);
    const float identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    for (int i = 0; i < 9; ++i)
    {
        checkClose(m[i], identity[i], 1e-6, "a zero temperature offset gives the identity matrix");
    }

    // Luminance of a neutral must hold across the whole range.
    const double temps[] = { -6000.0, -4000.0, -2000.0, -500.0, 0.0, 500.0, 2000.0, 4500.0 };
    const double tints[] = { -100.0, -40.0, 0.0, 40.0, 100.0 };

    for (int ti = 0; ti < 8; ++ti)
    {
        for (int ni = 0; ni < 5; ++ni)
        {
            wb::computeMatrix(temps[ti], tints[ni], true, m);

            const double r = m[0] + m[1] + m[2];
            const double g = m[3] + m[4] + m[5];
            const double b = m[6] + m[7] + m[8];

            checkClose(wb::ap1Luminance(r, g, b), 1.0, 1e-5, "preserve exposure holds neutral luminance");
        }
    }

    // Direction: a positive offset means a warmer picture.
    double previousRatio = -1.0;
    for (int ti = 0; ti < 8; ++ti)
    {
        wb::computeMatrix(temps[ti], 0.0, true, m);

        const double r = m[0] + m[1] + m[2];
        const double b = m[6] + m[7] + m[8];
        const double ratio = r / b;

        char label[128];
        snprintf(label, sizeof(label), "raising the offset past %+.0f warms the image", temps[ti]);
        check(ratio > previousRatio, label);
        previousRatio = ratio;
    }

    wb::computeMatrix(-2000.0, 0.0, true, m);
    check((m[0] + m[1] + m[2]) < 1.0, "a negative offset cools the image");
    wb::computeMatrix(2000.0, 0.0, true, m);
    check((m[0] + m[1] + m[2]) > 1.0, "a positive offset warms the image");

    // Direction: positive tint is magenta, so green comes down relative to red and blue.
    wb::computeMatrix(0.0, 50.0, true, m);
    {
        const double r = m[0] + m[1] + m[2];
        const double g = m[3] + m[4] + m[5];
        const double b = m[6] + m[7] + m[8];
        check(g < 1.0 && r > g && b > g, "positive tint pushes towards magenta");
    }
    wb::computeMatrix(0.0, -50.0, true, m);
    {
        const double r = m[0] + m[1] + m[2];
        const double g = m[3] + m[4] + m[5];
        const double b = m[6] + m[7] + m[8];
        check(g > 1.0 && r < g && b < g, "negative tint pushes towards green");
    }

    // Without preservation the luminance is free to move, which is the point of the switch.
    wb::computeMatrix(-3000.0, 0.0, false, m);
    const double unpreserved = wb::ap1Luminance(m[0] + m[1] + m[2], m[3] + m[4] + m[5], m[6] + m[7] + m[8]);
    check(fabs(unpreserved - 1.0) > 1e-4, "without preserve exposure the neutral luminance does move");

    // Every temperature and tint combination has to yield a usable matrix, not
    // just the ones that stay inside the physical region unaided.
    const double extremeTints[] = { -100.0, -75.0, 75.0, 100.0 };
    for (int ti = 0; ti < 8; ++ti)
    {
        for (int ni = 0; ni < 4; ++ni)
        {
            wb::computeMatrix(temps[ti], extremeTints[ni], true, m);

            bool finite = true;
            for (int i = 0; i < 9; ++i)
            {
                if (!std::isfinite(m[i])) finite = false;
            }

            char label[160];
            snprintf(label, sizeof(label), "offset %+.0f at tint %+.0f gives a finite matrix", temps[ti], extremeTints[ni]);
            check(finite, label);

            const double r = m[0] + m[1] + m[2];
            const double g = m[3] + m[4] + m[5];
            const double b = m[6] + m[7] + m[8];

            snprintf(label, sizeof(label), "offset %+.0f at tint %+.0f keeps the neutral gains positive",
                     temps[ti], extremeTints[ni]);
            check(r > 0.0 && g > 0.0 && b > 0.0, label);
        }
    }

    // The neutral reference is the Planckian white at 6000K rather than the ACES
    // white itself, which is what makes the defaults exactly the identity. The
    // two are close but not identical, and how close is worth pinning down.
    double x, y;
    wb::illuminantXY(0.0, 0.0, &x, &y);
    checkClose(x, 0.32168, 0.01, "the neutral reference is near the ACES white x");
    checkClose(y, 0.33767, 0.01, "the neutral reference is near the ACES white y");

    // Distance in CIE 1960 uv, which is the number the documentation quotes.
    const double refDenom = -2.0 * x + 12.0 * y + 3.0;
    const double refU = 4.0 * x / refDenom;
    const double refV = 6.0 * y / refDenom;

    const double acesDenom = -2.0 * 0.32168 + 12.0 * 0.33767 + 3.0;
    const double acesU = 4.0 * 0.32168 / acesDenom;
    const double acesV = 6.0 * 0.33767 / acesDenom;

    const double duv = sqrt((refU - acesU) * (refU - acesU) + (refV - acesV) * (refV - acesV));
    check(duv < 0.004, "the neutral reference is within 0.004 uv of the ACES white");
    printf("  note  neutral reference x %.5f y %.5f is %.5f uv from the ACES white\n", x, y, duv);
}

static void testTemperatureOffsetScale()
{
    section("Temperature offset scale");

    checkClose(wb::temperatureFromOffset(0.0), wb::kReferenceTemperature, 1e-9,
               "a zero offset is the reference temperature");

    // The whole point of the control: equal steps anywhere on the slider are
    // equal steps in mired, which is the scale colour temperature is even on.
    // A Kelvin-linear control would fail this badly at the warm end.
    const double referenceMired = 1.0e6 / wb::kReferenceTemperature;
    double previousStep = -1.0;

    for (double offset = wb::kMinTemperatureOffset; offset <= wb::kMaxTemperatureOffset - 100.0; offset += 100.0)
    {
        const double a = 1.0e6 / wb::temperatureFromOffset(offset);
        const double b = 1.0e6 / wb::temperatureFromOffset(offset + 100.0);
        const double step = fabs(a - b);

        checkClose(step, 100.0 * wb::kMiredPerOffsetUnit, 1e-6,
                   "every 100 unit step is the same size in mired");

        if (previousStep >= 0.0)
        {
            checkClose(step, previousStep, 1e-6, "the step size never changes along the slider");
        }
        previousStep = step;
    }

    // Near the reference the units really are Kelvin, which is what makes the
    // number worth showing at all.
    checkClose(wb::temperatureFromOffset(100.0) - wb::kReferenceTemperature, 100.0, 2.0,
               "100 units is about 100 K near the reference");
    checkClose(wb::temperatureFromOffset(-100.0) - wb::kReferenceTemperature, -100.0, 2.0,
               "-100 units is about -100 K near the reference");

    // Both ends must land inside the Planckian fit, or the last stretch of
    // travel would do nothing at all.
    const double warmest = wb::temperatureFromOffset(wb::kMinTemperatureOffset);
    const double coolest = wb::temperatureFromOffset(wb::kMaxTemperatureOffset);

    check(warmest > wb::kMinTemperature, "the warm end stays inside the Planckian fit");
    check(coolest < wb::kMaxTemperature, "the cool end stays inside the Planckian fit");
    check(warmest <= 3200.0, "the warm end reaches at least tungsten");
    check(coolest >= 20000.0, "the cool end reaches well past daylight");

    // No dead travel: a step at either extreme still moves the illuminant.
    const double warmNeighbour = wb::temperatureFromOffset(wb::kMinTemperatureOffset + 100.0);
    const double coolNeighbour = wb::temperatureFromOffset(wb::kMaxTemperatureOffset - 100.0);
    check(fabs(warmNeighbour - warmest) > 1.0, "the last step at the warm end still does something");
    check(fabs(coolNeighbour - coolest) > 1.0, "the last step at the cool end still does something");

    // Monotonic, and warmer as the number rises.
    double previous = 0.0;
    for (double offset = wb::kMinTemperatureOffset; offset <= wb::kMaxTemperatureOffset; offset += 50.0)
    {
        const double t = wb::temperatureFromOffset(offset);
        check(t > previous, "the assumed temperature rises with the offset");
        previous = t;
    }

    printf("  note  offset %+.0f is %.0fK, offset %+.0f is %.0fK, one step is %.3f mired\n",
           wb::kMinTemperatureOffset, warmest, wb::kMaxTemperatureOffset, coolest,
           100.0 * wb::kMiredPerOffsetUnit);
    (void)referenceMired;
}

static void testLimiters()
{
    section("Limiters");

    const double limits[] = { 1.0, 3.0, 6.0, 8.0 };
    const double softnesses[] = { 0.0, 0.1, 0.25, 0.5, 0.75, 1.0 };

    for (int li = 0; li < 4; ++li)
    {
        for (int si = 0; si < 6; ++si)
        {
            const float limit = static_cast<float>(limits[li]);
            const float soft = static_cast<float>(softnesses[si]);
            const float knee = limit * (1.0f - soft);

            char label[160];

            // Never exceeds the limit, and rises monotonically towards it.
            float previous = -1e9f;
            for (int i = 0; i <= 400; ++i)
            {
                const float e = -2.0f + (30.0f * i) / 400.0f;
                const float y = cmSoftClipHigh(e, limit, soft);

                snprintf(label, sizeof(label), "highlight limiter stays at or below %.1f EV", limits[li]);
                check(y <= limit + 1e-5f, label);

                snprintf(label, sizeof(label), "highlight limiter is monotonic at limit %.1f softness %.2f",
                         limits[li], softnesses[si]);
                check(y >= previous - 1e-6f, label);
                previous = y;

                // Below the knee it must be a straight pass through.
                if (e < knee - 0.01f)
                {
                    snprintf(label, sizeof(label), "highlight limiter is untouched below the knee");
                    checkClose(y, e, 1e-5, label);
                }
            }

            // Mirror image for the shadow side.
            previous = 1e9f;
            for (int i = 0; i <= 400; ++i)
            {
                const float e = 2.0f - (30.0f * i) / 400.0f;
                const float y = cmSoftClipLow(e, -limit, soft);

                snprintf(label, sizeof(label), "shadow limiter stays at or above %.1f EV", -limits[li]);
                check(y >= -limit - 1e-5f, label);

                snprintf(label, sizeof(label), "shadow limiter is monotonic at limit %.1f softness %.2f",
                         limits[li], softnesses[si]);
                check(y <= previous + 1e-6f, label);
                previous = y;
            }

            // The two sides must be exact reflections of each other.
            for (int i = 0; i <= 50; ++i)
            {
                const float e = (12.0f * i) / 50.0f - 2.0f;
                checkClose(cmSoftClipLow(-e, -limit, soft), -cmSoftClipHigh(e, limit, soft), 1e-6,
                           "the shadow limiter mirrors the highlight limiter");
            }

            if (soft > 0.0f)
            {
                // C1 continuity: the one sided slopes at the knee must agree.
                const float h = 1e-3f;
                const float slopeBelow = (cmSoftClipHigh(knee - h, limit, soft) - cmSoftClipHigh(knee - 2 * h, limit, soft)) / h;
                const float slopeAbove = (cmSoftClipHigh(knee + 2 * h, limit, soft) - cmSoftClipHigh(knee + h, limit, soft)) / h;

                snprintf(label, sizeof(label), "highlight knee is smooth at limit %.1f softness %.2f",
                         limits[li], softnesses[si]);
                checkClose(slopeBelow, 1.0, 1e-3, label);
                checkClose(slopeAbove, 1.0, 5e-3, label);
            }
            else
            {
                // Zero softness is a hard clip.
                checkClose(cmSoftClipHigh(limit + 5.0f, limit, 0.0f), limits[li], 1e-6, "zero softness clips hard");
                checkClose(cmSoftClipHigh(limit - 0.5f, limit, 0.0f), limits[li] - 0.5, 1e-6, "zero softness is untouched below the limit");
            }
        }
    }

    // Through the whole pipeline: a very bright input must land under the ceiling.
    KernelParams p = defaultParams();
    p.workingSpace = CM_SPACE_LINEAR;
    p.highlightEnable = 1.0f;
    p.highlightLimit = 4.0f;
    p.highlightSoftness = 0.6f;
    p.shadowEnable = 1.0f;
    p.shadowLimit = -5.0f;
    p.shadowSoftness = 0.6f;

    float r, g, b;
    process(p, 10000.0f, 10000.0f, 10000.0f, &r, &g, &b);
    check(r <= 0.18f * exp2f(4.0f) + 1e-3f, "the pipeline respects the highlight ceiling");

    process(p, 1e-7f, 1e-7f, 1e-7f, &r, &g, &b);
    check(r >= 0.18f * exp2f(-5.0f) - 1e-6f, "the pipeline respects the shadow floor");

    // The pivot itself must be immune to the limiters.
    process(p, 0.18f, 0.18f, 0.18f, &r, &g, &b);
    checkCloseRelative(r, 0.18, 1e-5, "the limiters leave the pivot alone");
}

static void testChannelIndependence()
{
    section("Plumbing");

    // Alpha is handled by the caller, but the three colour channels must not
    // leak into one another beyond the balance matrix.
    KernelParams p = defaultParams();
    p.workingSpace = CM_SPACE_LINEAR;
    p.slope = 1.7f;
    p.highlightEnable = 1.0f;
    p.highlightLimit = 3.0f;
    p.highlightSoftness = 0.4f;

    float r0, g0, b0, r1, g1, b1;
    process(p, 0.5f, 0.25f, 0.1f, &r0, &g0, &b0);
    process(p, 0.5f, 0.9f, 0.9f, &r1, &g1, &b1);
    checkClose(r0, r1, 1e-6, "the red result depends only on red when the matrix is diagonal");

    // The parameter block really is a flat float array in the order the kernel reads.
    check(sizeof(KernelParams) == kNumKernelParams * sizeof(float), "KernelParams is a flat float block");

    const float* raw = reinterpret_cast<const float*>(&p);
    check(raw[9] == p.pivot, "pivot sits at index 9");
    check(raw[10] == p.slope, "slope sits at index 10");
    check(raw[11] == p.shadowEnable, "shadow enable sits at index 11");
    check(raw[14] == p.highlightEnable, "highlight enable sits at index 14");
    check(raw[17] == p.workingSpace, "working space sits at index 17");
}

int main()
{
    printf("\nTechnical Grade pipeline checks\n\n");

    testTransferFunctions();
    testIdentity();
    testExposure();
    testLensFalloff();
    testContrastPivot();
    testNegativesAreNeverMadeWorse();
    testWhiteBalance();
    testTemperatureOffsetScale();
    testLimiters();
    testChannelIndependence();

    printf("\n%d checks, %d failures\n\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
