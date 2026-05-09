# DodgeBlinkShout

SKSE/CommonLibSSE-NG plugin plus Papyrus bridge for a 3-tier dodge/blink system.

## Nexus Releases

- NADS (main mod): `https://www.nexusmods.com/skyrimspecialedition/mods/174186`
- DMCO optional animation asset pack (`DMCO for NADS`): `https://www.nexusmods.com/skyrimspecialedition/mods/175129?tab=files`

## Quick Install (Nexus Users)

1. Install NADS from Nexus.
2. If using TK takeover, install `TK Dodge for RE` animation assets.
3. If using DMCO takeover, install either:
   - Full `Dodge - MCO (DMCO)` setup, or
   - The optional `DMCO for NADS` asset pack from the DMCO files tab.
4. Run Nemesis and enable the NADS takeover patch for your provider:
   - `NADS Takeover Mode (TK Dodge)`, or
   - `NADS Takeover Mode (DMCO)`.
5. In NADS MCM, enable takeover and choose the provider/style you want.

## Current State (Repo Snapshot)

- Native plugin version in source: `1.8.2` (`SKSEPluginInfo`) with Papyrus `GetVersion()` returning `802`.
- Activation modes implemented: `Shout` (`0`), `Spell/Power` (`1`), `Hotkey` (`2`).
- Bundled INI default activation mode: `Hotkey` (`ActivationMode=2`).
- Tier source by activation mode (from INI/comments + native handling):
  - `Spell`: spell record tier.
  - `Shout`: held shout word (`1/2/3`).
  - `Hotkey`: `HotkeyTier`, fired on key release when held <= `HotkeyTapMaxMs`.
- Takeover hotkey behavior:
  - Takeover bypasses `CD1..3` cooldown delay and is gated by active dash + stamina checks.
  - If hotkey is triggered during an active takeover dodge, one extra dodge is queued for follow-up after the current takeover dodge fully ends.
  - Queue is single-slot: additional mid-dodge queue attempts are ignored until that queued dodge is consumed.
- Dash timing model in native controller path: effective duration is constrained by both configured dash time and speed cap (`DashMaxSpeed`).
- Runtime config API is exposed to Papyrus:
  - `SetFloatConfig`, `SetIntConfig`, `GetFloatConfig`, `GetIntConfig`, `SaveConfig`, `ReloadConfig`.
- Dodge i-frames are handled in native code and can be toggled with `DodgeIFramesEnabled`.
- Takeover routing is implemented with providers for TK (`TakeoverProvider=0`) and DMCO (`TakeoverProvider=1`).
- OAR templates are bundled under `Data/Meshes/OpenAnimationReplacer/NADS/`, and no OAR dodge HKX clips are bundled in that tree.

## Runtime Requirements

- Skyrim Special Edition/Anniversary Edition runtime compatible with SKSE setup.
- SKSE64 (`skse64_loader.exe` workflow).
- Address Library for SKSE Plugins.
- SkyUI (required for MCM).
- Open Animation Replacer (optional, only for OAR animation template usage).
- Takeover mode:
  - Requires Nemesis behavior generation.
  - TK provider requires `TK Dodge for RE` animation assets (HKX files).
  - DMCO provider requires DMCO behavior + directional HKX assets, provided by either:
    - full `Dodge - MCO (DMCO)`, or
    - the optional `DMCO for NADS` asset pack.
  - DMCO takeover is third-person only; first-person attempts are blocked.
  - DMCO takeover routes 8-way direction values and supports DMCO style set selection (`-1` / `-2` HKX sets) from NADS MCM.
  - DMCO conflict guard:
    - If full DMCO is installed, keep DMCO above NADS in MO2's left pane so NADS can overwrite DMCO MCM/config files.
    - NADS forces `DodgeFramework` input off (`bUseSprintButton=0`, `uDodgeKey=-1`) and locks DMCO MCM pages to "Handled by NADS".
  - NADS ships takeover Nemesis behavior patches under:
    - `Data/Nemesis_Engine/mod/tkuc`
    - `Data/Nemesis_Engine/mod/dmco`
- Nemesis checkbox labels:
  - `NADS Takeover Mode (TK Dodge)`
  - `NADS Takeover Mode (DMCO)`

## Project Layout

- Native plugin source: `src/DodgeBlink.cpp`
- Build configuration: `CMakeLists.txt`, `CMakePresets.json`
- Runtime config: `Data/SKSE/Plugins/DodgeBlinkShout.ini`
- Papyrus runtime sources:
  - `Data/Scripts/Source/DodgeBlinkNative.psc`
  - `Data/Scripts/Source/DBS_Blink_AME.psc`
  - `Data/Scripts/Source/DodgeBlink_GenericAME.psc`
- MCM source scripts:
  - `Data/Scripts/Source/DodgeBlink_MCN.psc`
- Build/deploy helpers:
  - `tools/CompilePapyrus.ps1`
  - `tools/StageTestBuild.ps1`
- OAR template path (optional):
  `Data/Meshes/OpenAnimationReplacer/NADS/`

## Build (Recommended)

Prereqs:

- Visual Studio 2022 (Desktop C++)
- CMake 3.25+
- vcpkg toolchain (default assumed at `C:\vcpkg`)

Commands:

```powershell
$env:VCPKG_ROOT='C:\vcpkg'
cmake --preset vs2022-release-vcpkg-static
cmake --build --preset build-vs2022-release-vcpkg-static
```

Result:

- `build/vs2022-release-vcpkg-static/Release/DodgeBlinkShout.dll`
- Auto-copied to `Data/SKSE/Plugins/DodgeBlinkShout.dll`

Note:

- Dynamic preset output is staged to `Data/SKSE/Plugins/_dynamic` and is not the default deploy artifact.

## Stage To Game

After building the plugin and compiling Papyrus:

```powershell
.\tools\StageTestBuild.ps1 -GameDataPath "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\Data"
```

Staging behavior:

- Prefers static plugin DLL from `build/vs2022-release-vcpkg-static/Release/`
- Falls back to `Data/SKSE/Plugins/DodgeBlinkShout.dll` only if static artifact is missing
- Copies current INI and `.pex` scripts
- Copies local OAR animation framework folders (if present), including `OpenAnimationReplacer/NADS`
- Copies bundled takeover framework overrides, including Nemesis (`tkuc`/`dmco`) and DMCO MCM conflict-guard configs
- Removes legacy DAR custom-condition output path (`Meshes/Actors/Character/Animations/DynamicAnimationReplacer/_CustomConditions/940100`) in the target data folder

## MCM Setup

- Dependency: SkyUI (for `SKI_ConfigBase`).
- Script source: `Data/Scripts/Source/DodgeBlink_MCN.psc`
- Compile with SkyUI sources available in your Papyrus import path:

```powershell
.\tools\CompilePapyrus.ps1 -PapyrusCompilerPath "<path-to-PapyrusCompiler.exe>" -SkyUiSourcePath "<path-to-SkyUI-Scripts\\Source>" -IncludeMcm
```

- Bundled SkyUI header sources are included under `third_party/SkyUI/Scripts/Source` and are auto-used when `-IncludeMcm` is set and `-SkyUiSourcePath` is omitted.
- If you provide `-SkyUiSourcePath`, that path is used first for SkyUI imports.

- Attach `DodgeBlink_MCN` to a Start Game Enabled quest in your plugin (`DodgeBlinkShout.esp`) using CK/xEdit workflow.
- MCM exposes activation mode/policy, hotkey controls, distance/cooldown/timing tuning, and mode-specific stamina/magicka/shout cooldown controls.

## License

- See `LICENSE` for redistribution rules.
- Summary: free to use, edit, and include in modpacks; do not reupload this mod as a standalone file without permission.

## OAR Framework

- Dependency: `Open Animation Replacer` (OAR) is optional.
- NADS ships OAR template submods only (config stubs), not dodge HKX clips.
- Bundled templates: `TP_Dodge_Frozen` plus 8 directional `TP_Dodge_*` folders.
- With bundled INI defaults, runtime movement is controller-driven and third-person invisibility FX are enabled.
- Animators can drop custom clips into `Data/Meshes/OpenAnimationReplacer/NADS/` and use the included templates.
- See `Data/Meshes/OpenAnimationReplacer/NADS/NADS_ANIMATOR_GUIDE.txt` for direction codes and slot names.
- Any custom clip pack should avoid `MCO_` / `DMCO_` annotation events and root-motion displacement.
## Runtime Validation

- SKSE log path:
  `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\skse64.log`
- Plugin logs are expected under your SKSE log folder.
- Confirm plugin load in `skse64.log` before gameplay tuning.

## Tuning Focus

Main user-facing tuners are in `Data/SKSE/Plugins/DodgeBlinkShout.ini`:

- Tier distance/cooldown values (`Dist*`, `CD*`)
- Dash gate timing (`DashTime*`)
- Activation mode, visibility policy, and hotkey settings (`ActivationMode`, `AllowMultipleActivationTypes`, `HotkeyTier`, `HotkeyCode`, `HotkeyTapMaxMs`)
- Mode-specific resource/cooldown tuning (`HotkeyStaminaCost`, `SpellMagickaCost1..3`, `ShoutCooldown1..3`)
- Dash speed cap (`DashMaxSpeed`)
- Dodge i-frames (`DodgeIFramesEnabled`)
- Input conversion (`InputThreshold`, `InputYSign`)
- Input latching for shout wind-up (`InputLatchWindowMs`)
- Optional animation hooks (`AnimHooksEnabled`, `AnimStartEvent`, `AnimStopEvent`, `AnimStateVar`, `AnimTierVar`)
- Third-person framework keys (`TPAnimFrameworkEnabled`, `TPAnim*`)
- Third-person glide smoothing (`TPCatchupStrength`, `TPCatchupStepScaleMax`, `TPRecoverySpeedScale`)
- Third-person shout startup suppression (`TPSuppressVanillaShoutAnim`, `TPSuppressVanillaShoutEvent`)
- Third-person immediate input intercept (`TPImmediateShoutTriggerEnabled`)
- Global distance clamp (`MaxDistanceCap`)
