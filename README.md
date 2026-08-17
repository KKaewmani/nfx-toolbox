# NFX Toolbox

OpenFX plugins for DaVinci Resolve on macOS. One so far: **Technical Grade**, a
scene-linear grading tool covering exposure, lens falloff, white balance,
contrast and highlight/shadow limiting in ACES.

## How to install

macOS only, **DaVinci Resolve Studio** (the free version does not load third-party
OpenFX). You need the [Xcode command line tools](https://developer.apple.com/download/all/?q=command%20line%20tools)
(`xcode-select --install` if `make` is missing). The OpenFX SDK is already in
this repo.

```bash
cd TechnicalGrade
make
sudo make install
```

That builds a universal `TechnicalGrade.ofx.bundle` (Apple Silicon and Intel) and
copies it to `/Library/OFX/Plugins`, which is where Resolve looks. **Quit and
reopen Resolve** afterwards; it only scans plugins at launch. The effect appears
in the OpenFX library under **NFX Toolbox** as **Technical Grade**.

If you were given the `.ofx.bundle` itself rather than this repo, skip `make`
and copy it into the same folder, then clear Gatekeeper’s quarantine tag if the
file arrived by download, AirDrop or mail:

```bash
sudo cp -R TechnicalGrade.ofx.bundle /Library/OFX/Plugins/
sudo xattr -dr com.apple.quarantine /Library/OFX/Plugins/TechnicalGrade.ofx.bundle
```

`scp`, `rsync` or a USB stick do not set that tag. Restart Resolve either way.

| | |
| --- | --- |
| `TechnicalGrade/` | The plugin: source, tests and its own detailed README |
| `OpenFX-1.4/`, `Support/` | Vendored OpenFX SDK, BSD-3-Clause, so the repo builds standalone |

## Technical Grade

Appears under **NFX Toolbox** in Resolve's OpenFX library. Current version 2.3.

The plugin decodes to scene-linear ACES AP1, does every operation there, and
re-encodes on the way out, so each control does the thing its name suggests
rather than something distorted by a log curve.

**Working Space** — ACEScct, ACEScc, Linear AP1, ARRI LogC3 (EI 800), ARRI LogC4,
Sony S-Log3, Canon C-Log3 or RED Log3G10. Camera logs are converted to AP1 for
the grade (and back on the way out) so white balance stays an AP1 Bradford CAT.
Everything downstream happens in linear whichever you pick.

**Exposure**, -6 to +6 EV. A linear gain of 2^EV applied to all three channels.
**Lens Falloff (Vignette)**, -4 to +4 EV, in the same Exposure section. A radial
exposure change: nothing at the centre, the full amount at the corners, following
`2^(EV * r^2)`. That profile is the physical `cos^4` falloff law to second order,
so positive values genuinely cancel a lens that darkens the corners and negative
values emulate one. The radius is measured with the pixel aspect ratio applied,
so it stays circular on anamorphic material instead of stretching to the format.

**White Balance**, Temperature and Tint, both -100 to +100 per notch. Temperature
is a relative offset rather than an absolute Kelvin reading, because nothing here
knows what the scene was lit by. It steps in mired, so the same movement shifts
colour by the same perceptual amount anywhere on the range. Bradford chromatic
adaptation underneath. **Preserve Exposure** holds neutral luminance so the
control changes colour without changing brightness.

**Contrast**, Middle Grey 0.045 to 0.72 (default 0.18, the linear pivot; ends are
±2 stops), and Contrast -1 to +1 in stops. Contrast is the slope in log2
exposure, so 0 leaves the image alone and +1 doubles the stops between any two
tones.

**Limiters**, Highlight +2 to +8 EV and Shadow -8 to -2 EV, each with a softness
of 0.2 to 1. A tanh sigmoid that leaves the knee at exactly the slope it arrived
with and approaches the limit asymptotically, so there is no flat clipped region
and no visible crease at the join.

Two behaviours worth knowing in use. Negative values, which AP1 legitimately
carries in deep shadows and outside the gamut, are never driven further negative
by contrast or the shadow floor. And because the falloff reads the pixel's
position, the effect declares itself spatially aware, so Resolve will not bake it
into a LUT even with the falloff at zero; measured on an M3 Pro that costs
nothing at 4K, where the kernel is bandwidth-bound rather than arithmetic-bound.

## Building

Requires macOS and the Xcode command line tools. Install is in **How to install**
above. `make test` runs the full check suite without Resolve.

```bash
cd TechnicalGrade
make
make test
```

See [`TechnicalGrade/README.md`](TechnicalGrade/README.md) for the colour science,
the parameter reference and how the shared CPU/GPU maths is arranged.

## Platforms

The plugin uses Metal, so **`make` only works on macOS**. You can clone, read,
and edit the source on any OS. Push and pull over git as usual; there is no
need to copy working folders or git bundles between machines.

## Commit identity

Do not put a personal email in this repository. For public commits, use the
noreply address GitHub shows under **Settings → Emails** (keep “Block command
line pushes that expose my email” on if you use that option):

```bash
git config user.name  "your-github-username"
git config user.email "ID+username@users.noreply.github.com"
```

## Licence

The plugin source in `TechnicalGrade/` is BSD-3-Clause; see `LICENSE`.

The vendored OpenFX headers and support library are also BSD-3-Clause, copyright
The Open Effects Association; see `Support/LICENSE`.

Two files are an exception worth knowing about. `OpenFX-1.4/include/ofxParamExt.h`
and `ofxImageEffectExt.h` carry no copyright or licence notice, and are the only
headers here that do not exist in the upstream OpenFX project. They are
Blackmagic Design extensions covering Resolve-specific properties, shipped inside
DaVinci Resolve under `Developer/OpenFX/`, and the support library includes them,
so the build needs them. That folder ships with no licence terms of its own.
