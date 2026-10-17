#include "rail_solver.h"
#include "sim_internal.h"

#include <stdlib.h>

enum { RAIL_RANGE = 8, MAX_RAIL_NEIGHBORS = 6 };

typedef struct {
    BlockPos pos;
    uint8_t distance;
} RailNode;

typedef struct {
    RailNode *nodes;
    size_t count, capacity;
    PosMap visited;
} RailWork;

static bool add_node(RailWork *work, BlockPos position)
{
    if (work->count == work->capacity) {
        size_t capacity = work->capacity ? work->capacity*2 : 64;
        if (capacity < work->capacity ||
            capacity > SIZE_MAX/sizeof(*work->nodes)) return false;
        RailNode *next = realloc(work->nodes, capacity*sizeof(*next));
        if (!next) return false;
        work->nodes = next;
        work->capacity = capacity;
    }
    if (!posmap_set(&work->visited, position, work->count)) return false;
    work->nodes[work->count++] = (RailNode){position, UINT8_MAX};
    return true;
}

static bool solve_component(World *world, RailWork *work, BlockPos root)
{
    size_t start = work->count;
    if (!add_node(work, root)) return false;

    for (size_t cursor = start; cursor < work->count; ++cursor) {
        BlockPos neighbors[MAX_RAIL_NEIGHBORS];
        size_t count = rail_neighbors(world, work->nodes[cursor].pos, true,
                                      neighbors);
        for (size_t index = 0; index < count; ++index)
            if (!posmap_get(&work->visited, neighbors[index], NULL) &&
                !add_node(work, neighbors[index])) return false;
    }

    ++world->sim->stats.rail_components_solved;
    world->sim->stats.rails_visited += work->count - start;

    size_t *queue = malloc((work->count - start)*sizeof(*queue));
    if (!queue) return false;
    size_t head = 0, tail = 0;
    for (size_t index = start; index < work->count; ++index) {
        if (power_received(world, work->nodes[index].pos, SIGNAL_ALL) == 0)
            continue;
        work->nodes[index].distance = 0;
        queue[tail++] = index;
    }

    while (head < tail) {
        size_t index = queue[head++];
        uint8_t distance = work->nodes[index].distance;
        if (distance >= RAIL_RANGE) continue;
        BlockPos neighbors[MAX_RAIL_NEIGHBORS];
        size_t count = rail_neighbors(world, work->nodes[index].pos, true,
                                      neighbors);
        for (size_t neighbor = 0; neighbor < count; ++neighbor) {
            size_t next;
            if (!posmap_get(&work->visited, neighbors[neighbor], &next)) {
                free(queue);
                return false;
            }
            if (work->nodes[next].distance <= distance + 1) continue;
            work->nodes[next].distance = (uint8_t)(distance + 1);
            queue[tail++] = next;
        }
    }
    free(queue);

    for (size_t index = start; index < work->count; ++index) {
        BlockState state = world_get(world, work->nodes[index].pos);
        bool powered = work->nodes[index].distance <= RAIL_RANGE;
        if (state.data.rail.powered == powered) continue;
        state.data.rail.powered = powered;
        if (!world_write_derived(world, work->nodes[index].pos, state))
            return false;
        ++world->sim->stats.rail_changes;
    }
    return true;
}

void rail_solver_flush(World *world)
{
    RailWork work = {0};
    BlockPos root;
    while (queue_pop(&world->sim->rail_roots, &root)) {
        if (!block_is_rail((BlockId)world_get(world, root).id) ||
            posmap_get(&work.visited, root, NULL)) continue;
        if (!solve_component(world, &work, root)) {
            if (world->sim->error == RS_OK)
                world->sim->error = RS_OUT_OF_MEMORY;
            break;
        }
    }
    free(work.nodes);
    posmap_destroy(&work.visited);
}
