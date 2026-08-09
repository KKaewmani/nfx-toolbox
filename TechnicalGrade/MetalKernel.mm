#import <Metal/Metal.h>

#include <mutex>
#include <string>
#include <unordered_map>

#include "KernelParams.h"
#include "MetalSource.h"

namespace
{
    std::mutex s_PipelineQueueMutex;
    std::unordered_map<id<MTLCommandQueue>, id<MTLComputePipelineState> > s_PipelineQueueMap;

    id<MTLComputePipelineState> GetPipelineState(id<MTLCommandQueue> p_Queue)
    {
        const auto it = s_PipelineQueueMap.find(p_Queue);
        if (it != s_PipelineQueueMap.end())
        {
            return it->second;
        }

        id<MTLDevice> device = p_Queue.device;
        NSError* err = nil;

        MTLCompileOptions* options = [MTLCompileOptions new];

        // Fast math would let the compiler reassociate the log2/exp2 round trip
        // and diverge from the CPU path, which is exactly what the shared source
        // is here to prevent.
        if (@available(macOS 15.0, *))
        {
            options.mathMode = MTLMathModeSafe;
        }
        else
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            options.fastMathEnabled = NO;
#pragma clang diagnostic pop
        }

        const std::string source = BuildMetalSource();

        id<MTLLibrary> library = [device newLibraryWithSource:@(source.c_str()) options:options error:&err];
        [options release];

        if (!library)
        {
            fprintf(stderr, "TechnicalGrade: failed to compile Metal library, %s\n",
                    err.localizedDescription.UTF8String);
            return nil;
        }

        id<MTLFunction> function = [library newFunctionWithName:@"TechnicalGradeKernel"];
        if (!function)
        {
            fprintf(stderr, "TechnicalGrade: failed to retrieve kernel\n");
            [library release];
            return nil;
        }

        id<MTLComputePipelineState> pipelineState = [device newComputePipelineStateWithFunction:function error:&err];

        [function release];
        [library release];

        if (!pipelineState)
        {
            fprintf(stderr, "TechnicalGrade: failed to create pipeline state, %s\n",
                    err.localizedDescription.UTF8String);
            return nil;
        }

        // Deliberately retained for the lifetime of the process: compiling the
        // library on every render would stall playback.
        s_PipelineQueueMap[p_Queue] = pipelineState;
        return pipelineState;
    }
}

void RunMetalKernel(void* p_CmdQ, int p_Width, int p_Height, const float* p_Params,
                    const float* p_Input, float* p_Output)
{
    id<MTLCommandQueue> queue = static_cast<id<MTLCommandQueue> >(p_CmdQ);

    id<MTLComputePipelineState> pipelineState;
    {
        std::unique_lock<std::mutex> lock(s_PipelineQueueMutex);
        pipelineState = GetPipelineState(queue);
    }

    if (!pipelineState)
    {
        return;
    }

    id<MTLBuffer> srcDeviceBuf = reinterpret_cast<id<MTLBuffer> >(const_cast<float*>(p_Input));
    id<MTLBuffer> dstDeviceBuf = reinterpret_cast<id<MTLBuffer> >(p_Output);

    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    commandBuffer.label = @"TechnicalGradeKernel";

    id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:pipelineState];

    const int exeWidth = [pipelineState threadExecutionWidth];
    const MTLSize threadGroupCount = MTLSizeMake(exeWidth, 1, 1);
    const MTLSize threadGroups = MTLSizeMake((p_Width + exeWidth - 1) / exeWidth, p_Height, 1);

    [computeEncoder setBuffer:srcDeviceBuf offset:0 atIndex:0];
    [computeEncoder setBuffer:dstDeviceBuf offset:0 atIndex:8];
    [computeEncoder setBytes:&p_Width length:sizeof(int) atIndex:11];
    [computeEncoder setBytes:&p_Height length:sizeof(int) atIndex:12];
    [computeEncoder setBytes:p_Params length:kNumKernelParams * sizeof(float) atIndex:13];

    [computeEncoder dispatchThreadgroups:threadGroups threadsPerThreadgroup:threadGroupCount];

    [computeEncoder endEncoding];
    [commandBuffer commit];
}
