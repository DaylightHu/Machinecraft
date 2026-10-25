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

## Tick commands

The GUI chat input and CLI share these commands (the leading `/` is optional):

| Command | Action |
| --- | --- |
| `/tick freeze` | Toggle automatic game ticks. Toggling also cancels any remaining manual steps. |
| `/tick NUMBER` | While frozen, queue this many game ticks, then remain frozen. Accepts positive decimal integers through 4294967295. `/tick` alone requests one tick. |
| `/tick noupdate` | Toggle block callbacks, neighbor notifications and shape updates. Enabling does not replay skipped updates. |

Manual steps run without waiting for the usual real-time tick interval. The GUI
processes them in bounded batches so input and rendering remain responsive;
another step command adds to the remaining batch. The HUD shows the game tick,
freeze state, pending manual steps and update state. The CLI finishes the batch
before reading its next command. The CLI has no automatic clock; use
`tick freeze` before `tick NUMBER`.

Freezing still allows block edits and their synchronous redstone updates.
`noupdate` instead suppresses those updates, including support checks caused by
removing a block. Storage, rendering, inventories and runtime cleanup remain
active. Existing scheduled ticks, piston events/motions and hoppers continue
when time advances, but their neighbor/shape notifications are suppressed too.
Combine `freeze` and `noupdate` when both time and update propagation must stop.

Both controls reset when a world is reopened and are not saved in `.mcw` files.
