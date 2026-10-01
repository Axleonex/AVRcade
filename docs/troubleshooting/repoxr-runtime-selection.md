# RepoXR runtime selection — source findings (v1.2.3, commit 15b4aec)

Source read from `external/repoxr-src` (gitignored clone of
`https://github.com/DaXcess/RepoXR` tag `v1.2.3`), 2026-07-09. All paths below
are relative to that clone. READ-ONLY reference — no RepoXR code is copied,
vendored, or linked into VRClient (M1 D13 / plan D3).

## Q1: Candidate order

The loader is `OpenXR.Loader.InitializeXR()` — `Source/OpenXR.cs:310-372`. Order:

1. **Elevated process → system default only.** `Source/OpenXR.cs:328-334`:
   ```csharp
   if (Native.IsHighIntegrityLevel())
   {
       Logger.LogWarning("Application is elevated! Unable to override the XR runtime! ...");
       return InitializeXR(null);
   }
   ```
2. **Enumerate runtimes** via `GetRuntimes()` (`Source/OpenXR.cs:52-115`):
   `LocateCommonRuntimes()` (`:160-188`) probes, in order: SteamVR (via Steamworks —
   `SteamApps.IsAppInstalled(250820)` + `steamxr_win64.json` in its install dir),
   Virtual Desktop (`%ProgramFiles%\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`),
   Oculus. Then the registry `HKLM\SOFTWARE\Khronos\OpenXR\1\AvailableRuntimes`
   value names are added (`EnumRuntimeFiles`, `:136-155`), and the runtime whose
   path equals the registry `ActiveRuntime` is flagged `Default` (`:105`, `:270-271`).
3. **Config pin FIRST.** If `[Internal] OpenXRRuntimeFile` is non-empty it is tried
   before anything else — even when the path is not among the enumerated runtimes
   (used raw as name `RepoXR OpenXR Override`). `Source/OpenXR.cs:347-358`:
   ```csharp
   if (!string.IsNullOrEmpty(Plugin.Config.OpenXRRuntimeFile.Value))
   {
       var rtFound = runtimes.TryGetRuntimeByPath(Plugin.Config.OpenXRRuntimeFile.Value, out var rt);
       if (InitializeXR(rtFound ? rt : new Runtime { Name = "RepoXR OpenXR Override", Path = ... }))
           return true;
   ```
4. **Registry default (ActiveRuntime) second.** `Source/OpenXR.cs:361-363`.
5. **Every remaining enumerated runtime** in enumeration order. `Source/OpenXR.cs:365-368`.
6. First candidate whose init yields ≥1 `XRDisplaySubsystem` wins (`:393-408`) —
   the loader STOPS there. This is the black-headset root cause: VDXR initializes
   successfully even while SteamVR owns the headset panel, and VDXR is the
   registry default, so SteamVR is never attempted.

**The parenthetical in `Attempting to initialize OpenXR on VirtualDesktopXR (Bundled)`
is NOT a RepoXR source tag.** The log format is `Source/OpenXR.cs:381`:
```csharp
Logger.LogInfo($"Attempting to initialize OpenXR on {rt.Name}");
```
`rt.Name` comes from the runtime manifest's own `runtime.name` field; the local
VDXR manifest literally self-reports `"VirtualDesktopXR (Bundled)"` (verified by
reading `C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`).
The prior hypothesis that RepoXR ships a "bundled" loader of its own is FALSE.

## Q2: `OpenXRRuntimeFile` semantics

Defined `Source/Config.cs:160-161` — BepInEx entry, section `[Internal]`, key
`OpenXRRuntimeFile`, default `""`:
```csharp
public ConfigEntry<string> OpenXRRuntimeFile { get; } = file.Create("Internal", nameof(OpenXRRuntimeFile), "",
    "FOR INTERNAL USE ONLY, DO NOT EDIT");
```
Value = **full filesystem path to an OpenXR runtime manifest json**. Read once at
loader init (game boot). Honored FIRST (Q1 step 3); an unrecognized path is still
attempted raw. Matching against enumerated runtimes is **exact, case-sensitive**
string equality (`Source/OpenXR.cs:233`, `rt.Path == path`).

Q2_VALUE_FORMAT: `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json`

## Q3: Who writes the key back (why it read empty after runs)

The in-game RepoXR settings menu has an "OpenXR Runtime" slider
(`Source/UI/Settings/SettingOption.cs`). On build it maps the stored path to a
slider index via the case-sensitive `TryGetRuntimeByPath` (`:81-84`, falling back
to index 0 = "System Default") and immediately calls `UpdateSlider()` (`:86`),
which at index 0 **clears the key**, `:91-96`:
```csharp
if (slider.currentValue == 0)
{
    Plugin.Config.OpenXRRuntimeFile.Value = "";
    runtimeText.text = "System Default";
    return;
}
```
So any pinned value that does not exactly (case-sensitively) equal an enumerated
runtime path is wiped to `""` as soon as the settings UI initializes. BepInEx
persists `ConfigEntry` writes to the cfg file — hence the key "found empty after
the run" in the M1.3 evidence. Not one-shot consume; it is a UI-driven reset.

## Q4: Other override channels

- Command-line flags read in `Source/Plugin.cs`: `--disable-vr` (`:54`),
  `--repoxr-force-release-build` (`:32`), `--repoxr-enable-experiments` (`:80`),
  `--repoxr-debug-eyetracking` (`:84`). **None select a runtime.**
- The in-game settings slider (Q3) is the only other runtime-selection channel —
  it writes the same `OpenXRRuntimeFile` key.
- No environment variable is read for runtime selection (see Q5).

## Q5: `XR_RUNTIME_JSON`

Present in the DLL strings because the mod **writes** it — never reads it.
`Source/OpenXR.cs:382` (per attempt) and `:387` (cleared for the default attempt):
```csharp
Environment.SetEnvironmentVariable("XR_RUNTIME_JSON", rt.Path);
...
Environment.SetEnvironmentVariable("XR_RUNTIME_JSON", null);
```
An externally-set `XR_RUNTIME_JSON` on the spawned process is therefore
overwritten (or nulled) before Unity's OpenXR plugin initializes — which is why
the M1.3 env pin had no effect.

## Experiment results

All experiments 2026-07-09, headless (no headset session; Steam running,
SteamVR and Virtual Desktop Streamer NOT running), 60 s per launch.
`ExtendedDebugging = true` forced by the harness.

### exp-baseline (no pins)
```
[Info   :OpenXR Loader] Attempting to initialize OpenXR on VirtualDesktopXR (Bundled)
[Info   :OpenXR Loader] Attempting to initialize OpenXR on SteamVR
[Error  :OpenXR Loader] All available runtimes were attempted, but none worked. Aborting...
```
No runtime bound (headless — both inits fail without a live compositor). Order
matches Q1: registry default (VDXR) first, then the remaining enumerated
runtime (SteamVR). SteamVR IS enumerated on this machine.

### exp-cfgpin (`OpenXRRuntimeFile = C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json`)
```
[Info   :OpenXR Loader] Attempting to initialize OpenXR on SteamVR
[Info   :OpenXR Loader] Attempting to initialize OpenXR on VirtualDesktopXR (Bundled)
[Error  :OpenXR Loader] All available runtimes were attempted, but none worked. Aborting...
```
No runtime bound (headless), but **SteamVR was attempted FIRST** — the cfg pin
flipped the order, and the attempt line says `SteamVR` (not `RepoXR OpenXR
Override`), i.e. the pinned path exactly matched the enumerated SteamVR
runtime. `cfg-after-exit.txt` still held the pin 3 s after kill; by the next
launch (exp-envpin `cfg-before-launch.txt`) the key had been wiped back to
empty — consistent with the Q3 settings-UI reset via a delayed config flush or
a Steam-relaunched second game process. Per-launch re-pinning (what the client
already does) is therefore the correct write timing.

### exp-envpin (`XR_RUNTIME_JSON` set on the game process; cfg key empty)
```
[Info   :OpenXR Loader] Attempting to initialize OpenXR on VirtualDesktopXR (Bundled)
[Info   :OpenXR Loader] Attempting to initialize OpenXR on SteamVR
[Error  :OpenXR Loader] All available runtimes were attempted, but none worked. Aborting...
```
Identical to baseline — the external env var changed nothing, confirming Q5
(the mod overwrites `XR_RUNTIME_JSON` per attempt).

### E4 (system ActiveRuntime switch)
SKIPPED — no elevated shell available in this session, and decision-table
row 1 had already matched (lower row wins), so E4 could not change the
selection. Recorded in DEVIATIONS-STEAMVR.md.

SELECTED BRANCH: A
