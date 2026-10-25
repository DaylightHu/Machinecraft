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
| `/plot expand DIRECTION [COUNT]` | Expand toward `east`, `west`, `south` or `north` in 512-block units. Optional `COUNT` expands multiple units at once; for example, `/plot expand east 2`. |
| `/tick freeze` | Toggle automatic game ticks. Toggling also cancels any remaining manual steps. |
| `/tick step NUMBER` | While frozen, queue this many game ticks, then remain frozen. `NUMBER` is required and accepts positive decimal integers through 4294967295. |
| `/tick noupdate` | Toggle block callbacks, neighbor notifications and shape updates. Enabling does not replay skipped updates. |
