Compile: 
```cmd
clang -std=c11 -Wall -Wextra -Wpedantic -Iinclude -Isrc src/main.c src/app.c src/frontend/cli.c src/world.c src/worldgen.c src/queue.c src/scheduler.c src/sim.c src/wire_solver.c src/power.c src/blocks/registry.c src/blocks/iron.c src/blocks/redstone_block.c src/blocks/wire.c src/blocks/torch.c -o build/machinecraft.exe
```
