#include "wire_solver.h"
#include "sim.h"

#include "string.h"

void wire_solver_flush(World *world)
{
    Simulator *sim = world->sim;
    if (!sim->wire_count) {
        return;
    }
    if (++sim->epoch == 0) {
        memset(sim->visited, 0, world->volume *sizeof(*sim->visited));
        sim->epoch = 1;
    }

    for (size_t cursor = 0; cursor < sim->wire_count; ++cursor){
        uint32_t index = sim->wire_roots[cursor];
        sim->root_flags[index] &= (uint8_t)~ROOT_WIRE;
        if (world->cells[index].id == BLOCK_REDSTONE_DUST && sim->visited[index] != sim->epoch){
            solve_component(world, index);
        }
    }
    sim->wire_count = 0;
}
