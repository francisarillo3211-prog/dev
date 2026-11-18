# RobRunExt

External ESP + aimbot for ROB RUN (Unity 6 IL2CPP). Read-only, no driver, no injection.

## Build
1. Install Visual Studio 2022 (Desktop C++ workload) and Git.
2. Run `build.bat` — it clones ImGui and produces `RobRunExt.exe`.

## Run
1. Disable VBS / HVCI / Hyper-V (Windows Security > Core isolation > Memory integrity = Off,
   `bcdedit /set hypervisorlaunchtype off`, reboot). Without this EAC blocks OpenProcess.
2. Launch ROB RUN through Steam, get in game.
3. Run `RobRunExt.exe` as administrator.

Keys: F1 menu, Insert hide overlay, End unload, right mouse hold = aim.

## How it works
- `il2cpp.cpp` locates `global-metadata.dat` in memory and resolves every class/field
  **by name**, so offsets self-heal after patches instead of hardcoded numbers.
- `scan_objects()` finds `PlayerEntityModel` instances by validating the klass pointer.
- World positions come from the native `TransformHierarchy` TRS chain (parents composed),
  head transform at `view + 0xE8`.
- ESP uses camera `worldToCameraMatrix * projectionMatrix`; aimbot uses `SendInput`.

## When it breaks after a patch
- Positions wrong → change `off_head_transform` (try 0xE0/0xF0) or re-dump with Il2CppDumper.
- Camera dead → native matrix offsets `0x2A0` / `0x2E0` in `game.cpp` need re-checking.
- Class renamed → update the strings in `game::init_sdk()`.