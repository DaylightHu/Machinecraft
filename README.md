# mcmux

A redstone sandbox written in C, based on Minecraft 1.16.2.

MCMUX(Minecraft Multiplexer) is a small, independent environment for building, observing, and testing redstone machines. It deliberately removes survival mechanics,
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

## Commands

In the GUI, press `/` to open command input, type a command, and press Enter.
Command replies appear above the hotbar for ten seconds. Press `/` to view
the reply history; use the mouse wheel or Page Up/Down to scroll. The latest
100 replies are kept in memory and cleared when leaving the world or exiting
the GUI. Empty input and Escape close the input without running a command.
The CLI accepts the same commands, with or without the leading `/`.
Press Tab in either interface to complete the current word (for example,
`tick f` becomes `tick freeze`); repeated Tab presses cycle through matches.

| Command | Action |
| --- | --- |
| `//pos1 [X Y Z]`, `//pos2 [X Y Z]` | Set the two inclusive selection corners. Coordinates also accept `X,Y,Z`. |
| `//copy [X Y Z]` | Copy the selection, using the given position as the clipboard anchor. |
| `//paste [-a] [X Y Z]` | Paste relative to the clipboard anchor. `-a` skips air. |
| `//stack [COUNT] [DIRECTION]` | Add `COUNT` copies along a direction, spaced by the selection size. Default count: 1. |
| `//move [DISTANCE] [DIRECTION]` | Move the selection's blocks, leaving air in uncovered source cells. Default distance: 1. |
| `//undo` | Undo the last paste, stack or move, including block settings and inventories. |
| `//redo` | Redo the last undone edit. A new nonempty edit clears redo. |
| `/plot expand DIRECTION [COUNT]` | Expand toward `east`, `west`, `south` or `north` in 512-block units. Optional `COUNT` expands multiple units at once; for example, `/plot expand east 2`. |
| `/plot shrink DIRECTION [COUNT]` | Remove outer 512-block strips from the specified side, keeping at least 512 blocks on both axes. If edits may be deleted, enter `y` to confirm or `n` to cancel. |
| `/tick freeze` | Toggle automatic game ticks. Toggling also cancels any remaining manual steps. |
| `/tick step NUMBER` | While frozen, queue this many game ticks, then remain frozen. `NUMBER` is required and accepts positive decimal integers through 4294967295. |
| `/tick noupdate` | Toggle block callbacks, neighbor notifications and shape updates. Enabling does not replay skipped updates. |

## Reference
```
https://github.com/raysan5/raylib
https://github.com/tsoding/nob.h
https://github.com/jdah/minecraft-weekend
https://github.com/MCHPR/MCHPRS
https://github.com/gnembon/fabric-carpet
https://github.com/maruohon/tweakeroo
https://github.com/EngineHub/WorldEdit
https://github.com/tmux/tmux
```
