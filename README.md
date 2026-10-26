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

## Workspace

Opening a world in `machinecraft-gui` shows a fixed `mc-gui` pane on the left
(70% of the window) and a built-in `mc-cli` pane on the right. Both operate on
the same world, selection, clipboard and undo history. The built-in CLI runs
the existing commands directly, without a shell or a separate terminal app.
The standalone `machinecraft-cli` remains available.

Press **Ctrl+b**, release it, then press one of these keys:

| Key | Action |
| --- | --- |
| `%` | Split the current CLI pane left/right. From GUI focus, split the first CLI or create one. |
| `"` | Split the current CLI pane top/bottom. From GUI focus, split the first CLI or create one. |
| Arrow | Focus a pane in that direction. |
| `o` | Cycle focus through panes. |
| Ctrl+Arrow or `H/J/K/L` | Move the nearest CLI divider left/down/up/right. The GUI boundary stays fixed. |
| `z` | Zoom/restore the focused CLI within the CLI area. |
| `x` | Close the focused pane. The last remaining pane stays open. |
| `g` | Restore and focus the GUI. |
| `?` | Show keyboard help. Escape closes help or cancels an unfinished prefix. |

Only the focused pane receives input. Switching to CLI releases the mouse;
switching back to GUI resumes camera control. GUI cannot be split or resized.
Closing GUI gives its space to CLI; `Ctrl+b`, then `g` restores the 70/30 layout.
Closing the last CLI leaves its area empty while GUI remains open. Up to eight
CLI panes are supported. Splits that do not fit are refused; shrinking the
window too far temporarily shows one CLI and restores the layout when enlarged.

In CLI, Enter executes; Tab completes; Up/Down recall the last 32 commands;
Left/Right, Home/End and Backspace/Delete edit input; Page Up/Down scroll the last
192 output lines. Ctrl+U clears input, Ctrl+C cancels input or a pending plot
confirmation, and Ctrl+V (Cmd+V on macOS) pastes text without executing it.
`quit` saves and closes that CLI pane (the last pane returns to world selection);
`title` saves and returns the whole workspace to world selection. Escape opens
the existing pause menu; Escape again resumes. Frozen `tick step` commands are
queued and advanced by the shared host in bounded batches so input stays usable.
Pane layouts and command history are cleared when leaving the world.

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
```
