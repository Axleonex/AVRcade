# SteamVR / Virtual Desktop runtime conflict — user guide

When SteamVR is running, a RepoXR-modded game can bind Virtual Desktop's OpenXR
runtime (VDXR) while SteamVR owns the headset display — frames go to one
compositor, the panel belongs to the other, and the headset shows black.
This page is the diagnosis map plus the fix VRClient ships for it.

## Diagnosis map

| Symptom | Check | Meaning |
|---|---|---|
| Monitor mirror tracks head movement; headset black/void with SteamVR open | `BepInEx/LogOutput.log` line `OpenXR runtime being used: ...` | If it says `VirtualDesktopXR` while SteamVR runs: two compositors — frames go to VDXR, SteamVR owns the panel. Close SteamVR, or execute this plan. |
| Headset shows only the "RepoXR by DaXcess" splash | `C:\ProgramData\Virtual Desktop\OpenXR.log` shows healthy session (controllers bound) | GPU cannot feed frames (esp. iGPU): lower VD "VR Graphics Quality" (headset-side menu) and set `CameraResolution = 50` in `BepInEx/config/io.daxcess.repoxr.cfg` `[Rendering]`. |
| Headset frozen at menu, mirror still tracks | same as above | Same GPU saturation, milder. Same fix. |
| `vrmonitor.exe ... d3dx10_43.dll was not found` popup | `Test-Path C:\Windows\SysWOW64\d3dx10_43.dll` | Legacy DirectX runtime missing. Run `"C:\Program Files (x86)\Steam\steamapps\common\Steamworks Shared\_CommonRedist\DirectX\Jun2010\DXSETUP.exe" /silent` elevated. |
| Which runtime OWNS this machine? | `(Get-ItemProperty "HKLM:\SOFTWARE\Khronos\OpenXR\1").ActiveRuntime` | Baseline captured 2026-07-08 on the development PC: ActiveRuntime = `C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`, and `AvailableRuntimes` lists ONLY that VDXR json — SteamVR has never registered as an OpenXR runtime here. This is hypothesis H1 for why RepoXR always picks VDXR. |
| What did the mod try, in order? | set `ExtendedDebugging = true` in the RepoXR cfg `[General]`, relaunch, read `LogOutput.log` `Attempting to initialize OpenXR on <name> (<source>)` lines | The attempt list + source tags ("Bundled", etc.) reveal the loader's real search order. |

> 2026-07-09 update to the last two rows: on machines where SteamVR HAS
> registered (e.g. `AvailableRuntimes` lists both manifests), the loader
> enumerates SteamVR fine but still tries the registry-default (VDXR) first —
> that ordering, not a missing registration, is the root cause. And the
> "(Bundled)" tag is just VDXR's self-reported manifest name, not a RepoXR
> source tag. Full source findings: `repoxr-runtime-selection.md`.

## Fix shipped by M1.4

**Selected branch: A — per-launch config pin.** Source reading + controlled
experiments (`repoxr-runtime-selection.md`) proved RepoXR honors its
`[Internal] OpenXRRuntimeFile` cfg key FIRST, before the registry-default
runtime. `vrclient launch` already wrote this key fresh on every launch with
the chosen runtime's manifest path; the experiments confirmed that exact
value format (`C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json`)
flips the loader's attempt order to SteamVR-first. The key is wiped back to
empty by the mod's own settings UI after a session — expected; the per-launch
re-pin makes that harmless.

User-visible behavior:

- `vrclient launch <slug> --game-dir <dir> --acknowledge --xr-runtime steamvr`
  → pins SteamVR's manifest; the mod attempts SteamVR first.
- `--xr-runtime vd` → pins VDXR the same way. No flag (`auto`) → picks by which
  runtime's processes are running.
- When SteamVR is running, the old "quit SteamVR and relaunch" warning is
  replaced by `XR: pin=applied runtime=<name>` — the pin now handles it.

## Manual one-time alternative

Open SteamVR → Settings → Developer → "Set SteamVR as OpenXR runtime". This
changes the machine's default (registry `ActiveRuntime`), so the loader's
default-first attempt hits SteamVR. Revert inside Virtual Desktop's Streamer
settings or the same SteamVR page.
