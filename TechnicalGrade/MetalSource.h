#pragma once

#include <string>

// Assembles the Metal Shading Language source for the render kernel.
//
// Kept out of MetalKernel.mm so the test harness can emit the identical text and
// hand it to the offline metal compiler, which is what proves the shared colour
// maths really does compile on the GPU side.

// The Metal prelude binds the same CM_* macros the C++ prelude in ColorMath.h
// binds, but to the Metal Shading Library equivalents, so the shared body text
// that follows compiles unchanged on both sides.
static const char* kMetalPrelude = R"MSL(
#include <metal_stdlib>
using namespace metal;

#define CM_INLINE     inline
#define CM_PARAM_PTR  constant
#define CM_THREAD     thread

#define CM_LOG2(x)    log2(x)
#define CM_EXP2(x)    exp2(x)
#define CM_LOG10(x)   (log2(x) * 0.3010299956639812f)
#define CM_POW10(x)   exp2((x) * 3.3219280948873626f)
#define CM_TANH(x)    tanh(x)
#define CM_MIN(a, b)  fmin(a, b)
#define CM_MAX(a, b)  fmax(a, b)
)MSL";

// ColorMathSource.h is ColorMathBody.h wrapped in a raw string literal by the
// Makefile, so there is a single copy of the maths in the repository.
static const char* kColorMathSource =
#include "ColorMathSource.h"
;

static const char* kKernelSource = R"MSL(
kernel void TechnicalGradeKernel(constant int& p_Width [[buffer (11)]],
                                  constant int& p_Height [[buffer (12)]],
                                  constant float* p_Params [[buffer (13)]],
                                  const device float* p_Input [[buffer (0)]],
                                  device float* p_Output [[buffer (8)]],
                                  uint2 id [[thread_position_in_grid]])
{
    if ((id.x < (uint)p_Width) && (id.y < (uint)p_Height))
    {
        const int index = ((id.y * p_Width) + id.x) * 4;

        float r, g, b;
        cmProcessPixel(p_Input[index + 0], p_Input[index + 1], p_Input[index + 2],
                       (float)id.x + 0.5f, (float)id.y + 0.5f,
                       p_Params, &r, &g, &b);

        p_Output[index + 0] = r;
        p_Output[index + 1] = g;
        p_Output[index + 2] = b;
        p_Output[index + 3] = p_Input[index + 3];
    }
}
)MSL";

inline std::string BuildMetalSource()
{
    return std::string(kMetalPrelude) + kColorMathSource + kKernelSource;
}
