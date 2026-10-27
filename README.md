# mcmux

mcmux is a C redstone sandbox based on Minecraft 1.16.2. The GUI combines a
world editor, redstone simulation, saved worlds, structure export, and an
embedded mc-nvim workspace.

## Build

Use a C compiler to compile `nob.c` into the `nob` build runner. Then run the
`./nob` commands from the project root. On Windows, use the equivalent
`nob.exe` form.

| Command | Purpose |
| --- | --- |
| `./nob` or `./nob build` | Build the project. |
| `./nob gui` | Build the GUI. |
| `./nob run` | Build and launch the GUI. |
| `./nob reader` | Build the standalone `.mcs` structure reader. |
| `./nob vendor-list` | List the vendored raylib files required by the build. |
| `./nob precommit` | Stage the required raylib files and verify an index-only build. |
| `./nob clean` | Remove build outputs. |
| `./nob help` | Show the available build commands. |

## Use mcmux
| Command | Purpose |
| --- | --- |
| `Ctrl + b` & `%` | Open mc-nvim |
|`Ctrl + b` & `?` | Help |


## mc-nvim

mc-nvim is the text workspace inside mcmux. The available editor actions are:

| Mode or command | Action |
| --- | --- |
| Normal mode: `i` | Insert before the cursor. |
| Normal mode: `a` | Insert after the cursor. |
| Normal mode: `x` | Delete the character under the cursor. |
| Normal mode: `h` `j` `k` `l` or arrow keys | Move the cursor. |
| `Esc` | Return to Normal mode. |
| `:Ex` | Browse the current directory. |
| `:e PATH` | Open a file, or prepare a new file when it does not exist. |
| `:w` | Save the current file. |
| `:q` | Close the current pane when its buffers are saved. |
| `:q!` | Close the current pane and discard its unsaved buffers. |
| `:wq` | Save and close the current pane. |
| `:COMMAND` | Run a world or simulation command using the same syntax as the CLI. |
| `:! COMMAND` | Run an external build or program command from the pane directory. |

`:e` and `:Ex` keep the directory context in the pane. An `.mcs` file opens as
a read-only view. Use the command tables below instead of an in-application
help screen.

| `:COMMAND` | Purpose |
| --- | --- |
| `:world list` | List saved and loaded worlds. |
| `:world create NAME` | Create and attach a new world. |
| `:world attach NAME` | Attach the pane to an existing world. |
| `:world detach` | Detach the pane from its current world. |
| `:pos1 X Y Z` / `:pos2 X Y Z` | Set the inclusive selection corners. |
| `:copy [X Y Z]` | Copy the selection with an optional anchor. |
| `:paste [-a] X Y Z` | Paste at an anchor; `-a` skips air. |
| `:stack [COUNT] DIRECTION` | Add repeated copies in a direction. |
| `:move [DISTANCE] DIRECTION` | Move the selected blocks. |
| `:undo` / `:redo` | Undo or redo the last selection edit. |
| `:schem save [-f] NAME [X Y Z]` | Save the selection to `structures/NAME.mcs`; `-f` replaces an existing file. |
| `:tick freeze` | Toggle automatic ticking. |
| `:tick step NUMBER` | Run a fixed number of ticks while frozen. |
| `:tick noupdate` | Toggle block callbacks and neighbor updates. |
| `:plot info` | Show the current plot bounds. |
| `:plot expand DIRECTION [COUNT]` | Expand the plot. |
| `:plot shrink DIRECTION [COUNT]` | Shrink the plot; confirm if edits would be deleted. |
| `:place X Y Z BLOCK [DIRECTION]` | Place a block. |
| `:remove X Y Z` | Remove a block. |
| `:get X Y Z` | Inspect a block and its state. |
| `:dot X Y Z` | Toggle a redstone-wire dot. |
| `:delay X Y Z` | Cycle a repeater delay. |
| `:mode X Y Z` | Toggle comparator mode. |
| `:tune X Y Z` | Cycle a note block note. |
| `:capacity X Y Z LEVEL` | Set a barrel fullness level. |
| `:slots X Y Z` | List a container's slots. |
| `:slot X Y Z INDEX ITEM COUNT LIMIT` | Set one container slot. |
| `:hit X Y Z U V arrow\|trident\|other` | Hit a target at face coordinates `U V` from `0` to `1`. |
| `:dropper-seed SEED` | Set the dropper seed. |
| `:stats` | Show simulation statistics. |
| `:save` | Save the current world. |
| `:title` | Save and detach this pane from its world. |
| `:quit` | Save and close the current pane. |

## References

- https://github.com/raysan5/raylib
- https://github.com/tsoding/nob.h
- https://github.com/jdah/minecraft-weekend
- https://github.com/MCHPR/MCHPRS
- https://github.com/gnembon/fabric-carpet
- https://github.com/maruohon/tweakeroo
- https://github.com/EngineHub/WorldEdit
- https://github.com/tmux/tmux
