# Machinecraft

A redstone sandbox written in C, based on Minecraft 1.16.2.

Machinecraft is a small, independent environment for building, observing, and testing redstone machines. It deliberately removes survival mechanics,
entities, random terrain, and lighting simulation so the project can focus on
redstone behavior, predictable timing, and performance.


## Build
This repository includes the raylib 6.0 source files required by Machinecraft. You do not need to install raylib, CMake, Make, Ninja, or a package manager.
The only prerequisite is a C11-capable toolchain for your platform.
Machinecraft uses [nob.h](https://github.com/tsoding/nob.h) (by Tsoding) as its build system. 

MacOS:

```sh
cc nob.c -o nob
./nob run
```

Windows:

```powershell
clang nob.c -o nob.exe
.\nob.exe run
```

Linux：
```
Just find ways by yourself.
```

After the first bootstrap, use `./nob` or `.\nob.exe` to build both the GUI and CLI. Run `help` to see the other commands. Outputs are written to
`build/macos/` or `build/windows/`.

## Comparator regression checks

Run `python3 tests/run_comparator.py` with a C11 compiler named `cc` (or select
one with `--cc`). `--sanitize` enables UndefinedBehaviorSanitizer. These headless
checks cover signal/state timing, observer and BUD notifications, downstream
dust, and save compatibility against Minecraft 1.16.2 source-derived expectations.

Save version 8 preserves comparator `POWERED` independently of its output
cache. Versions 2 through 7 remain readable; their effective output infers
`POWERED`, without resampling inputs or advancing pending ticks. Old saves did
not retain a suppressed comparator's rear-input cache.
