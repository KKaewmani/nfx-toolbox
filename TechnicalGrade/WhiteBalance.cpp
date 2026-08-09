#include "WhiteBalance.h"

#include <cmath>

namespace
{
    // ACES AP1 to CIE XYZ, D60/ACES white. The second row doubles as the
    // luminance weights.
    const double kAP1ToXYZ[9] = {
         0.6624541811,  0.1340042065,  0.1561876870,
         0.2722287168,  0.6740817658,  0.0536895174,
        -0.0055746495,  0.0040607335,  1.0103391003
    };

    // Bradford cone response, the sharpened basis the von Kries scaling happens in.
    const double kBradford[9] = {
         0.8951,  0.2664, -0.1614,
        -0.7502,  1.7135,  0.0367,
         0.0389, -0.0685,  1.0296
    };

    void matMul(const double a[9], const double b[9], double out[9])
    {
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c)
            {
                out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c]
                               + a[r * 3 + 1] * b[1 * 3 + c]
                               + a[r * 3 + 2] * b[2 * 3 + c];
            }
        }
    }

    void matVec(const double m[9], const double v[3], double out[3])
    {
        for (int r = 0; r < 3; ++r)
        {
            out[r] = m[r * 3 + 0] * v[0] + m[r * 3 + 1] * v[1] + m[r * 3 + 2] * v[2];
        }
    }

    // Inverting numerically rather than pasting a second set of published
    // constants keeps the forward and inverse matrices exactly consistent.
    void matInverse(const double m[9], double out[9])
    {
        const double c00 =  (m[4] * m[8] - m[5] * m[7]);
        const double c01 = -(m[3] * m[8] - m[5] * m[6]);
        const double c02 =  (m[3] * m[7] - m[4] * m[6]);

        const double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
        const double inv = 1.0 / det;

        out[0] = c00 * inv;
        out[3] = c01 * inv;
        out[6] = c02 * inv;

        out[1] = -(m[1] * m[8] - m[2] * m[7]) * inv;
        out[4] =  (m[0] * m[8] - m[2] * m[6]) * inv;
        out[7] = -(m[0] * m[7] - m[1] * m[6]) * inv;

        out[2] =  (m[1] * m[5] - m[2] * m[4]) * inv;
        out[5] = -(m[0] * m[5] - m[2] * m[3]) * inv;
        out[8] =  (m[0] * m[4] - m[1] * m[3]) * inv;
    }

    double clampd(double v, double lo, double hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Kim et al. cubic fit to the Planckian locus, valid 1667K to 25000K.
    void planckianXY(double t, double* outX, double* outY)
    {
        t = clampd(t, wb::kMinTemperature, wb::kMaxTemperature);

        const double invT = 1.0 / t;
        const double invT2 = invT * invT;
        const double invT3 = invT2 * invT;

        double x;
        if (t <= 4000.0)
        {
            x = -0.2661239e9 * invT3 - 0.2343589e6 * invT2 + 0.8776956e3 * invT + 0.179910;
        }
        else
        {
            x = -3.0258469e9 * invT3 + 2.1070379e6 * invT2 + 0.2226347e3 * invT + 0.240390;
        }

        const double x2 = x * x;
        const double x3 = x2 * x;

        double y;
        if (t <= 2222.0)
        {
            y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
        }
        else if (t <= 4000.0)
        {
            y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
        }
        else
        {
            y =  3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
        }

        *outX = x;
        *outY = y;
    }

    void xyToUV(double x, double y, double* u, double* v)
    {
        const double d = -2.0 * x + 12.0 * y + 3.0;
        *u = 4.0 * x / d;
        *v = 6.0 * y / d;
    }

    void uvToXY(double u, double v, double* x, double* y)
    {
        const double d = 2.0 * u - 8.0 * v + 4.0;
        *x = 3.0 * u / d;
        *y = 2.0 * v / d;
    }

    // Unit normal to the Planckian locus in CIE 1960 uv, oriented so that
    // positive duv always moves towards green.
    void locusNormal(double t, double* outNU, double* outNV)
    {
        const double dt = 1.0;
        double xa, ya, xb, yb;
        planckianXY(clampd(t - dt, wb::kMinTemperature, wb::kMaxTemperature), &xa, &ya);
        planckianXY(clampd(t + dt, wb::kMinTemperature, wb::kMaxTemperature), &xb, &yb);

        double ua, va, ub, vb;
        xyToUV(xa, ya, &ua, &va);
        xyToUV(xb, yb, &ub, &vb);

        const double tu = ub - ua;
        const double tv = vb - va;
        const double len = sqrt(tu * tu + tv * tv);

        if (len <= 0.0)
        {
            *outNU = 0.0;
            *outNV = 0.0;
            return;
        }

        double nu = -tv / len;
        double nv =  tu / len;

        // Green is the high-v side of the locus.
        if (nv < 0.0)
        {
            nu = -nu;
            nv = -nv;
        }

        *outNU = nu;
        *outNV = nv;
    }

    // A chromaticity is only usable as a white point if all three XYZ tristimulus
    // values are positive. Far off the locus, and especially down at 1667K where
    // the locus already runs close to the spectral edge, a large tint would
    // otherwise produce a negative Z and a nonsensical adaptation matrix.
    bool physicalChromaticity(double x, double y)
    {
        const double margin = 0.02;
        return (x > margin) && (y > margin) && ((1.0 - x - y) > margin);
    }

    void offsetXY(double baseU, double baseV, double nu, double nv, double duv, double* x, double* y)
    {
        uvToXY(baseU + nu * duv, baseV + nv * duv, x, y);
    }

    // Chromaticity of the assumed illuminant: the Planckian white at t, pushed
    // off the locus by duv along the locus normal in CIE 1960 uv.
    void illuminantUV(double t, double duv, double* outU, double* outV)
    {
        double px, py;
        planckianXY(t, &px, &py);

        double u, v;
        xyToUV(px, py, &u, &v);

        if (duv != 0.0)
        {
            double nu, nv;
            locusNormal(t, &nu, &nv);

            double x, y;
            offsetXY(u, v, nu, nv, duv, &x, &y);

            if (!physicalChromaticity(x, y))
            {
                // Pull the offset back towards the locus until the white point is
                // usable again. Bisection keeps the direction of the tint and just
                // limits how far it can travel.
                double lo = 0.0;            // always valid, this is the locus itself
                double hi = duv;            // requested, currently invalid
                for (int i = 0; i < 40; ++i)
                {
                    const double mid = 0.5 * (lo + hi);
                    offsetXY(u, v, nu, nv, mid, &x, &y);
                    if (physicalChromaticity(x, y))
                    {
                        lo = mid;
                    }
                    else
                    {
                        hi = mid;
                    }
                }
                duv = lo;
            }

            u += nu * duv;
            v += nv * duv;
        }

        *outU = u;
        *outV = v;
    }

    void whiteXYZ(double t, double duv, double out[3])
    {
        double u, v;
        illuminantUV(t, duv, &u, &v);

        double x, y;
        uvToXY(u, v, &x, &y);

        // Normalised to Y = 1.
        out[0] = x / y;
        out[1] = 1.0;
        out[2] = (1.0 - x - y) / y;
    }
}

namespace wb
{
    double temperatureFromOffset(double offset)
    {
        // Walk along the mired scale rather than the Kelvin one so every step is
        // the same size in colour terms, then clamp to the range the Planckian
        // fit is defined over.
        const double mired = 1.0e6 / kReferenceTemperature - offset * kMiredPerOffsetUnit;
        const double clamped = clampd(mired, 1.0e6 / kMaxTemperature, 1.0e6 / kMinTemperature);
        return 1.0e6 / clamped;
    }

    void illuminantXY(double temperatureOffset, double tint, double* outX, double* outY)
    {
        double u, v;
        illuminantUV(temperatureFromOffset(temperatureOffset), tint * kTintToDuv, &u, &v);
        uvToXY(u, v, outX, outY);
    }

    double ap1Luminance(double r, double g, double b)
    {
        return kAP1ToXYZ[3] * r + kAP1ToXYZ[4] * g + kAP1ToXYZ[5] * b;
    }

    void computeMatrix(double temperatureOffset, double tint, bool preserveExposure, float outMatrix[9])
    {
        if ((temperatureOffset == 0.0) && (tint == 0.0))
        {
            // Short circuit to a bit exact identity. Running the neutral case
            // through the matrix chain leaves about 1e-9 of residue off the
            // diagonal, and that is enough to leak a hair of one channel into
            // another and turn a true zero into a small positive, which then
            // takes the wrong branch of the tone curve.
            for (int i = 0; i < 9; ++i)
            {
                outMatrix[i] = (i % 4 == 0) ? 1.0f : 0.0f;
            }
            return;
        }

        const double duv = tint * kTintToDuv;

        double srcXYZ[3], refXYZ[3];
        whiteXYZ(temperatureFromOffset(temperatureOffset), duv, srcXYZ);
        whiteXYZ(kReferenceTemperature, 0.0, refXYZ);

        double srcLMS[3], refLMS[3];
        matVec(kBradford, srcXYZ, srcLMS);
        matVec(kBradford, refXYZ, refLMS);

        // Von Kries: scale each cone response by the ratio of the two whites.
        // Adapting away from the assumed illuminant is what corrects the image,
        // so a warm assumed illuminant cools the picture and vice versa.
        for (int i = 0; i < 3; ++i)
        {
            if (!(srcLMS[i] > 1e-6))
            {
                // Unreachable for a physical white, but a division by zero here
                // would poison every pixel in the frame.
                srcLMS[i] = 1e-6;
            }
        }

        const double diag[9] = {
            refLMS[0] / srcLMS[0], 0.0, 0.0,
            0.0, refLMS[1] / srcLMS[1], 0.0,
            0.0, 0.0, refLMS[2] / srcLMS[2]
        };

        double bradfordInv[9];
        matInverse(kBradford, bradfordInv);

        double tmp[9], catXYZ[9];
        matMul(diag, kBradford, tmp);
        matMul(bradfordInv, tmp, catXYZ);

        double xyzToAP1[9];
        matInverse(kAP1ToXYZ, xyzToAP1);

        double catAP1[9];
        matMul(catXYZ, kAP1ToXYZ, tmp);
        matMul(xyzToAP1, tmp, catAP1);

        if (preserveExposure)
        {
            // A neutral is (1,1,1) in AP1; hold its luminance across the adaptation.
            double neutral[3] = { 1.0, 1.0, 1.0 };
            double adapted[3];
            matVec(catAP1, neutral, adapted);

            const double lum = ap1Luminance(adapted[0], adapted[1], adapted[2]);
            if (lum > 1e-9)
            {
                for (int i = 0; i < 9; ++i)
                {
                    catAP1[i] /= lum;
                }
            }
        }

        for (int i = 0; i < 9; ++i)
        {
            outMatrix[i] = static_cast<float>(catAP1[i]);
        }
    }
}
