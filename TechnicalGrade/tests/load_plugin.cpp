// Opens the built bundle the way an OFX host does and checks that it registers
// the plugin. Catches missing exports, load-time crashes and bad packaging
// without having to restart Resolve.

#include <dlfcn.h>

#include <cstdio>
#include <cstring>

#include "ofxImageEffect.h"

namespace
{
    const char* kBundleBinary = "../TechnicalGrade.ofx.bundle/Contents/MacOS/TechnicalGrade.ofx";
    const char* kExpectedIdentifier = "com.nfx.TechnicalGrade";
}

int main()
{
    printf("\nBundle load check\n\n");

    void* handle = dlopen(kBundleBinary, RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
    {
        printf("  FAIL  could not open %s: %s\n\n", kBundleBinary, dlerror());
        return 1;
    }

    typedef int (*GetNumberOfPluginsFn)(void);
    typedef OfxPlugin* (*GetPluginFn)(int);

    GetNumberOfPluginsFn getCount = reinterpret_cast<GetNumberOfPluginsFn>(dlsym(handle, "OfxGetNumberOfPlugins"));
    GetPluginFn getPlugin = reinterpret_cast<GetPluginFn>(dlsym(handle, "OfxGetPlugin"));

    if (!getCount || !getPlugin)
    {
        printf("  FAIL  the bundle does not export the OFX entry points\n\n");
        return 1;
    }

    const int count = getCount();
    if (count != 1)
    {
        printf("  FAIL  expected exactly one plugin, the bundle reports %d\n\n", count);
        return 1;
    }

    OfxPlugin* plugin = getPlugin(0);
    if (!plugin)
    {
        printf("  FAIL  OfxGetPlugin returned nothing\n\n");
        return 1;
    }

    int failures = 0;

    if (strcmp(plugin->pluginApi, kOfxImageEffectPluginApi) != 0)
    {
        printf("  FAIL  unexpected plugin API '%s'\n", plugin->pluginApi);
        ++failures;
    }
    if (plugin->apiVersion != 1)
    {
        printf("  FAIL  unexpected API version %d\n", plugin->apiVersion);
        ++failures;
    }
    if (strcmp(plugin->pluginIdentifier, kExpectedIdentifier) != 0)
    {
        printf("  FAIL  identifier is '%s', expected '%s'\n", plugin->pluginIdentifier, kExpectedIdentifier);
        ++failures;
    }
    if (!plugin->setHost || !plugin->mainEntry)
    {
        printf("  FAIL  the plugin struct is missing setHost or mainEntry\n");
        ++failures;
    }

    if (failures == 0)
    {
        printf("  %s v%d.%d registers correctly.\n\n",
               plugin->pluginIdentifier, plugin->pluginVersionMajor, plugin->pluginVersionMinor);
    }
    else
    {
        printf("\n");
    }

    return failures == 0 ? 0 : 1;
}
