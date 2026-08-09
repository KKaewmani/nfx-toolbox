// Compiles the render kernel through the Metal runtime, exactly as the plugin
// does at first render. This catches a break in the shared colour maths without
// needing the Xcode Metal toolchain installed, and without opening Resolve.

#import <Metal/Metal.h>

#include <cstdio>

#include "../MetalSource.h"

int main()
{
    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
        {
            printf("No Metal device available, skipping the kernel compile check.\n");
            return 0;
        }

        NSError* err = nil;
        MTLCompileOptions* options = [MTLCompileOptions new];
        if (@available(macOS 15.0, *))
        {
            options.mathMode = MTLMathModeSafe;
        }

        const std::string source = BuildMetalSource();
        id<MTLLibrary> library = [device newLibraryWithSource:@(source.c_str()) options:options error:&err];

        if (!library)
        {
            printf("Metal kernel FAILED to compile:\n%s\n", err.localizedDescription.UTF8String);
            return 1;
        }

        id<MTLFunction> function = [library newFunctionWithName:@"TechnicalGradeKernel"];
        if (!function)
        {
            printf("Metal kernel compiled but TechnicalGradeKernel is missing.\n");
            return 1;
        }

        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&err];
        if (!pipeline)
        {
            printf("Metal pipeline FAILED to build:\n%s\n", err.localizedDescription.UTF8String);
            return 1;
        }

        printf("Metal kernel compiles on %s.\n", device.name.UTF8String);
        return 0;
    }
}
