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

After the first bootstrap, use `./nob` or `.\nob.exe` to build both the GUI and CLI. Run `help` to see the other commands. Outputs are written to
`build/macos/` or `build/windows/`.

## Plot commands

A new world starts with a 512 × 512 plot. The plot can be expanded in 512-block units without creating boundaries inside the expanded world.

In the GUI, press `/` to open command input, type a command, and press Enter.
```
/plot expand east|west|south|north [COUNT]
```

An optional count expands multiple 512-block units at once:
```
/plot expand east 2
```

The CLI accepts the same commands, with or without the leading `/`.
