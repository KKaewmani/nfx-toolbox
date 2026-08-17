// Portable colour maths shared verbatim by the CPU path and the Metal kernel.
//
// This file is compiled as C++ (via ColorMath.h) and is also wrapped into a raw
// string literal at build time (ColorMathSource.h) so the Metal kernel compiles
// the exact same text. It must therefore stay inside the intersection of C++ and
// the Metal Shading Language:
//
//   - no #include, no namespace, no templates, no std::
//   - every maths call goes through the CM_* macros defined by the two preludes
//   - float only, never double
//
// Working space identifiers. Kept as floats in the parameter block so the whole
// block is a flat float array, but only ever compared against these exact values.
#define CM_SPACE_ACESCCT 0
#define CM_SPACE_ACESCC  1
#define CM_SPACE_LINEAR  2
#define CM_SPACE_LOGC3   3
#define CM_SPACE_LOGC4   4
#define CM_SPACE_SLOG3   5
#define CM_SPACE_CLOG3   6
#define CM_SPACE_LOG3G10 7

// ACES log encoding constants, shared by ACEScc and ACEScct.
#define CM_LOG_SLOPE   17.52f
#define CM_LOG_OFFSET  9.72f

// ACEScct pseudo-log toe.
#define CM_CCT_A      10.5402377416545f
#define CM_CCT_B      0.0729055341958355f
#define CM_CCT_X_BRK  0.0078125f
#define CM_CCT_Y_BRK  0.155251141552511f

// (log2(65504) + 9.72) / 17.52: the encoded value of the half-float ceiling,
// where both ACEScc and ACEScct saturate.
#define CM_CEIL_ENC   1.4679963120447153f
#define CM_CEIL_LIN   65504.0f

// ACEScc has no toe; below this encoded value it uses an offset log segment.
// (9.72 - 15) / 17.52
#define CM_CC_BRK    -0.3013698630136986f

// ARRI LogC3, EI 800 only. ALEXA Log C Curve – Usage in VFX.
#define CM_LC3_A      5.555556f
#define CM_LC3_B      0.052272f
#define CM_LC3_C      0.247190f
#define CM_LC3_D      0.385537f
#define CM_LC3_E      5.367655f
#define CM_LC3_F      0.092809f
#define CM_LC3_CUT    0.010591f
#define CM_LC3_YCUT   0.149657834105f

// ARRI LogC4. a = (2^18 - 16) / 117.45, b = (1023-95)/1023, c = 95/1023.
#define CM_C4_A       2231.8263090676883f
#define CM_C4_B       0.9071358748778103f
#define CM_C4_C       0.09286412512218964f
#define CM_C4_S       0.1135972086105891f
#define CM_C4_T      -0.01805699611991131f

// Sony S-Log3, full range. Technical summary PDF.
#define CM_SL3_LIN_BRK  0.01125f
#define CM_SL3_LOG_OFF  0.01f
#define CM_SL3_GREY     0.18f
#define CM_SL3_B        261.5f
#define CM_SL3_C        420.0f
#define CM_SL3_D        95.0f
#define CM_SL3_N        1023.0f
#define CM_SL3_ENC_BRK  0.16736099187966763f
#define CM_SL3_TOE      171.2102946929f

// Canon C-Log3. Canon's x is IRE-style (reflection / 0.9); the plugin's linear
// is scene reflection, so decode multiplies by 0.9 and encode divides by it.
#define CM_CL3_A      0.36726845f
#define CM_CL3_B      14.98325f
#define CM_CL3_C      0.12783901f
#define CM_CL3_D      1.9754798f
#define CM_CL3_E      0.12512219f
#define CM_CL3_F      0.12240537f
#define CM_CL3_XBRK   0.014f
#define CM_CL3_YLO    0.097465473f
#define CM_CL3_YHI    0.15277891f
#define CM_CL3_IRE    0.9f

// RED Log3G10, 915-0187 Rev-C.
#define CM_L3G10_A    0.224282f
#define CM_L3G10_B    155.975327f
#define CM_L3G10_C    0.01f
#define CM_L3G10_G    15.1927f

// Smallest linear value fed to log2. 1e-10 sits around -33 EV, far below any
// limiter range, so it only ever catches zeroes and out-of-gamut negatives.
#define CM_LIN_EPS    1e-10f

// Below this the soft-clip knee has no width left and degenerates to a hard clip.
#define CM_KNEE_EPS   1e-6f

// ---------------------------------------------------------------------------
// Working space transfer functions
// ---------------------------------------------------------------------------

CM_INLINE float cmACEScctToLinear(float x)
{
    if (x <= CM_CCT_Y_BRK)
    {
        return (x - CM_CCT_B) / CM_CCT_A;
    }
    if (x < CM_CEIL_ENC)
    {
        return CM_EXP2(x * CM_LOG_SLOPE - CM_LOG_OFFSET);
    }
    return CM_CEIL_LIN;
}

CM_INLINE float cmLinearToACEScct(float x)
{
    if (x <= CM_CCT_X_BRK)
    {
        return CM_CCT_A * x + CM_CCT_B;
    }
    return (CM_LOG2(x) + CM_LOG_OFFSET) / CM_LOG_SLOPE;
}

CM_INLINE float cmACESccToLinear(float x)
{
    if (x < CM_CC_BRK)
    {
        return (CM_EXP2(x * CM_LOG_SLOPE - CM_LOG_OFFSET) - CM_EXP2(-16.0f)) * 2.0f;
    }
    if (x < CM_CEIL_ENC)
    {
        return CM_EXP2(x * CM_LOG_SLOPE - CM_LOG_OFFSET);
    }
    return CM_CEIL_LIN;
}

CM_INLINE float cmLinearToACEScc(float x)
{
    if (x <= 0.0f)
    {
        return (-16.0f + CM_LOG_OFFSET) / CM_LOG_SLOPE;
    }
    if (x < CM_EXP2(-15.0f))
    {
        return (CM_LOG2(CM_EXP2(-16.0f) + x * 0.5f) + CM_LOG_OFFSET) / CM_LOG_SLOPE;
    }
    return (CM_LOG2(x) + CM_LOG_OFFSET) / CM_LOG_SLOPE;
}

CM_INLINE float cmLogC3ToLinear(float x)
{
    if (x > CM_LC3_YCUT)
    {
        return (CM_POW10((x - CM_LC3_D) / CM_LC3_C) - CM_LC3_B) / CM_LC3_A;
    }
    return (x - CM_LC3_F) / CM_LC3_E;
}

CM_INLINE float cmLinearToLogC3(float x)
{
    if (x > CM_LC3_CUT)
    {
        return CM_LC3_C * CM_LOG10(CM_LC3_A * x + CM_LC3_B) + CM_LC3_D;
    }
    return CM_LC3_E * x + CM_LC3_F;
}

CM_INLINE float cmLogC4ToLinear(float x)
{
    if (x < 0.0f)
    {
        return x * CM_C4_S + CM_C4_T;
    }
    return (CM_EXP2(14.0f * (x - CM_C4_C) / CM_C4_B + 6.0f) - 64.0f) / CM_C4_A;
}

CM_INLINE float cmLinearToLogC4(float x)
{
    if (x < CM_C4_T)
    {
        return (x - CM_C4_T) / CM_C4_S;
    }
    return ((CM_LOG2(CM_C4_A * x + 64.0f) - 6.0f) / 14.0f) * CM_C4_B + CM_C4_C;
}

CM_INLINE float cmSLog3ToLinear(float x)
{
    if (x >= CM_SL3_ENC_BRK)
    {
        return CM_POW10((x * CM_SL3_N - CM_SL3_C) / CM_SL3_B) * (CM_SL3_GREY + CM_SL3_LOG_OFF) - CM_SL3_LOG_OFF;
    }
    return (x * CM_SL3_N - CM_SL3_D) * CM_SL3_LIN_BRK / (CM_SL3_TOE - CM_SL3_D);
}

CM_INLINE float cmLinearToSLog3(float x)
{
    if (x >= CM_SL3_LIN_BRK)
    {
        return (CM_SL3_C + CM_LOG10((x + CM_SL3_LOG_OFF) / (CM_SL3_GREY + CM_SL3_LOG_OFF)) * CM_SL3_B) / CM_SL3_N;
    }
    return (x * (CM_SL3_TOE - CM_SL3_D) / CM_SL3_LIN_BRK + CM_SL3_D) / CM_SL3_N;
}

CM_INLINE float cmCLog3ToLinear(float y)
{
    float x;
    if (y < CM_CL3_YLO)
    {
        x = -(CM_POW10((CM_CL3_C - y) / CM_CL3_A) - 1.0f) / CM_CL3_B;
    }
    else if (y <= CM_CL3_YHI)
    {
        x = (y - CM_CL3_E) / CM_CL3_D;
    }
    else
    {
        x = (CM_POW10((y - CM_CL3_F) / CM_CL3_A) - 1.0f) / CM_CL3_B;
    }
    return CM_CL3_IRE * x;
}

CM_INLINE float cmLinearToCLog3(float lin)
{
    const float x = lin / CM_CL3_IRE;
    if (x < -CM_CL3_XBRK)
    {
        return -CM_CL3_A * CM_LOG10(1.0f - CM_CL3_B * x) + CM_CL3_C;
    }
    if (x > CM_CL3_XBRK)
    {
        return CM_CL3_A * CM_LOG10(CM_CL3_B * x + 1.0f) + CM_CL3_F;
    }
    return CM_CL3_D * x + CM_CL3_E;
}

CM_INLINE float cmLog3G10ToLinear(float y)
{
    if (y < 0.0f)
    {
        return y / CM_L3G10_G - CM_L3G10_C;
    }
    return (CM_POW10(y / CM_L3G10_A) - 1.0f) / CM_L3G10_B - CM_L3G10_C;
}

CM_INLINE float cmLinearToLog3G10(float lin)
{
    const float x = lin + CM_L3G10_C;
    if (x < 0.0f)
    {
        return x * CM_L3G10_G;
    }
    return CM_L3G10_A * CM_LOG10(x * CM_L3G10_B + 1.0f);
}

CM_INLINE float cmDecode(float x, float space)
{
    if (space == (float)CM_SPACE_ACESCCT)
    {
        return cmACEScctToLinear(x);
    }
    if (space == (float)CM_SPACE_ACESCC)
    {
        return cmACESccToLinear(x);
    }
    if (space == (float)CM_SPACE_LOGC3)
    {
        return cmLogC3ToLinear(x);
    }
    if (space == (float)CM_SPACE_LOGC4)
    {
        return cmLogC4ToLinear(x);
    }
    if (space == (float)CM_SPACE_SLOG3)
    {
        return cmSLog3ToLinear(x);
    }
    if (space == (float)CM_SPACE_CLOG3)
    {
        return cmCLog3ToLinear(x);
    }
    if (space == (float)CM_SPACE_LOG3G10)
    {
        return cmLog3G10ToLinear(x);
    }
    return x;
}

CM_INLINE float cmEncode(float x, float space)
{
    if (space == (float)CM_SPACE_ACESCCT)
    {
        return cmLinearToACEScct(x);
    }
    if (space == (float)CM_SPACE_ACESCC)
    {
        return cmLinearToACEScc(x);
    }
    if (space == (float)CM_SPACE_LOGC3)
    {
        return cmLinearToLogC3(x);
    }
    if (space == (float)CM_SPACE_LOGC4)
    {
        return cmLinearToLogC4(x);
    }
    if (space == (float)CM_SPACE_SLOG3)
    {
        return cmLinearToSLog3(x);
    }
    if (space == (float)CM_SPACE_CLOG3)
    {
        return cmLinearToCLog3(x);
    }
    if (space == (float)CM_SPACE_LOG3G10)
    {
        return cmLinearToLog3G10(x);
    }
    return x;
}

// ---------------------------------------------------------------------------
// Limiters
// ---------------------------------------------------------------------------
//
// Both operate in EV relative to the pivot, so the pivot sits at 0. The knee
// begins at t = limit * (1 - softness): softness 0 would put the knee on the
// limit and clip hard, softness 1 pulls it back to the pivot. The interface
// stops short of 0 so there is always a knee of some width, but the hard clip
// branch stays here to cover a limit set at or very near the pivot, where the
// knee has nowhere to live whatever the softness.
//
// Past the knee the curve is
//
//   y = t + (limit - t) * tanh((e - t) / (limit - t))
//
// whose derivative is exactly 1 at t, making the join C1 continuous, and which
// approaches the limit asymptotically without ever reaching it.

CM_INLINE float cmSoftClipHigh(float e, float limit, float softness)
{
    const float knee = limit * (1.0f - softness);
    const float width = limit - knee;
    if (width <= CM_KNEE_EPS)
    {
        return CM_MIN(e, limit);
    }
    if (e <= knee)
    {
        return e;
    }
    return knee + width * CM_TANH((e - knee) / width);
}

CM_INLINE float cmSoftClipLow(float e, float limit, float softness)
{
    const float knee = limit * (1.0f - softness);
    const float width = knee - limit;
    if (width <= CM_KNEE_EPS)
    {
        return CM_MAX(e, limit);
    }
    if (e >= knee)
    {
        return e;
    }
    return knee - width * CM_TANH((knee - e) / width);
}

// ---------------------------------------------------------------------------
// Tone: contrast about the pivot followed by the two limiters
// ---------------------------------------------------------------------------

// log2 needs a positive argument, but AP1 legitimately carries small negatives
// in deep shadows and outside the gamut, so they have to be dealt with rather
// than clamped away.
//
// The rule is that a value at or below zero is darker than any floor the shadow
// limiter can set, because every floor is a positive linear value. So either the
// limiter is on and the value lands on the floor, or it is off and the value
// passes through untouched. Nothing in between, and in particular the tone curve
// never runs on a negative.
//
// Both branches agree with the limit of the positive branch as it approaches
// zero, so the whole operator stays continuous and monotonic across zero:
//
//   limiter off:  pivot * 2^(log2(v/pivot) * slope) -> 0 as v -> 0+
//   limiter on:   the low clip is asymptotic to shadowLimit, so the result
//                 tends to pivot * 2^shadowLimit, which is the floor itself
//
// Mirroring the curve through the origin instead, which is the obvious way to
// keep the sign, is wrong: it applies the tone to the magnitude, so anything
// that pulls magnitudes towards the pivot, such as a contrast below 1 or the
// shadow floor, drives negatives further from zero rather than towards it.

CM_INLINE float cmTone(float lin, float pivot, float slope,
                       float shadowEnable, float shadowLimit, float shadowSoftness,
                       float highlightEnable, float highlightLimit, float highlightSoftness)
{
    if (lin <= 0.0f)
    {
        return (shadowEnable > 0.5f) ? (pivot * CM_EXP2(shadowLimit)) : lin;
    }

    float e = CM_LOG2(CM_MAX(lin, CM_LIN_EPS) / pivot) * slope;

    if (shadowEnable > 0.5f)
    {
        e = cmSoftClipLow(e, shadowLimit, shadowSoftness);
    }
    if (highlightEnable > 0.5f)
    {
        e = cmSoftClipHigh(e, highlightLimit, highlightSoftness);
    }

    return pivot * CM_EXP2(e);
}

// ---------------------------------------------------------------------------
// Lens falloff
// ---------------------------------------------------------------------------
//
// A pure exposure gain that varies with distance from the centre of the frame:
// nothing at the centre, the full amount set by the control at the far corners.
//
//   gain(r) = 2^(EV * r^2),  r = 0 at the centre and 1 at the corners
//
// The r^2 profile is not arbitrary. Natural vignetting follows the cos^4 law,
// and for the field angles a real lens works over, log2(cos^4 t) is proportional
// to r^2 to second order. So this is the physical falloff curve near the centre,
// and it also has the property that matters visually: zero slope at the centre,
// meaning no crease in the middle of frame the way a profile linear in r gives.
//
// The scale factors carry the pixel aspect ratio, so r is measured in real
// geometry and the falloff stays circular on a non-square-pixel or very wide
// frame, which is how a lens actually behaves.

CM_INLINE float cmVignetteGain(float x, float y, CM_PARAM_PTR float* p)
{
    const float ev = p[18];
    if (ev == 0.0f)
    {
        return 1.0f;
    }

    const float dx = (x - p[19]) * p[21];
    const float dy = (y - p[20]) * p[22];

    return CM_EXP2(ev * (dx * dx + dy * dy));
}

// ---------------------------------------------------------------------------
// The whole per-pixel pipeline
// ---------------------------------------------------------------------------
//
// x and y are pixel centres measured from the corner of the image, not of the
// render window, so the falloff is anchored to the frame.
//
// p is the flat parameter block described by KernelParams.h:
//   [0..8]   exposure-premultiplied white balance matrix, row major, in AP1
//   [9]      pivot
//   [10]     contrast slope
//   [11..13] shadow enable, limit, softness
//   [14..16] highlight enable, limit, softness
//   [17]     working space
//   [18]     lens falloff EV at the corners
//   [19..20] frame centre in pixels
//   [21..22] scale factors that put r at 1 in the corners
//   [23..31] camera linear RGB -> AP1
//   [32..40] AP1 -> camera linear RGB

CM_INLINE void cmProcessPixel(float inR, float inG, float inB, float x, float y,
                              CM_PARAM_PTR float* p,
                              CM_THREAD float* outR, CM_THREAD float* outG, CM_THREAD float* outB)
{
    const float space = p[17];

    const float lr = cmDecode(inR, space);
    const float lg = cmDecode(inG, space);
    const float lb = cmDecode(inB, space);

    // Camera RGB is converted to AP1 before the grade so white balance, which
    // is an AP1 Bradford CAT, and any future AP1 operator see the same space.
    // The tone curve is nonlinear, so the return matrix cannot be folded in.
    const float ar = p[23] * lr + p[24] * lg + p[25] * lb;
    const float ag = p[26] * lr + p[27] * lg + p[28] * lb;
    const float ab = p[29] * lr + p[30] * lg + p[31] * lb;

    // Exposure and white balance in one matrix multiply. The falloff belongs
    // between the two, but it is a single scalar applied to all three channels
    // and a scalar commutes with a matrix, so folding it in here is the same
    // arithmetic for a third of the work.
    const float vignette = cmVignetteGain(x, y, p);

    const float mr = (p[0] * ar + p[1] * ag + p[2] * ab) * vignette;
    const float mg = (p[3] * ar + p[4] * ag + p[5] * ab) * vignette;
    const float mb = (p[6] * ar + p[7] * ag + p[8] * ab) * vignette;

    const float pivot = p[9];
    const float slope = p[10];

    const float tr = cmTone(mr, pivot, slope, p[11], p[12], p[13], p[14], p[15], p[16]);
    const float tg = cmTone(mg, pivot, slope, p[11], p[12], p[13], p[14], p[15], p[16]);
    const float tb = cmTone(mb, pivot, slope, p[11], p[12], p[13], p[14], p[15], p[16]);

    const float cr = p[32] * tr + p[33] * tg + p[34] * tb;
    const float cg = p[35] * tr + p[36] * tg + p[37] * tb;
    const float cb = p[38] * tr + p[39] * tg + p[40] * tb;

    *outR = cmEncode(cr, space);
    *outG = cmEncode(cg, space);
    *outB = cmEncode(cb, space);
}
