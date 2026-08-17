#include "ColorSpaces.h"

#include "ColorMath.h"

#include <cmath>

namespace
{
    const double kD65[2] = { 0.3127, 0.3290 };
    const double kACESWhite[2] = { 0.32168, 0.33767 };

    // ACEScg / AP1, SMPTE ST 2065-1 companion primaries (S-2014-004).
    const double kAP1Primaries[6] = {
        0.713, 0.293,
        0.165, 0.830,
        0.128, 0.044
    };

    // ARRI ALEXA Wide Gamut (LogC3), VFX spec.
    const double kAWG3Primaries[6] = {
        0.6840, 0.3130,
        0.2210, 0.8480,
        0.0861, -0.1020
    };

    // ARRI Wide Gamut 4 (LogC4 spec).
    const double kAWG4Primaries[6] = {
        0.7347, 0.2653,
        0.1424, 0.8576,
        0.0991, -0.0308
    };

    // Sony S-Gamut3.Cine, ACES IDT / Sony technical summary.
    const double kSGamut3CinePrimaries[6] = {
        0.766, 0.275,
        0.225, 0.800,
        0.089, -0.087
    };

    // Canon Cinema Gamut, ACES CSC.Canon.CLog3_CGamut_to_ACES.
    const double kCinemaGamutPrimaries[6] = {
        0.7400, 0.2700,
        0.1700, 1.1400,
        0.0800, -0.1000
    };

    // REDWideGamutRGB, 915-0187 Rev-C.
    const double kRWGPrimaries[6] = {
        0.780308, 0.304253,
        0.121595, 1.493994,
        0.095612, -0.084589
    };

    // CAT02, as used by ACES IDTs.
    const double kCAT02[9] = {
         0.7328,  0.4296, -0.1624,
        -0.7036,  1.6975,  0.0061,
         0.0030,  0.0136,  0.9834
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

    bool invert3x3(const double m[9], double out[9])
    {
        const double a = m[0], b = m[1], c = m[2];
        const double d = m[3], e = m[4], f = m[5];
        const double g = m[6], h = m[7], i = m[8];

        const double A = (e * i - f * h);
        const double B = -(d * i - f * g);
        const double C = (d * h - e * g);
        const double D = -(b * i - c * h);
        const double E = (a * i - c * g);
        const double F = -(a * h - b * g);
        const double G = (b * f - c * e);
        const double H = -(a * f - c * d);
        const double I = (a * e - b * d);

        const double det = a * A + b * B + c * C;
        if (fabs(det) < 1e-18)
        {
            return false;
        }

        const double inv = 1.0 / det;
        out[0] = A * inv; out[1] = D * inv; out[2] = G * inv;
        out[3] = B * inv; out[4] = E * inv; out[5] = H * inv;
        out[6] = C * inv; out[7] = F * inv; out[8] = I * inv;
        return true;
    }

    void xyToXYZ(double x, double y, double xyz[3])
    {
        xyz[0] = x / y;
        xyz[1] = 1.0;
        xyz[2] = (1.0 - x - y) / y;
    }

    void rgbToXyz(const double prim[6], const double white[2], double out[9])
    {
        double r[3], g[3], b[3], w[3];
        xyToXYZ(prim[0], prim[1], r);
        xyToXYZ(prim[2], prim[3], g);
        xyToXYZ(prim[4], prim[5], b);
        xyToXYZ(white[0], white[1], w);

        const double M[9] = {
            r[0], g[0], b[0],
            r[1], g[1], b[1],
            r[2], g[2], b[2]
        };

        double Minv[9];
        invert3x3(M, Minv);
        double s[3];
        matVec(Minv, w, s);

        out[0] = r[0] * s[0]; out[1] = g[0] * s[1]; out[2] = b[0] * s[2];
        out[3] = r[1] * s[0]; out[4] = g[1] * s[1]; out[5] = b[1] * s[2];
        out[6] = r[2] * s[0]; out[7] = g[2] * s[1]; out[8] = b[2] * s[2];
    }

    void cat02(const double srcW[3], const double dstW[3], double out[9])
    {
        double srcC[3], dstC[3];
        matVec(kCAT02, srcW, srcC);
        matVec(kCAT02, dstW, dstC);

        const double scale[9] = {
            dstC[0] / srcC[0], 0.0, 0.0,
            0.0, dstC[1] / srcC[1], 0.0,
            0.0, 0.0, dstC[2] / srcC[2]
        };

        double coneInv[9], tmp[9];
        invert3x3(kCAT02, coneInv);
        matMul(scale, kCAT02, tmp);
        matMul(coneInv, tmp, out);
    }

    void rgbToRgb(const double srcPrim[6], const double srcWhite[2],
                  const double dstPrim[6], const double dstWhite[2],
                  double out[9])
    {
        double srcToXyz[9], dstToXyz[9], xyzToDst[9];
        rgbToXyz(srcPrim, srcWhite, srcToXyz);
        rgbToXyz(dstPrim, dstWhite, dstToXyz);
        invert3x3(dstToXyz, xyzToDst);

        double srcXYZ[3], dstXYZ[3], cat[9], tmp[9];
        xyToXYZ(srcWhite[0], srcWhite[1], srcXYZ);
        xyToXYZ(dstWhite[0], dstWhite[1], dstXYZ);
        cat02(srcXYZ, dstXYZ, cat);

        matMul(cat, srcToXyz, tmp);
        matMul(xyzToDst, tmp, out);
    }

    void toFloat(const double src[9], float dst[9])
    {
        for (int i = 0; i < 9; ++i)
        {
            dst[i] = static_cast<float>(src[i]);
        }
    }

    void cameraToAp1(const double prim[6], float inM[9], float outM[9])
    {
        double camToAp1[9], ap1ToCam[9];
        rgbToRgb(prim, kD65, kAP1Primaries, kACESWhite, camToAp1);
        invert3x3(camToAp1, ap1ToCam);
        toFloat(camToAp1, inM);
        toFloat(ap1ToCam, outM);
    }
}

void cs::identityMatrix(float out[9])
{
    for (int i = 0; i < 9; ++i)
    {
        out[i] = 0.0f;
    }
    out[0] = out[4] = out[8] = 1.0f;
}

void cs::applyWorkingSpaceMatrices(KernelParams& p)
{
    const int space = static_cast<int>(p.workingSpace + 0.5f);

    switch (space)
    {
        case CM_SPACE_LOGC3:
            cameraToAp1(kAWG3Primaries, p.inMatrix, p.outMatrix);
            break;
        case CM_SPACE_LOGC4:
            cameraToAp1(kAWG4Primaries, p.inMatrix, p.outMatrix);
            break;
        case CM_SPACE_SLOG3:
            cameraToAp1(kSGamut3CinePrimaries, p.inMatrix, p.outMatrix);
            break;
        case CM_SPACE_CLOG3:
            cameraToAp1(kCinemaGamutPrimaries, p.inMatrix, p.outMatrix);
            break;
        case CM_SPACE_LOG3G10:
            cameraToAp1(kRWGPrimaries, p.inMatrix, p.outMatrix);
            break;
        default:
            identityMatrix(p.inMatrix);
            identityMatrix(p.outMatrix);
            break;
    }
}
