#include "Saturation.h"

#include "ColorSpaces.h"

#include <cmath>

namespace
{
    bool validChromaticity(double x, double y)
    {
        // AP1 red already has x+y > 1 (negative Z). Only y <= 0 breaks xyToXYZ.
        return std::isfinite(x) && std::isfinite(y) && (y > 1e-6);
    }

    void movePrimary(double px, double py, double wx, double wy,
                     double sat, double hue, double* outX, double* outY)
    {
        double wu, wv, pu, pv;
        sat::xyToUV(wx, wy, &wu, &wv);
        sat::xyToUV(px, py, &pu, &pv);

        const double vu = pu - wu;
        const double vv = pv - wv;
        const double length = hypot(vu, vv);
        if (length < 1e-18)
        {
            *outX = px;
            *outY = py;
            return;
        }

        // Clockwise perpendicular, looking from W toward P: (du, dv) -> (dv, -du).
        const double nu = vv / length;
        const double nv = -vu / length;
        const double tu = sat * vu + (hue - 1.0) * length * nu;
        const double tv = sat * vv + (hue - 1.0) * length * nv;

        auto sample = [&](double t, double* x, double* y) -> bool
        {
            sat::uvToXY(wu + t * tu, wv + t * tv, x, y);
            return validChromaticity(*x, *y);
        };

        double x, y;
        if (sample(1.0, &x, &y))
        {
            *outX = x;
            *outY = y;
            return;
        }

        double lo = 0.0;
        double hi = 1.0;
        for (int i = 0; i < 40; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            if (sample(mid, &x, &y))
            {
                lo = mid;
            }
            else
            {
                hi = mid;
            }
        }
        sample(lo, outX, outY);
    }

    bool slidersAreIdentity(double rSat, double rHue,
                            double gSat, double gHue,
                            double bSat, double bHue)
    {
        return (rSat == 1.0) && (rHue == 1.0) &&
               (gSat == 1.0) && (gHue == 1.0) &&
               (bSat == 1.0) && (bHue == 1.0);
    }
}

void sat::xyToUV(double x, double y, double* u, double* v)
{
    const double d = -2.0 * x + 12.0 * y + 3.0;
    *u = 4.0 * x / d;
    *v = 6.0 * y / d;
}

void sat::uvToXY(double u, double v, double* x, double* y)
{
    const double d = 2.0 * u - 8.0 * v + 4.0;
    *x = 3.0 * u / d;
    *y = 2.0 * v / d;
}

void sat::adjustedPrimaries(double rSat, double rHue,
                            double gSat, double gHue,
                            double bSat, double bHue,
                            double outXy[6])
{
    double prim[6];
    double white[2];
    cs::ap1PrimariesXy(prim);
    cs::acesWhiteXy(white);

    movePrimary(prim[0], prim[1], white[0], white[1], rSat, rHue, &outXy[0], &outXy[1]);
    movePrimary(prim[2], prim[3], white[0], white[1], gSat, gHue, &outXy[2], &outXy[3]);
    movePrimary(prim[4], prim[5], white[0], white[1], bSat, bHue, &outXy[4], &outXy[5]);
}

void sat::computeMatrix(double rSat, double rHue,
                        double gSat, double gHue,
                        double bSat, double bHue,
                        float outMatrix[9])
{
    if (slidersAreIdentity(rSat, rHue, gSat, gHue, bSat, bHue))
    {
        cs::identityMatrix(outMatrix);
        return;
    }

    double adjusted[6];
    double ap1[6];
    double white[2];
    adjustedPrimaries(rSat, rHue, gSat, gHue, bSat, bHue, adjusted);
    cs::ap1PrimariesXy(ap1);
    cs::acesWhiteXy(white);

    // Full NPM: interpret RGB on the adjusted primaries and convert back to
    // AP1 with the same ACES white, so grey stays put. Unmoved primaries stay
    // on-axis but their amounts change.
    double m[9];
    cs::rgbToRgb(adjusted, white, ap1, white, m);
    for (int i = 0; i < 9; ++i)
    {
        outMatrix[i] = static_cast<float>(m[i]);
    }
}
