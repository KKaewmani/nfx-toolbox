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
}
