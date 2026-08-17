#pragma once

#include <cmath>

// Everything the per-pixel pipeline needs, derived once per render on the CPU.
//
// Every member is a float so the struct doubles as a flat float array. That is
// what lets it cross into the Metal kernel through a single setBytes call
// without any risk of the C++ and MSL layouts disagreeing: the kernel reads it
// as `constant float*` and indexes it positionally. The indices are mirrored in
// the comment at the top of cmProcessPixel in ColorMathBody.h.

struct KernelParams
{
    float matrix[9];            // row major, exposure premultiplied into the white balance CAT
    float pivot;                // middle grey in linear
    float slope;                // contrast, as a slope in log2 space
    float shadowEnable;         // 0 or 1
    float shadowLimit;          // EV relative to the pivot, negative
    float shadowSoftness;       // 0 hard clip .. 1 knee at the pivot
    float highlightEnable;      // 0 or 1
    float highlightLimit;       // EV relative to the pivot, positive
    float highlightSoftness;    // 0 hard clip .. 1 knee at the pivot
    float workingSpace;         // CM_SPACE_*

    // Lens falloff. The geometry is derived from the image bounds and the pixel
    // aspect ratio once per render, so the kernel only does a dot product.
    float vignetteEV;           // exposure change at the far corners, 0 is off
    float vignetteCenterX;      // frame centre in pixels, relative to the bounds
    float vignetteCenterY;
    float vignetteScaleX;       // scales the offset so r reaches 1 in the corners
    float vignetteScaleY;

    // Camera linear RGB <-> ACES AP1. Identity for ACEScct / ACEScc / Linear.
    float inMatrix[9];
    float outMatrix[9];
};

enum { kNumKernelParams = 41 };

static_assert(sizeof(KernelParams) == kNumKernelParams * sizeof(float),
              "KernelParams must stay a flat, padding-free block of floats");

// Works out the falloff geometry for a frame of the given size, so the kernel is
// left with two multiplies and an exp2 per pixel.
//
// Offsets from the centre are taken into real proportions by the pixel aspect
// ratio and then divided by the distance to the corner. That puts r at exactly 1
// in the corners of any frame shape while keeping the falloff circular rather
// than stretched to fit the format, which is how a lens behaves.
inline void setVignetteGeometry(KernelParams& p, double ev,
                                double width, double height, double pixelAspect)
{
    const double aspect = (pixelAspect > 0.0) ? pixelAspect : 1.0;
    const double halfW = 0.5 * width * aspect;
    const double halfH = 0.5 * height;
    const double corner = sqrt((halfW * halfW) + (halfH * halfH));
    const double invCorner = (corner > 0.0) ? (1.0 / corner) : 0.0;

    p.vignetteEV = static_cast<float>(ev);
    p.vignetteCenterX = static_cast<float>(0.5 * width);
    p.vignetteCenterY = static_cast<float>(0.5 * height);
    p.vignetteScaleX = static_cast<float>(aspect * invCorner);
    p.vignetteScaleY = static_cast<float>(invCorner);
}
