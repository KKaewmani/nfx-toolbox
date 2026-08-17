#pragma once

namespace wb
{
    // The reference the balance is measured from. A temperature offset of zero
    // adapts this white to itself, so the matrix is exactly the identity and the
    // plugin does nothing. The reference sits 0.0032 from the ACES white point
    // (x 0.32168, y 0.33767) in CIE 1960 uv, which is what the working space
    // calls neutral.
    const double kReferenceTemperature = 6000.0;

    // Valid range of the Planckian fit the illuminant is drawn from.
    const double kMinTemperature = 1667.0;
    const double kMaxTemperature = 25000.0;

    // The temperature control is a relative offset, not an absolute reading:
    // nothing here knows what the scene was actually lit by, so quoting an
    // absolute Kelvin figure would be pretending to a knowledge we do not have.
    //
    // Equal steps in Kelvin are not equal steps in colour, though. 100 K at the
    // warm end is a large shift and 100 K at the cool end is invisible. So the
    // offset is converted to a shift in mired, the reciprocal scale on which
    // colour temperature is close to perceptually even, using the slope of the
    // mired curve at the reference:
    //
    //   mired = 1e6 / K,  d(mired)/dK = -1e6 / K^2
    //
    // One slider unit is therefore 1e6 / 6000^2 = 0.0278 mired, so a 100 unit
    // step is 2.78 mired everywhere on the range: roughly one just noticeable
    // shift, and the same size wherever the slider happens to be.
    const double kMiredPerOffsetUnit = 1.0e6 / (kReferenceTemperature * kReferenceTemperature);

    // Asymmetric because colour temperature is. Both ends land inside the
    // Planckian fit with no dead travel: -6000 reaches 3000 K, warmer than
    // tungsten, and +4500 reaches 24000 K. In the units that matter that is
    // +166.7 and -125.0 mired.
    const double kMinTemperatureOffset = -6000.0;
    const double kMaxTemperatureOffset = 4500.0;

    // Slider units to Duv. +-100 on the tint slider spans +-0.03 Duv, which
    // covers any practical green/magenta correction while keeping the assumed
    // illuminant inside the physical region at every temperature.
    const double kTintToDuv = 0.0003;

    // The illuminant a given offset stands for. Exposed so the interface and the
    // tests can talk about the underlying temperature even though the control
    // does not show it.
    double temperatureFromOffset(double offset);

    // Builds the row-major 3x3 that white balances ACES AP1 linear.
    //
    // A positive offset warms the image and a negative one cools it, matching
    // the direction of a camera raw temperature control. Positive tint is
    // magenta.
    //
    // The plugin always passes preserveExposure true, so an AP1 neutral keeps
    // its luminance. The flag remains for tests.
    void computeMatrix(double temperatureOffset, double tint, bool preserveExposure, float outMatrix[9]);

    // Chromaticity of the assumed illuminant, exposed for the test harness.
    void illuminantXY(double temperatureOffset, double tint, double* outX, double* outY);

    // Luminance of an AP1 linear triple, the Y row of the AP1 to XYZ matrix.
    double ap1Luminance(double r, double g, double b);
}
