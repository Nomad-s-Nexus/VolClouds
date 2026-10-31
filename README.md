# VolClouds

Volumetric cloud layer for Fallout 4. An F4SE plugin that ray-marches a procedural cloud layer
inside the engine's sky pass, depth-tested against terrain and driven by the game's sun and weather.

**Status: M1 preview - concept-level and unverified in game. Disabled by default.**

## What it does

- Hooks the sky shader's geometry setup (`BSSkyShader::SetupGeometry`) through the engine vtable.
- On the last vanilla cloud-layer pass, draws one fullscreen triangle into the already-bound scene
  colour + depth targets. Terrain and mountains occlude the clouds through the depth test.
- Density is generated procedurally (2-octave 3D value noise) between two configurable heights, so
  no cloud textures or models are required.
- Lighting uses the engine's sun direction and tint (`Sky::sun`, `Sky::skyColor`) and the current
  weather blend (`Sky::currentWeatherPct`, `TESWeather::cloudAlpha`), plus wind drift and a lightning
  flash term.

## Requirements

| Runtime | Version | F4SE |
|---|---|---|
| Old Gen | 1.10.163 | 0.6.23 |
| Anniversary | 1.11.240 | 0.7.9 |

New Gen 1.10.980-1.10.984 is not a target.

## Install

1. Copy `VolClouds.dll` into `Data/F4SE/Plugins/`.
2. Copy `clouds.json` into `Data/F4SE/Plugins/VolClouds/clouds.json` and set `"bEnabled": true`.
3. Launch the game through F4SE.

Log: `Documents/My Games/Fallout4/F4SE/VolClouds.log`.

## Configuration

`clouds.json` keys:

| Key | Meaning | Default |
|---|---|---|
| `bEnabled` | master switch | `false` |
| `fDensity` | cloud density scale | `1.6` |
| `fCoverageScale` | weather coverage multiplier | `1.0` |
| `fLayerBottom` / `fLayerTop` | cloud layer heights (world Z) | `3500` / `9000` |
| `iSteps` | raymarch steps (4-64) | `24` |
| `fSunIntensity` / `fAmbient` | lighting scale | `2.5` / `0.22` |
| `fWindDrift` | wind scroll multiplier | `1.0` |
| `fMaxDistance` | max ray distance | `60000` |
| `fBlendAlpha` | overall opacity | `1.0` |
| `fNoiseScale` | noise frequency | `1.0` |
| `iDepthMode` | `0` auto, `1` standard Z, `2` reversed Z | `0` |

## Build

Requires xmake and the MSVC x64 toolchain, plus a CommonLibF4 checkout:

```powershell
$env:COMMONLIBF4_PATH = "C:\path\to\commonlibf4-nomad"
xmake f -p windows -a x64 -m release -y
xmake build -y VolClouds
```

The DLL is written to `build/release/VolClouds.dll`. CI does the same on `windows-latest`, cloning
`G-A-R-D-E-N/commonlibf4-nomad` at a pinned commit.

## Troubleshooting

Check `Documents/My Games/Fallout4/F4SE/VolClouds.log` first, and `VolClouds.bootstrap.log` next to it
(it records whether F4SE asked the plugin to load at all).

| Symptom | Meaning |
|---|---|
| No `VolClouds.log` and no `VolClouds.bootstrap.log` | F4SE never loaded the DLL. Check `f4se.log`: a `no version data` line means the DLL is rejected (this build exports `F4SEPlugin_Version`, so update to 0.1.1+); also confirm the DLL is really in `Data/F4SE/Plugins/`. |
| `bootstrap.log` has `query: supported=0` | Unsupported game runtime (only OG 1.10.163 and AE 1.11.240 are loaded). |
| Log has `Hook fired but bEnabled=false` | Config found but disabled: set `"bEnabled": true` in `Data/F4SE/Plugins/VolClouds/clouds.json`. |
| Log says `No config at ...` | Config missing from `Data/F4SE/Plugins/VolClouds/clouds.json`. |
| Log has `Sky mode N ... clouds skipped` | Sky is not in dome/full mode (interior or menu scene). |
| Log has `Waiting for last cloud layer ...` | Hook fires, but the pass filter never matched (record the line and report it). |
| Log has `Bound target rejected: ...` | The pass is running on a non-main target or unexpected format; the line prints format and size. |
| Log has `First draw: ...` and statistics every ~600 calls | The pass is rendering; tune density/coverage/depth mode. |

## Known limitations

- Not yet verified in game; sun direction, depth convention and density are expected to need a
  tuning pass on first run.
- No depth read yet: clouds occlude correctly but do not soften against terrain silhouettes.
- Full-resolution raymarch; half-resolution + temporal upsampling is the next milestone.
- AE 1.11.240 compiles through the same code path but is untested.
- Cloud models/textures are not sampled; a model or texture source can replace the procedural
  density function later.

## License

GPL-3.0. Depends on [CommonLibF4](https://github.com/G-A-R-D-E-N/commonlibf4-nomad) (GPL-3.0).
