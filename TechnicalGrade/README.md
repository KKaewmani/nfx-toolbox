# Technical Grade

An OpenFX plugin for DaVinci Resolve that does exposure, lens falloff, white
balance, contrast and highlight/shadow limiting where those operations actually
belong: in scene linear ACES AP1, with the log encoding taken off at the start
and put back at the end.

Part of the **NFX Toolbox** group. Metal on the GPU, a multithreaded fallback on
the CPU. Both run the same source text, so they cannot drift apart.

## Building

The OpenFX headers and the C++ support library are vendored in `../OpenFX-1.4`
and `../Support`, copied out of the Resolve developer package, so nothing else
needs installing beyond the Xcode command line tools.

```bash
make            # builds TechnicalGrade.ofx.bundle, universal arm64 + x86_64
make test       # runs the whole check suite described below
sudo make install
```

`make install` copies the bundle to `/Library/OFX/Plugins`, which is where
Resolve scans on macOS. Restart Resolve afterwards; it only enumerates plugins at
launch. The effect appears in the OpenFX library under **NFX Toolbox** as
**Technical Grade**.

If you installed the earlier `ExposureBalance` bundle, delete it. The rename
changed the plugin identifier, so Resolve treats the two as unrelated effects and
will happily list both.

## Installing on another machine

`TechnicalGrade.ofx.bundle` is the whole plugin. It links nothing but system
frameworks and is universal, so the same bundle runs on Apple Silicon and Intel
with no toolchain, no runtime and no build on the far end. Copy it into
`/Library/OFX/Plugins` and restart Resolve.

```bash
sudo cp -R TechnicalGrade.ofx.bundle /Library/OFX/Plugins/
xattr -dr com.apple.quarantine /Library/OFX/Plugins/TechnicalGrade.ofx.bundle
```

Three things decide whether it appears:

- **Resolve Studio.** OpenFX plugins are a Studio feature. The free version will
  not list this or any other third-party effect.
- **Quarantine.** Anything that arrives by AirDrop, mail, download or a cloud
  folder gets tagged `com.apple.quarantine`, and Gatekeeper then refuses to load
  it into Resolve without saying so. The `xattr` line above clears it, and is
  harmless when there was no tag. A USB stick, `scp` or `rsync` never sets it.
- **macOS version.** The build targets 11.0 and up, set explicitly in the
  Makefile. Left unset it silently inherits the SDK of whichever machine built
  it, which produces a bundle that refuses to load on anything older with no
  diagnostic at all.

The bundle carries only the ad-hoc signature the linker applies. That is enough
to run, and enough for passing it around a facility. Distributing it publicly
would need a Developer ID signature and notarisation.

## The pipeline

```
input (working-space encoding)
  -> decode the transfer function to camera-linear RGB
  -> 3x3 camera RGB -> ACES AP1              ] identity for ACEScct / ACEScc / Linear
  -> exposure gain, 2^EV                     ] folded into one 3x3
  -> lens falloff, a radial gain             ] and a scalar
  -> white balance, Bradford adaptation      ]
  -> log2 exposure relative to the pivot
  -> contrast, a slope in that log space
  -> shadow limiter, sigmoid
  -> highlight limiter, sigmoid
  -> back to linear AP1
  -> 3x3 AP1 -> camera RGB
  -> re-encode to the working space
```

Exposure and white balance are both linear operations, so they collapse into a
single 3x3 matrix that is computed once per frame on the CPU. The falloff sits
between them but is one scalar applied to all three channels, and a scalar
commutes with a matrix, so it is applied to the result rather than to the input.
Camera-to-AP1 and AP1-to-camera stay as their own multiplies because the tone
curve between them is nonlinear. The kernel does three matrix multiplies, one
radial gain and three scalar tone curves per pixel.

Alpha is passed through untouched.

## Parameters

| Control | Range | Default | Notes |
| --- | --- | --- | --- |
| Working Space | ACEScct, ACEScc, Linear AP1, LogC3 EI 800, LogC4, S-Log3, C-Log3, Log3G10 | ACEScct | Encoding in and out. Camera logs are converted to AP1 for the grade |
| Exposure | -6 to +6 EV | 0 | Linear gain of 2^EV |
| Corner Exposure | -4 to +4 EV | 0 | Lens falloff. Positive opens the corners up, negative darkens them |
| Temperature | -6000 to +4500 | 0 | Relative warm/cool trim, positive warms |
| Tint | -100 to +100 | 0 | Positive is magenta, negative is green |
| Preserve Exposure | on/off | on | Holds neutral luminance across the balance |
| Middle Grey | 0.045 to 0.72 | 0.18 | Linear pivot; ends are ±2 stops from 0.18 |
| Contrast | -1 to +1 | 0 | Stops of slope, so 0 is unchanged |
| Shadow Limiter | off/on | off | |
| Shadow Limit | -8 to -2 EV | -8 | Floor, in stops below middle grey |
| Shadow Softness | 0.2 to 1 | 0.5 | 1 rolls off all the way from middle grey |
| Highlight Limiter | off/on | off | |
| Highlight Limit | +2 to +8 EV | +8 | Ceiling, in stops above middle grey |
| Highlight Softness | 0.2 to 1 | 0.5 | 1 rolls off all the way from middle grey |

At their defaults every control is neutral, and the plugin reports itself as a
pass through so Resolve skips it entirely.

Exposure and Contrast are set in stops. Middle Grey is the linear pivot itself,
so the slider reads 0.18 at rest rather than 0 EV around 0.18. The ends are still
two stops either side (0.045 and 0.72).

## Colour science notes

### Working spaces

ACEScct and ACEScc use the shared log encoding `(log2(lin) + 9.72) / 17.52`, and
differ only in how they treat the bottom end: ACEScct has a linear toe below
0.0078125 that meets the log segment at 0.155251, ACEScc has an offset log
segment instead. Both saturate at the half float ceiling of 65504. The encode and
decode functions here are exact inverses of one another to within float32
rounding, which is a few parts per million, or about 3e-6 of a stop.

Pick the setting that matches your Resolve colour science. If the node is fed
linear AP1 directly, choose Linear and no encoding is applied.

Camera logs use the vendor curve, then a 3x3 from that camera RGB into ACES AP1
(CAT02, matching the ACES IDT convention) so the grade itself always runs in
AP1. The inverse 3x3 and the same curve go back on the way out. LogC3 is the
EI 800 curve only. S-Log3 is paired with S-Gamut3.Cine, C-Log3 with Cinema
Gamut, LogC4 with AWG4, LogC3 with AWG3, Log3G10 with REDWideGamutRGB.

### White balance

The Temperature control is a **relative trim, not an absolute reading**. Nothing
in a grading node knows what the scene was actually lit by, so printing a figure
like "5600 K" would claim a knowledge the plugin does not have and would not
describe the colour of the image either. Zero means no change; positive warms,
negative cools.

Equal steps in Kelvin are not equal steps in colour, though. A hundred Kelvin at
the warm end is a large shift and a hundred at the cool end is invisible. So the
control walks along the **mired** scale, the reciprocal of temperature, on which
colour temperature is close to perceptually even:

```
mired = 1e6 / K,   d(mired)/dK at 6000 K = 1e6 / 6000^2 = 0.0278 mired per unit
```

One hundred units is therefore 2.78 mired anywhere on the range, which is roughly
one just noticeable shift. Near the middle of the slider a hundred units really
is a hundred Kelvin, which is what makes the number worth reading at all; out at
the ends it stays a hundred units of the same visual size while the underlying
Kelvin figure stretches, exactly as colour temperature does.

The range is asymmetric because colour temperature is. In the units that matter
it is symmetric enough: -6000 is +166.7 mired and reaches 3000 K, warmer than
tungsten, while +4500 is -125.0 mired and reaches 24000 K. Both ends land inside
the range the Planckian fit is defined over, so there is no stretch of slider at
either end that does nothing.

Mechanically:

1. The offset is turned into an assumed illuminant temperature through the mired
   relation above, then into a chromaticity on the Planckian locus using the Kim
   et al. cubic fit.
2. Tint offsets that point perpendicular to the locus in CIE 1960 uv. The full
   +-100 spans +-0.03 Duv.
3. A von Kries adaptation in the Bradford cone space maps the assumed illuminant
   to the neutral reference, and the result is composed into AP1:
   `M = AP1_from_XYZ . Bradford^-1 . diag(LMS_ref / LMS_assumed) . Bradford . XYZ_from_AP1`.
4. With Preserve Exposure on, the matrix is divided by the AP1 luminance of the
   adapted neutral, so a grey card holds its brightness while its colour moves.

The neutral reference is the Planckian white at 6000 K. It is 0.0032 from the
ACES white point in CIE 1960 uv, which the test suite asserts, so in practice the
reference is the ACES white. At a zero offset and zero tint the matrix is
short circuited to a bit exact identity: running the neutral case through the
matrix chain leaves around 1e-9 off the diagonal, and that is enough to leak a
trace of one channel into another and turn a true zero into a small positive,
which then takes the wrong branch of the tone curve.

Because the correction is a full 3x3 rather than three independent gains, it
accounts for the cross terms a chromatic adaptation actually has.

Very large tints at very low temperatures would push the assumed illuminant off
the edge of the physical spectral region, where the adaptation matrix stops
meaning anything and can produce negative gains. When that happens the tint is
quietly pulled back to the largest value that keeps all three tristimulus values
positive, so the slider saturates instead of breaking.

### Lens falloff

An exposure gain that varies with distance from the centre of the frame: nothing
at the centre, the full amount set by the control at the far corners.

```
gain(r) = 2^(EV * r^2)
```

where `r` is 0 at the centre and 1 in the corners. Positive EV opens the corners
up, which is what cancels a lens that darkens them; negative EV darkens them,
which is the same thing used creatively.

The `r^2` profile is not a guess. Natural vignetting follows the `cos^4` law, and
over the field angles a real lens works across, `log2(cos^4 t)` is proportional
to `r^2` to second order. So the curve is the physical falloff near the centre.
It also has the property that matters visually: zero slope at the centre, so
there is no crease in the middle of frame the way a profile linear in `r` gives.

`r` is measured in real geometry, not in samples. Offsets from the centre are
scaled by the pixel aspect ratio before the radius is taken, so the falloff stays
circular on anamorphic or non-square-pixel material rather than being stretched
to the shape of the format. It is then divided by the distance to the corner,
which is what puts the full amount exactly in the corners on any frame size.

Because it reads the pixel's position, this is the one part of the plugin that is
not a pure colour transform. The effect therefore declares itself spatially aware
and Resolve will no longer bake it into a LUT, even when the falloff is at 0.
That flag is fixed when the plugin is described and cannot be made conditional on
a parameter value.

That trade was made deliberately. The falloff itself is free on the GPU: over a
4K frame on an M3 Pro the kernel takes 2.29 ms whether the falloff is on or off,
the difference between the two changing sign from run to run. At that size the
kernel is bandwidth-bound, moving around 265 MB per frame at roughly 116 GB/s, so
the handful of extra instructions fit inside the wait on memory. The single
threaded CPU fallback, where there is no memory wall to hide behind, costs about
8% more. Losing the LUT is worth that little.

Working in linear means the corner correction is a real exposure change and
composes with everything downstream exactly as an extra stop from the Exposure
slider would. The falloff runs before the limiters, so a corner lifted past the
highlight ceiling is rolled off along with everything else rather than clipping.

### Contrast

Contrast is a slope applied to log2 exposure measured from the pivot:

```
e  = log2(linear / pivot)
e' = e * slope
```

The pivot is a fixed point for any slope, which is the property that makes middle
grey stay put while everything else fans out around it.

The pivot is a linear number (0.18 at rest). Contrast is the slope in stops,
so the slider value `c` gives a slope of `2^c`: 0 leaves the image alone, +1
doubles the stops between any two tones, -1 halves them. Setting it this way
makes the control symmetric, so +0.5 and -0.5 are equal and opposite, which a
linear slope slider is not: on that scale the neutral 1.0 sits a quarter of the
way along and half the travel does nothing much.

The slider stops at ±1, a slope of 0.5 to 2. The kernel will take any slope, and
the tests still exercise it from 0.25 to 4, but nothing past a stop was usable in
practice and carrying the unusable ends cost precision across the part of the
range that gets touched.

Middle Grey is the linear pivot: 0.18 at rest, 0.045 two stops down, 0.72 two
stops up. Moving it moves both the contrast fulcrum and the origin the limiters
measure from. The kernel still works in stops relative to that number.

### Limiters

Both limiters work in EV relative to the pivot. Given a limit `L` and a softness
`s`, the knee sits at `t = L * (1 - s)`, and past it the curve is

```
y = t + (L - t) * tanh((e - t) / (L - t))
```

The derivative of `tanh` is 1 at the origin, so the curve leaves the knee at
exactly the slope it arrived with and the join is smooth rather than a visible
crease. It approaches the limit asymptotically and never reaches it, so there is
no flat clipped region. Softness 1 starts the roll-off all the way back at middle
grey. The shadow limiter is the exact mirror image, which the tests check
numerically.

Two things are kept out of reach of the interface, both for the same reason. The
softness sliders stop at 0.2 rather than 0, because a softness of 0 is a genuine
hard clip and the one setting where the curve degenerates: the knee has no width
and the tanh has nothing to divide by. The limits stop two stops short of middle
grey, at -8 to -2 and +2 to +8, because a limit sitting on the pivot would clamp
the whole image to a single tone, and one approaching it does the same thing
gradually.

Together those bounds put the narrowest reachable knee at `|L| * s = 2 * 0.2`, or
0.4 EV, which is nowhere near degenerate. The hard clip branch stays in the code
because it is four lines and keeps the maths total for any input, but nothing the
interface can produce reaches it. This is the whole guard: two slider ranges, and
no special cases anywhere in the kernel.

Because the roll-off is asymptotic, at high softness a very dark tone lands a
little above the shadow floor rather than exactly on it. That is the curve
working as intended, not a failure to reach the limit.

### Negative values

Log space is only defined above zero, but AP1 legitimately carries small
negatives in deep shadows and in saturated colours outside the gamut, so they
have to be dealt with rather than clamped away.

The rule is that **a value at or below zero is darker than any floor the shadow
limiter can set**, because every floor is a positive linear value. So either the
limiter is on and the value lands on the floor, or it is off and the value passes
through untouched. The tone curve never runs on a negative. Both branches agree
with the limit of the positive branch as it approaches zero, so the operator
stays continuous and monotonic across zero:

- limiter off: `pivot * 2^(log2(v/pivot) * slope)` tends to 0 as `v` tends to 0
- limiter on: the low clip is asymptotic to the shadow limit, so the result tends
  to `pivot * 2^shadowLimit`, which is the floor itself

The obvious alternative, mirroring the curve through the origin so the tone maps
the magnitude and the sign is restored afterwards, looks like it preserves
negatives and does not. It applies the tone to the magnitude, so anything that
pulls magnitudes towards the pivot, such as a contrast below 1 or the shadow
floor, drives a negative *away* from zero rather than towards it. In this plugin
that turned an ACEScct 0.0 into -0.77 at a contrast of 0.25. The test suite now
sweeps every combination of contrast, pivot, limiter state and softness across
the whole legal ACEScct range and asserts that none of them can produce a
negative code value.

Positive magnitudes below 1e-10, around -33 EV, are floored before the log.
Nothing in the limiter range gets anywhere near that.

## Layout

| File | |
| --- | --- |
| `ColorMathBody.h` | All the per-pixel maths. The single source of truth |
| `ColorMath.h` | C++ prelude: binds the `CM_*` macros to libm and includes the body |
| `ColorMathSource.h` | Generated. The body wrapped in a raw string literal for Metal |
| `MetalSource.h` | Metal prelude, the kernel, and the assembly of the full shader |
| `MetalKernel.mm` | Metal dispatch and the pipeline state cache |
| `KernelParams.h` | The 41 float parameter block shared by both paths, and the falloff geometry |
| `ColorSpaces.h/.cpp` | Camera RGB <-> AP1 3x3s from chromaticities and CAT02. CPU only |
| `WhiteBalance.h/.cpp` | Mired, CCT, Duv and Bradford. CPU only, once per frame |
| `TechnicalGradePlugin.h/.cpp` | OFX factory, parameters, render, CPU path |

`ColorMathBody.h` is written in the intersection of C++ and the Metal Shading
Language: no includes, no namespaces, no `std::`, floats only, and every maths
call routed through a `CM_*` macro. The C++ prelude binds those macros to `log2f`
and friends; the Metal prelude binds them to the MSL equivalents. The Makefile
wraps the body in a raw string literal so the runtime shader compiler receives
character for character what the C++ compiler saw. There is no second copy of the
maths to forget to update.

The slider-to-kernel conversions live in the plugin, not the kernel. Contrast
stops become a slope, and the temperature offset becomes a matrix, all once per
frame on the CPU, so the interface can be reshaped without touching the
per-pixel code. Middle Grey is already linear and is passed through.

## Tests

```bash
make test
```

Four stages, all runnable without Resolve:

- **`test_pipeline`** exercises the shared maths directly, around 42,300 checks:
  transfer function round trips in both directions, identity at defaults
  including negatives, exposure landing on exact powers of two, the pivot as a
  fixed point across every pivot and slope combination, the negative handling
  described above, white balance direction and luminance preservation, matrices
  staying finite and positive at every temperature and tint extreme, the
  temperature scale being evenly spaced in mired with no dead travel at either
  end, the falloff hitting its set amount in all four corners while leaving the
  centre bit-exact and staying circular under a pixel aspect ratio, and the
  limiters for boundedness, monotonicity, mirror symmetry and a smooth knee.
- **`compile_metal`** compiles the kernel through the Metal runtime. This is the
  check that the shared body really is valid MSL. It uses the runtime compiler
  rather than the offline `metal` tool so it works without the Xcode Metal
  toolchain component installed.
- **`parity_metal`** runs the real kernel over 2048 pixels for nine parameter
  sets and compares against the CPU path pixel for pixel. It links the plugin's
  own `MetalKernel.mm` and reproduces the buffer-handle-as-float-pointer
  convention Resolve uses, so it drives the identical dispatch. Since the falloff
  went in, this also checks that both paths address pixels from the same origin
  with the same half-sample offset. Worst observed divergence is around 4e-6
  relative, which is the difference between the GPU's transcendentals and libm's.
- **`load_plugin`** opens the built bundle with `dlopen` the way a host does and
  checks it exports the OFX entry points and registers one plugin with the
  expected identifier.

The parity test is the one that pays for itself. It caught a stale generated
header during development, where the GPU was compiling an older version of the
maths than the CPU: a bug that would have shown up in Resolve only as shadows
behaving differently depending on which render path was active.
