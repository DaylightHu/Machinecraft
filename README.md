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
a read-only view. Use `:help` to print the CLI command list. The `world`
commands below manage which saved world this mc-nvim pane is attached to.

| `:COMMAND` | Purpose |
| --- | --- |
| `:world list` | List saved and loaded worlds. |
| `:world create NAME` | Create and attach a new world. |
| `:world attach NAME` | Attach the pane to an existing world. |
| `:world detach` | Detach the pane from its current world. |
| `:tick freeze` | Toggle automatic ticking. |
| `:tick step NUMBER` | Run a fixed number of ticks while frozen. |
| `:tick noupdate` | Toggle block callbacks and neighbor updates. |
| `:schem save NAME` | Save the current selection to `structures/NAME.mcs`. |
| `:undo` / `:redo` | Undo or redo the last world edit. |

The same world-command language is available in the GUI command input opened
with `/`.

## References

- https://github.com/raysan5/raylib
- https://github.com/tsoding/nob.h
- https://github.com/jdah/minecraft-weekend
- https://github.com/MCHPR/MCHPRS
- https://github.com/gnembon/fabric-carpet
- https://github.com/maruohon/tweakeroo
- https://github.com/EngineHub/WorldEdit
- https://github.com/tmux/tmux
