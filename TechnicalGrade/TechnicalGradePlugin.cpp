#include "TechnicalGradePlugin.h"

#include <cmath>
#include <memory>
#include <string>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.h"
#include "ofxsLog.h"

#include "ColorMath.h"
#include "ColorSpaces.h"
#include "KernelParams.h"
#include "WhiteBalance.h"

#define kPluginName "Technical Grade"
#define kPluginGrouping "NFX Toolbox"
#define kPluginDescription \
    "Scene-linear exposure, lens falloff, white balance, contrast and highlight/shadow " \
    "limiting for ACES.\n\n" \
    "Decodes the selected working space to linear AP1, applies an exposure gain, a radial " \
    "falloff and a Bradford chromatic adaptation driven by temperature and tint, then moves " \
    "to log2 exposure to pivot contrast about middle grey and roll the extremes off with a " \
    "sigmoid before re-encoding."
#define kPluginIdentifier "com.nfx.TechnicalGrade"
#define kPluginVersionMajor 2
#define kPluginVersionMinor 3

#define kSupportsTiles false
#define kSupportsMultiResolution false
#define kSupportsMultipleClipPARs false

namespace
{
    // Middle grey is the linear pivot. The slider shows that number, default
    // 0.18, and the ends are two stops either side (0.18 * 2^±2).
    const double kBasePivot = 0.18;
    const double kPivotRangeEV = 2.0;
    const double kPivotMin = kBasePivot * exp2(-kPivotRangeEV);
    const double kPivotMax = kBasePivot * exp2(kPivotRangeEV);

    // Contrast is set in stops: the slope is 2^control, so the control
    // runs -1 to +1 for a slope of 0.5 to 2 and is symmetric about no change.
    // The kernel will take any slope; the range is this narrow because the ends
    // of a wider one were unusable and cost precision everywhere in between.
    const double kContrastRangeEV = 1.0;

    // A limit sitting on middle grey would clamp the whole image to the pivot,
    // so the sliders stop two stops short of it at either end.
    const double kMaxLimiterEV = 8.0;
    const double kMinLimiterEV = 2.0;

    // A hard clip is almost never what anyone wants, and excluding it keeps the
    // knee from ever collapsing to nothing.
    const double kMinSoftness = 0.2;

    const double kMaxVignetteEV = 4.0;
}

////////////////////////////////////////////////////////////////////////////////

class TechnicalGradeProcessor : public OFX::ImageProcessor
{
public:
    explicit TechnicalGradeProcessor(OFX::ImageEffect& p_Instance);

    virtual void processImagesMetal();
    virtual void multiThreadProcessImages(OfxRectI p_ProcWindow);

    void setSrcImg(OFX::Image* p_SrcImg);
    void setParams(const KernelParams& p_Params);

private:
    OFX::Image* _srcImg;
    KernelParams _params;
};

TechnicalGradeProcessor::TechnicalGradeProcessor(OFX::ImageEffect& p_Instance)
    : OFX::ImageProcessor(p_Instance)
    , _srcImg(0)
{
}

#ifdef __APPLE__
extern void RunMetalKernel(void* p_CmdQ, int p_Width, int p_Height, const float* p_Params,
                           const float* p_Input, float* p_Output);
#endif

void TechnicalGradeProcessor::processImagesMetal()
{
#ifdef __APPLE__
    const OfxRectI& bounds = _srcImg->getBounds();
    const int width = bounds.x2 - bounds.x1;
    const int height = bounds.y2 - bounds.y1;

    const float* input = static_cast<const float*>(_srcImg->getPixelData());
    float* output = static_cast<float*>(_dstImg->getPixelData());

    RunMetalKernel(_pMetalCmdQ, width, height, reinterpret_cast<const float*>(&_params), input, output);
#endif
}

void TechnicalGradeProcessor::multiThreadProcessImages(OfxRectI p_ProcWindow)
{
    const float* params = reinterpret_cast<const float*>(&_params);

    // The falloff is anchored to the frame, so coordinates have to be measured
    // from the image bounds rather than from this thread's slice of them. The
    // GPU indexes from the same origin.
    const OfxRectI& bounds = _srcImg ? _srcImg->getBounds() : _dstImg->getBounds();

    for (int y = p_ProcWindow.y1; y < p_ProcWindow.y2; ++y)
    {
        if (_effect.abort()) break;

        float* dstPix = static_cast<float*>(_dstImg->getPixelAddress(p_ProcWindow.x1, y));
        const float py = static_cast<float>(y - bounds.y1) + 0.5f;

        for (int x = p_ProcWindow.x1; x < p_ProcWindow.x2; ++x)
        {
            const float* srcPix = static_cast<const float*>(_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

            if (srcPix)
            {
                cmProcessPixel(srcPix[0], srcPix[1], srcPix[2],
                               static_cast<float>(x - bounds.x1) + 0.5f, py,
                               params, &dstPix[0], &dstPix[1], &dstPix[2]);
                dstPix[3] = srcPix[3];
            }
            else
            {
                for (int c = 0; c < 4; ++c)
                {
                    dstPix[c] = 0.0f;
                }
            }

            dstPix += 4;
        }
    }
}

void TechnicalGradeProcessor::setSrcImg(OFX::Image* p_SrcImg)
{
    _srcImg = p_SrcImg;
}

void TechnicalGradeProcessor::setParams(const KernelParams& p_Params)
{
    _params = p_Params;
}

////////////////////////////////////////////////////////////////////////////////

class TechnicalGradePlugin : public OFX::ImageEffect
{
public:
    explicit TechnicalGradePlugin(OfxImageEffectHandle p_Handle);

    virtual void render(const OFX::RenderArguments& p_Args);
    virtual bool isIdentity(const OFX::IsIdentityArguments& p_Args, OFX::Clip*& p_IdentityClip, double& p_IdentityTime);

    void setupAndProcess(TechnicalGradeProcessor& p_Processor, const OFX::RenderArguments& p_Args);

private:
    KernelParams buildParams(double p_Time, const OfxRectI& p_Bounds, double p_PixelAspect);
    bool highlightLimiterOn(double p_Time);
    bool shadowLimiterOn(double p_Time);

    // Does not own the following pointers
    OFX::Clip* m_DstClip;
    OFX::Clip* m_SrcClip;

    OFX::ChoiceParam* m_WorkingSpace;
    OFX::DoubleParam* m_Exposure;
    OFX::DoubleParam* m_Vignette;
    OFX::DoubleParam* m_Temperature;
    OFX::DoubleParam* m_Tint;
    OFX::DoubleParam* m_Pivot;
    OFX::DoubleParam* m_Contrast;
    OFX::BooleanParam* m_ShadowEnable;
    OFX::DoubleParam* m_ShadowLimit;
    OFX::DoubleParam* m_ShadowSoftness;
    OFX::BooleanParam* m_HighlightEnable;
    OFX::DoubleParam* m_HighlightLimit;
    OFX::DoubleParam* m_HighlightSoftness;
};

TechnicalGradePlugin::TechnicalGradePlugin(OfxImageEffectHandle p_Handle)
    : ImageEffect(p_Handle)
{
    m_DstClip = fetchClip(kOfxImageEffectOutputClipName);
    m_SrcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);

    m_WorkingSpace = fetchChoiceParam("workingSpace");
    m_Exposure = fetchDoubleParam("exposure");
    m_Vignette = fetchDoubleParam("vignette");
    m_Temperature = fetchDoubleParam("temperature");
    m_Tint = fetchDoubleParam("tint");
    m_Pivot = fetchDoubleParam("middleGrey");
    m_Contrast = fetchDoubleParam("contrast");
    m_ShadowEnable = fetchBooleanParam("shadowEnable");
    m_ShadowLimit = fetchDoubleParam("shadowLimit");
    m_ShadowSoftness = fetchDoubleParam("shadowSoftness");
    m_HighlightEnable = fetchBooleanParam("highlightEnable");
    m_HighlightLimit = fetchDoubleParam("highlightLimit");
    m_HighlightSoftness = fetchDoubleParam("highlightSoftness");
}

void TechnicalGradePlugin::render(const OFX::RenderArguments& p_Args)
{
    if ((m_DstClip->getPixelDepth() == OFX::eBitDepthFloat) &&
        (m_DstClip->getPixelComponents() == OFX::ePixelComponentRGBA))
    {
        TechnicalGradeProcessor processor(*this);
        setupAndProcess(processor, p_Args);
    }
    else
    {
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

bool TechnicalGradePlugin::isIdentity(const OFX::IsIdentityArguments& p_Args, OFX::Clip*& p_IdentityClip, double& p_IdentityTime)
{
    const double exposure = m_Exposure->getValueAtTime(p_Args.time);
    const double vignette = m_Vignette->getValueAtTime(p_Args.time);
    const double temperature = m_Temperature->getValueAtTime(p_Args.time);
    const double tint = m_Tint->getValueAtTime(p_Args.time);
    const double contrast = m_Contrast->getValueAtTime(p_Args.time);

    // The pivot is only observable through contrast or the limiters, so it is
    // deliberately absent from this test.
    if ((exposure == 0.0) && (vignette == 0.0) && (temperature == 0.0) && (tint == 0.0) &&
        (contrast == 0.0) && !shadowLimiterOn(p_Args.time) && !highlightLimiterOn(p_Args.time))
    {
        p_IdentityClip = m_SrcClip;
        p_IdentityTime = p_Args.time;
        return true;
    }

    return false;
}

bool TechnicalGradePlugin::highlightLimiterOn(double p_Time)
{
    // Hidden enable flag keeps old nodes that had the checkbox on. New ones
    // engage by pulling the limit in from the default end of the slider.
    if (m_HighlightEnable->getValueAtTime(p_Time))
    {
        return true;
    }
    return m_HighlightLimit->getValueAtTime(p_Time) < kMaxLimiterEV - 1e-6;
}

bool TechnicalGradePlugin::shadowLimiterOn(double p_Time)
{
    if (m_ShadowEnable->getValueAtTime(p_Time))
    {
        return true;
    }
    return m_ShadowLimit->getValueAtTime(p_Time) > -kMaxLimiterEV + 1e-6;
}

KernelParams TechnicalGradePlugin::buildParams(double p_Time, const OfxRectI& p_Bounds, double p_PixelAspect)
{
    KernelParams params;

    const double temperature = m_Temperature->getValueAtTime(p_Time);
    const double tint = m_Tint->getValueAtTime(p_Time);

    wb::computeMatrix(temperature, tint, true, params.matrix);

    // Exposure is a plain linear gain, so it folds straight into the adaptation
    // matrix and costs the kernel nothing.
    const float gain = static_cast<float>(exp2(m_Exposure->getValueAtTime(p_Time)));
    for (int i = 0; i < 9; ++i)
    {
        params.matrix[i] *= gain;
    }

    // Both tone controls are set in stops and converted to the linear quantities
    // the kernel wants, which keeps the sliders evenly spaced in what the eye sees.
    params.pivot = static_cast<float>(m_Pivot->getValueAtTime(p_Time));
    params.slope = static_cast<float>(exp2(m_Contrast->getValueAtTime(p_Time)));

    params.shadowEnable = shadowLimiterOn(p_Time) ? 1.0f : 0.0f;
    params.shadowLimit = static_cast<float>(m_ShadowLimit->getValueAtTime(p_Time));
    params.shadowSoftness = static_cast<float>(m_ShadowSoftness->getValueAtTime(p_Time));

    params.highlightEnable = highlightLimiterOn(p_Time) ? 1.0f : 0.0f;
    params.highlightLimit = static_cast<float>(m_HighlightLimit->getValueAtTime(p_Time));
    params.highlightSoftness = static_cast<float>(m_HighlightSoftness->getValueAtTime(p_Time));

    int workingSpace = CM_SPACE_ACESCCT;
    m_WorkingSpace->getValueAtTime(p_Time, workingSpace);
    params.workingSpace = static_cast<float>(workingSpace);
    cs::applyWorkingSpaceMatrices(params);

    setVignetteGeometry(params, m_Vignette->getValueAtTime(p_Time),
                        static_cast<double>(p_Bounds.x2 - p_Bounds.x1),
                        static_cast<double>(p_Bounds.y2 - p_Bounds.y1),
                        p_PixelAspect);

    return params;
}

void TechnicalGradePlugin::setupAndProcess(TechnicalGradeProcessor& p_Processor, const OFX::RenderArguments& p_Args)
{
    std::unique_ptr<OFX::Image> dst(m_DstClip->fetchImage(p_Args.time));
    OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents = dst->getPixelComponents();

    std::unique_ptr<OFX::Image> src(m_SrcClip->fetchImage(p_Args.time));
    OFX::BitDepthEnum srcBitDepth = src->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

    if ((srcBitDepth != dstBitDepth) || (srcComponents != dstComponents))
    {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
    }

    p_Processor.setDstImg(dst.get());
    p_Processor.setSrcImg(src.get());
    p_Processor.setGPURenderArgs(p_Args);
    p_Processor.setRenderWindow(p_Args.renderWindow);
    p_Processor.setParams(buildParams(p_Args.time, src->getBounds(), src->getPixelAspectRatio()));

    p_Processor.process();
}

////////////////////////////////////////////////////////////////////////////////

using namespace OFX;

TechnicalGradePluginFactory::TechnicalGradePluginFactory()
    : OFX::PluginFactoryHelper<TechnicalGradePluginFactory>(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor)
{
}

void TechnicalGradePluginFactory::describe(OFX::ImageEffectDescriptor& p_Desc)
{
    p_Desc.setLabels(kPluginName, kPluginName, kPluginName);
    p_Desc.setPluginGrouping(kPluginGrouping);
    p_Desc.setPluginDescription(kPluginDescription);

    p_Desc.addSupportedContext(eContextFilter);
    p_Desc.addSupportedContext(eContextGeneral);

    p_Desc.addSupportedBitDepth(eBitDepthFloat);

    p_Desc.setSingleInstance(false);
    p_Desc.setHostFrameThreading(false);
    p_Desc.setSupportsMultiResolution(kSupportsMultiResolution);
    p_Desc.setSupportsTiles(kSupportsTiles);
    p_Desc.setTemporalClipAccess(false);
    p_Desc.setRenderTwiceAlways(false);
    p_Desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);

#ifdef __APPLE__
    p_Desc.setSupportsMetalRender(true);
#endif

    // The lens falloff reads the pixel's position, so the effect is no longer a
    // pure colour transform and Resolve must not bake it into a LUT. Everything
    // else here still is one, but this flag is fixed at describe time and there
    // is no way to say "only when the falloff is off".
    p_Desc.setNoSpatialAwareness(false);
}

static DoubleParamDescriptor* defineDouble(OFX::ImageEffectDescriptor& p_Desc, const std::string& p_Name,
                                           const std::string& p_Label, const std::string& p_Hint,
                                           double p_Default, double p_Min, double p_Max, double p_Increment,
                                           GroupParamDescriptor* p_Parent)
{
    DoubleParamDescriptor* param = p_Desc.defineDoubleParam(p_Name);
    param->setLabels(p_Label, p_Label, p_Label);
    param->setScriptName(p_Name);
    param->setHint(p_Hint);
    param->setDefault(p_Default);
    param->setRange(p_Min, p_Max);
    param->setDisplayRange(p_Min, p_Max);
    param->setIncrement(p_Increment);
    param->setDoubleType(eDoubleTypePlain);

    if (p_Parent)
    {
        param->setParent(*p_Parent);
    }

    return param;
}

static BooleanParamDescriptor* defineBoolean(OFX::ImageEffectDescriptor& p_Desc, const std::string& p_Name,
                                             const std::string& p_Label, const std::string& p_Hint,
                                             bool p_Default, GroupParamDescriptor* p_Parent)
{
    BooleanParamDescriptor* param = p_Desc.defineBooleanParam(p_Name);
    param->setLabels(p_Label, p_Label, p_Label);
    param->setScriptName(p_Name);
    param->setHint(p_Hint);
    param->setDefault(p_Default);

    if (p_Parent)
    {
        param->setParent(*p_Parent);
    }

    return param;
}

void TechnicalGradePluginFactory::describeInContext(OFX::ImageEffectDescriptor& p_Desc, OFX::ContextEnum /*p_Context*/)
{
    ClipDescriptor* srcClip = p_Desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    ClipDescriptor* dstClip = p_Desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);

    PageParamDescriptor* page = p_Desc.definePageParam("Controls");

    // Working space ---------------------------------------------------------
    ChoiceParamDescriptor* space = p_Desc.defineChoiceParam("workingSpace");
    space->setLabels("Working Space", "Working Space", "Working Space");
    space->setScriptName("workingSpace");
    space->setHint("How the incoming pixels are encoded. Camera logs are converted to linear AP1 for the grade and converted back on the way out.");
    space->appendOption("ACEScct");
    space->appendOption("ACEScc");
    space->appendOption("Linear (AP1)");
    space->appendOption("ARRI LogC3 (EI 800)");
    space->appendOption("ARRI LogC4");
    space->appendOption("Sony S-Log3");
    space->appendOption("Canon C-Log3");
    space->appendOption("RED Log3G10");
    space->setDefault(CM_SPACE_ACESCCT);
    page->addChild(*space);

    // Exposure / white balance ----------------------------------------------
    GroupParamDescriptor* exposureGroup = p_Desc.defineGroupParam("exposureWhiteBalance");
    exposureGroup->setLabels("Exposure / White Balance", "Exposure / White Balance", "Exposure / White Balance");
    exposureGroup->setHint("Uniform gain, radial falloff and chromatic adaptation.");
    page->addChild(*exposureGroup);

    DoubleParamDescriptor* param = defineDouble(p_Desc, "exposure", "Exposure (EV)",
        "Linear gain of 2^EV, applied to all three channels before anything else.",
        0.0, -6.0, 6.0, 0.01, exposureGroup);
    page->addChild(*param);

    param = defineDouble(p_Desc, "vignette", "Lens Falloff (Vignette)",
        "Exposure change at the far corners, easing to none at the centre. "
        "Positive opens the corners up to cancel a lens that darkens them; "
        "negative darkens them for a vignette. 0 does nothing.",
        0.0, -kMaxVignetteEV, kMaxVignetteEV, 0.05, exposureGroup);
    page->addChild(*param);

    param = defineDouble(p_Desc, "temperature", "Temperature",
        "Relative warm/cool trim, as an offset rather than an absolute reading. "
        "Positive warms the image, negative cools it. One hundred units is the same "
        "size of shift wherever you are on the range, worth about 100 K at the "
        "middle of it.",
        0.0, wb::kMinTemperatureOffset, wb::kMaxTemperatureOffset, 100.0, exposureGroup);
    page->addChild(*param);

    param = defineDouble(p_Desc, "tint", "Tint",
        "Green/magenta trim across the Planckian locus. Positive is magenta, negative is green.",
        0.0, -100.0, 100.0, 0.5, exposureGroup);
    page->addChild(*param);

    // Tonal range -----------------------------------------------------------
    GroupParamDescriptor* toneGroup = p_Desc.defineGroupParam("tonalRange");
    toneGroup->setLabels("Tonal Range", "Tonal Range", "Tonal Range");
    toneGroup->setHint("Contrast and limiters, all relative to the same middle grey.");
    page->addChild(*toneGroup);

    param = defineDouble(p_Desc, "middleGrey", "Middle Grey",
        "The linear tone held fixed by the contrast slope, and the origin the limiters measure from. "
        "0.18 is ACEScct middle grey. The ends are two stops either side: 0.045 and 0.72.",
        kBasePivot, kPivotMin, kPivotMax, 0.005, toneGroup);
    page->addChild(*param);

    param = defineDouble(p_Desc, "contrast", "Contrast",
        "Slope of the log2 exposure response, itself set in stops: 0 leaves the image alone, "
        "+1 doubles the stops between any two tones and -1 halves them.",
        0.0, -kContrastRangeEV, kContrastRangeEV, 0.01, toneGroup);
    page->addChild(*param);

    param = defineDouble(p_Desc, "highlightLimit", "Highlight Limit (EV)",
        "Ceiling in stops above middle grey. At the default +8 the limiter is off; "
        "pull it down to engage. The curve approaches the limit without ever reaching it.",
        kMaxLimiterEV, kMinLimiterEV, kMaxLimiterEV, 0.05, toneGroup);
    page->addChild(*param);

    param = defineDouble(p_Desc, "highlightSoftness", "Highlight Softness",
        "How far down towards middle grey the roll-off begins. At 1 it starts at middle grey itself.",
        0.5, kMinSoftness, 1.0, 0.01, toneGroup);
    page->addChild(*param);

    param = defineDouble(p_Desc, "shadowLimit", "Shadow Limit (EV)",
        "Floor in stops below middle grey. At the default -8 the limiter is off; "
        "pull it up to engage. The curve approaches the limit without ever reaching it.",
        -kMaxLimiterEV, -kMaxLimiterEV, -kMinLimiterEV, 0.05, toneGroup);
    page->addChild(*param);

    param = defineDouble(p_Desc, "shadowSoftness", "Shadow Softness",
        "How far up towards middle grey the roll-off begins. At 1 it starts at middle grey itself.",
        0.5, kMinSoftness, 1.0, 0.01, toneGroup);
    page->addChild(*param);

    // Hidden leftovers so existing projects still load. White balance always
    // preserves luminance; the limiter checkboxes do not skip GPU work.
    BooleanParamDescriptor* boolParam = defineBoolean(p_Desc, "preserveExposure", "Preserve Exposure for WB",
        "Normalise the white-balance matrix so a neutral holds its luminance.",
        true, 0);
    boolParam->setIsSecret(true);

    boolParam = defineBoolean(p_Desc, "highlightEnable", "Enable Highlight Limiter",
        "Bound how far above middle grey the image may go.", false, 0);
    boolParam->setIsSecret(true);

    boolParam = defineBoolean(p_Desc, "shadowEnable", "Enable Shadow Limiter",
        "Bound how far below middle grey the image may go.", false, 0);
    boolParam->setIsSecret(true);
}

ImageEffect* TechnicalGradePluginFactory::createInstance(OfxImageEffectHandle p_Handle, ContextEnum /*p_Context*/)
{
    return new TechnicalGradePlugin(p_Handle);
}

void OFX::Plugin::getPluginIDs(PluginFactoryArray& p_FactoryArray)
{
    static TechnicalGradePluginFactory exposureBalancePlugin;
    p_FactoryArray.push_back(&exposureBalancePlugin);
}
