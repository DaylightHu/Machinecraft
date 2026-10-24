# Machinecraft

A redstone sandbox written in C, based on Minecraft 1.16.2.

Machinecraft is a small, independent environment for building, observing, and testing redstone machines. It deliberately removes survival mechanics,
entities, random terrain, and lighting simulation so the project can focus on
redstone behavior, predictable timing, and performance.


## Build
A C11 compiler is the only required build tool; raylib is included in the repository. Compile the `nob` build runner once:

macOS:

```sh
cc nob.c -o nob
```

Windows PowerShell:

```powershell
clang nob.c -o nob.exe
```

Run `./nob COMMAND` on macOS or `.\nob.exe COMMAND` on Windows.

| Command | Action |
| --- | --- |
| `build` | Build the GUI and CLI. This is the default when no command is given. |
| `gui` | Build only the GUI. |
| `cli` | Build only the CLI. |
| `run` | Build and run the GUI. |
| `run-cli` | Build and run the CLI. |
| `vendor-list` | Print the raylib files required by macOS and Windows. |
| `precommit` | Stage the required raylib files and verify an index-only build. |
| `clean` | Remove the build directory. |
| `help` | Show the command list. |

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
