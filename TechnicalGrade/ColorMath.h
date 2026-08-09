#pragma once

// C++ prelude for the shared colour maths. The Metal side supplies its own
// prelude inside MetalKernel.mm and then includes the identical body text, so
// the two render paths execute the same arithmetic.

#include <cmath>

#define CM_INLINE     inline
#define CM_PARAM_PTR  const
#define CM_THREAD

#define CM_LOG2(x)    log2f(x)
#define CM_EXP2(x)    exp2f(x)
#define CM_TANH(x)    tanhf(x)
#define CM_MIN(a, b)  fminf(a, b)
#define CM_MAX(a, b)  fmaxf(a, b)

#include "ColorMathBody.h"
