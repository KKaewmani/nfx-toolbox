#pragma once

#include "KernelParams.h"

// Camera RGB (linear, after the log curve) <-> ACES AP1.
//
// Built from vendor/ACES chromaticities with CAT02, matching the ACES IDT
// convention (D65 camera whites adapted to the ACES white point). ACEScct,
// ACEScc and Linear AP1 use identities: they are already AP1.

namespace cs
{
    void identityMatrix(float out[9]);

    // Fills p.inMatrix and p.outMatrix from p.workingSpace. Does not touch
    // the exposure/white-balance matrix.
    void applyWorkingSpaceMatrices(KernelParams& p);

    void ap1PrimariesXy(double out[6]);
    void acesWhiteXy(double out[2]);

    // White-balanced RGB-to-XYZ for the given primaries and white.
    void rgbToXyz(const double prim[6], const double white[2], double out[9]);

    // src RGB -> dst RGB, CAT02 when the whites differ.
    void rgbToRgb(const double srcPrim[6], const double srcWhite[2],
                  const double dstPrim[6], const double dstWhite[2],
                  double out[9]);
}
