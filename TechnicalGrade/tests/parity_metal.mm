// Runs the real Metal kernel over a spread of pixels and parameter sets and
// compares it against the CPU path.
//
// The two share their source text, so this is really checking that the shared
// body behaves the same once the GPU's own log2, exp2 and tanh are substituted
// in, and that the flat parameter block arrives in the kernel intact.

#import <Metal/Metal.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../ColorMath.h"
#include "../KernelParams.h"
#include "../WhiteBalance.h"

// Linked straight out of the plugin: this is the same entry point Resolve calls.
extern void RunMetalKernel(void* p_CmdQ, int p_Width, int p_Height, const float* p_Params,
                           const float* p_Input, float* p_Output);

namespace
{
    int g_Failures = 0;

    // The frame the parity buffers are shaped as. Both sides have to derive the
    // falloff from the same geometry or every pixel off centre disagrees.
    const int kWidth = 512;
    const int kHeight = 4;

    KernelParams baseParams()
    {
        KernelParams p;
        wb::computeMatrix(0.0, 0.0, true, p.matrix);
        p.pivot = 0.18f;
        p.slope = 1.0f;
        p.shadowEnable = 0.0f;
        p.shadowLimit = -8.0f;
        p.shadowSoftness = 0.5f;
        p.highlightEnable = 0.0f;
        p.highlightLimit = 8.0f;
        p.highlightSoftness = 0.5f;
        p.workingSpace = CM_SPACE_ACESCCT;
        setVignetteGeometry(p, 0.0, kWidth, kHeight, 1.0);
        return p;
    }

    std::vector<KernelParams> parameterSets()
    {
        std::vector<KernelParams> sets;

        sets.push_back(baseParams());

        {   // exposure and a warm balance
            KernelParams p = baseParams();
            wb::computeMatrix(-3000.0, -25.0, true, p.matrix);
            const float gain = exp2f(1.5f);
            for (int i = 0; i < 9; ++i) p.matrix[i] *= gain;
            sets.push_back(p);
        }

        {   // heavy contrast with both limiters rolling off
            KernelParams p = baseParams();
            wb::computeMatrix(2500.0, 40.0, true, p.matrix);
            p.pivot = 0.09f;
            p.slope = 2.4f;
            p.shadowEnable = 1.0f;
            p.shadowLimit = -5.5f;
            p.shadowSoftness = 0.8f;
            p.highlightEnable = 1.0f;
            p.highlightLimit = 4.5f;
            p.highlightSoftness = 0.35f;
            sets.push_back(p);
        }

        {   // hard clips, the degenerate knee
            KernelParams p = baseParams();
            p.slope = 0.4f;
            p.pivot = 0.6f;
            p.shadowEnable = 1.0f;
            p.shadowLimit = -3.0f;
            p.shadowSoftness = 0.0f;
            p.highlightEnable = 1.0f;
            p.highlightLimit = 2.0f;
            p.highlightSoftness = 0.0f;
            sets.push_back(p);
        }

        {   // the other two working spaces
            KernelParams p = baseParams();
            p.workingSpace = CM_SPACE_ACESCC;
            p.slope = 1.6f;
            const float gain = exp2f(-2.0f);
            for (int i = 0; i < 9; ++i) p.matrix[i] *= gain;
            sets.push_back(p);

            p = baseParams();
            p.workingSpace = CM_SPACE_LINEAR;
            p.slope = 1.25f;
            p.highlightEnable = 1.0f;
            p.highlightLimit = 6.0f;
            p.highlightSoftness = 1.0f;
            sets.push_back(p);
        }

        {   // lens falloff, both directions and off square pixels
            KernelParams p = baseParams();
            setVignetteGeometry(p, -2.5, kWidth, kHeight, 1.0);
            sets.push_back(p);

            p = baseParams();
            p.workingSpace = CM_SPACE_LINEAR;
            p.slope = 1.3f;
            setVignetteGeometry(p, 3.0, kWidth, kHeight, 2.0);
            sets.push_back(p);

            // Falloff pushing tones into a limiter that is already rolling off.
            p = baseParams();
            wb::computeMatrix(1200.0, -15.0, true, p.matrix);
            p.highlightEnable = 1.0f;
            p.highlightLimit = 3.0f;
            p.highlightSoftness = 0.2f;
            p.shadowEnable = 1.0f;
            p.shadowLimit = -6.0f;
            p.shadowSoftness = 0.2f;
            setVignetteGeometry(p, 4.0, kWidth, kHeight, 1.0);
            sets.push_back(p);
        }

        return sets;
    }

    // A wide spread of pixels: encoded ramps, negatives, blown highlights and
    // heavily saturated colours where the channels disagree.
    std::vector<float> makeInput(int count)
    {
        std::vector<float> pixels(count * 4);

        for (int i = 0; i < count; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(count - 1);

            pixels[i * 4 + 0] = -0.1f + t * 1.6f;
            pixels[i * 4 + 1] = 1.5f - t * 1.7f;
            pixels[i * 4 + 2] = (i % 7) * 0.19f - 0.15f;
            pixels[i * 4 + 3] = t;
        }

        return pixels;
    }
}

int main()
{
    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
        {
            printf("No Metal device available, skipping the parity check.\n");
            return 0;
        }

        id<MTLCommandQueue> queue = [device newCommandQueue];

        const int width = kWidth;
        const int height = kHeight;
        const int count = width * height;
        const size_t bytes = count * 4 * sizeof(float);

        const std::vector<float> input = makeInput(count);

        id<MTLBuffer> srcBuf = [device newBufferWithBytes:input.data() length:bytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> dstBuf = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];

        const std::vector<KernelParams> sets = parameterSets();

        printf("\nMetal versus CPU parity\n\n");

        for (size_t s = 0; s < sets.size(); ++s)
        {
            const KernelParams& params = sets[s];
            const float* raw = reinterpret_cast<const float*>(&params);

            memset(dstBuf.contents, 0, bytes);

            // Resolve hands the plugin MTLBuffer handles disguised as float
            // pointers, so the test has to lie in exactly the same way.
            RunMetalKernel((__bridge void*)queue, width, height, raw,
                           reinterpret_cast<const float*>(srcBuf),
                           reinterpret_cast<float*>(dstBuf));

            // The kernel is committed but not waited on, so drain the queue.
            id<MTLCommandBuffer> fence = [queue commandBuffer];
            [fence commit];
            [fence waitUntilCompleted];

            const float* gpu = static_cast<const float*>(dstBuf.contents);

            double worst = 0.0;
            int worstIndex = -1;
            int failures = 0;

            for (int i = 0; i < count; ++i)
            {
                // Same origin and same half-pixel offset the kernel uses, so
                // any disagreement is arithmetic rather than addressing.
                const float px = static_cast<float>(i % width) + 0.5f;
                const float py = static_cast<float>(i / width) + 0.5f;

                float r, g, b;
                cmProcessPixel(input[i * 4 + 0], input[i * 4 + 1], input[i * 4 + 2],
                               px, py, raw, &r, &g, &b);

                const float expected[4] = { r, g, b, input[i * 4 + 3] };

                for (int c = 0; c < 4; ++c)
                {
                    const double got = gpu[i * 4 + c];
                    const double want = expected[c];

                    // Relative, but with an absolute floor so a near-zero result
                    // is not judged on a ratio of two rounding errors.
                    const double absolute = fabs(got - want);
                    const double error = absolute <= 1e-6 ? 0.0 : absolute / fabs(want);

                    if (error > worst)
                    {
                        worst = error;
                        worstIndex = i * 4 + c;
                    }
                    if (error > 1e-4)
                    {
                        if (failures < 5)
                        {
                            printf("  FAIL  set %zu pixel %d channel %d: gpu %.9g cpu %.9g\n",
                                   s, i, c, got, want);
                        }
                        ++failures;
                    }
                }
            }

            printf("  set %zu: %d pixels, worst relative error %.3g at element %d%s\n",
                   s, count, worst, worstIndex, failures ? "  <-- MISMATCH" : "");

            g_Failures += failures;
        }

        printf("\n%s\n\n", g_Failures ? "PARITY FAILED" : "Metal and CPU agree.");
        return g_Failures == 0 ? 0 : 1;
    }
}
