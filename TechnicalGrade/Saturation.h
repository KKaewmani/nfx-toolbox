#pragma once

// Primary sat / hue: move AP1 R, G, B in CIE 1960 uv around the ACES white,
// then build the AP1 3x3 that interprets those new primaries.

namespace sat
{
    void xyToUV(double x, double y, double* u, double* v);
    void uvToXY(double u, double v, double* x, double* y);

    // Adjusted AP1 primary xy, row-packed R, G, B.
    void adjustedPrimaries(double rSat, double rHue,
                           double gSat, double gHue,
                           double bSat, double bHue,
                           double outXy[6]);

    // Row-major AP1 3x3. Identity when every slider is 1. Full NPM with the
    // ACES white held fixed, so a grey stays a grey.
    void computeMatrix(double rSat, double rHue,
                       double gSat, double gHue,
                       double bSat, double bHue,
                       float outMatrix[9]);
}
