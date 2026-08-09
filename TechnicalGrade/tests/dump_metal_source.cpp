// Emits the exact Metal source the plugin compiles at runtime, so the build can
// hand it to the offline metal compiler and catch a break in the shared colour
// maths without opening Resolve.

#include <cstdio>

#include "../MetalSource.h"

int main()
{
    fputs(BuildMetalSource().c_str(), stdout);
    return 0;
}
