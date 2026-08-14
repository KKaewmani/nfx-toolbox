# NFX Toolbox

OpenFX plugins for DaVinci Resolve on macOS. One so far: **Technical Grade**, a
scene-linear grading tool covering exposure, lens falloff, white balance,
contrast and highlight/shadow limiting in ACES.

| | |
| --- | --- |
| `TechnicalGrade/` | The plugin: source, tests and its own detailed README |
| `OpenFX-1.4/`, `Support/` | Vendored OpenFX SDK, BSD-3-Clause, so the repo builds standalone |

## Technical Grade

Appears under **NFX Toolbox** in Resolve's OpenFX library. Current version 2.1.

The plugin decodes to scene-linear ACES AP1, does every operation there, and
re-encodes on the way out, so each control does the thing its name suggests
rather than something distorted by a log curve.

**Working Space** — ACEScct, ACEScc or Linear AP1. Everything downstream happens
in linear whichever you pick.

**Exposure**, -6 to +6 EV. A linear gain of 2^EV applied to all three channels.

**Lens Falloff (Vignette)**, Corner Exposure -4 to +4 EV. A radial exposure
change: nothing at the centre, the full amount at the corners, following
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

**Contrast**, Middle Grey -2 to +2 EV around 0.18, and Contrast -1 to +1. Both in
stops. Contrast is the slope in log2 exposure, so 0 leaves the image alone and +1
doubles the stops between any two tones.

**Limiters**, Shadow -8 to -2 EV and Highlight +2 to +8 EV, each with a softness
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

Requires macOS and the Xcode command line tools. Nothing else needs installing:
the SDK is in the repository.

```bash
cd TechnicalGrade
make                # universal arm64 + x86_64 bundle
make test           # full check suite, no Resolve needed
sudo make install   # into /Library/OFX/Plugins
```

Restart Resolve afterwards; it only enumerates plugins at launch. See
[`TechnicalGrade/README.md`](TechnicalGrade/README.md) for the colour science,
the parameter reference and how the shared CPU/GPU maths is arranged.

## Working across two machines

The plugin renders through Metal, so it **only builds on macOS**. That splits
development across two machines:

- **The Mac builds.** It clones and pulls this repo anonymously, because the
  repo is public, and has no credentials for the GitHub account that owns it.
  It can edit, build, test and commit, but not push.
- **The PC publishes.** It holds the SSH key for the account and is the only
  machine that can push.

Commits therefore travel from the Mac to the PC by hand.

### Moving commits to the PC

Commit on the Mac as usual, then package the branch into a single file:

```bash
git bundle create ~/Desktop/update.bundle main
```

Copy that one file across on a USB stick. Then, inside the existing clone on the
PC:

```powershell
git pull D:\update.bundle main
git push
```

A bundle is a whole branch's history in one file, a couple of hundred kilobytes
for this project. `git pull` fast-forwards the PC onto it, leaving that clone's
config and remote untouched.

**Do not copy the working folder across and overwrite the other one.** It drags
along build artefacts and the `.git` directory, wipes out the destination's own
configuration, and silently destroys any commit the destination had that the
source did not. The bundle merges histories; a folder copy replaces one with the
other.

### Bringing the PC up to date on the Mac

Anything pushed from the PC comes back down normally, since the repo is public
and needs no credentials to read:

```bash
git pull
```

## Commit identity

This repository sets its author name and email locally, overriding whatever the
machine's global git config says, so commits are attributed to the right account
regardless of which machine they were made on:

```bash
git config user.name  "Kritsada Kaewmani"
git config user.email "kritsadakaewmani@gmail.com"
```

Worth checking with `git config user.email` after cloning onto a new machine.
Fixing the author of a commit after the fact means rewriting history.

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
