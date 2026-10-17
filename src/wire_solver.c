#include "wire_solver.h"
#include "sim_internal.h"
#include <stdlib.h>

enum { POWER_LEVELS = 16, MAX_WIRE_NEIGHBORS = 12 };
typedef struct { BlockPos pos; size_t next, previous; uint8_t power; } WireNode;
typedef struct { WireNode *nodes; size_t count, capacity; PosMap visited; } WireWork;
static bool add_node(WireWork *work, BlockPos p)
{
    if (work->count == work->capacity) {
        size_t capacity = work->capacity ? work->capacity * 2 : 64;
        if (capacity < work->capacity || capacity > SIZE_MAX / sizeof(WireNode)) return false;
        WireNode *next = realloc(work->nodes, capacity * sizeof(WireNode));
        if (!next) return false;
        work->nodes = next; work->capacity = capacity;
    }
    if (!posmap_set(&work->visited, p, work->count)) return false;
    work->nodes[work->count++] = (WireNode){p, SIZE_MAX, SIZE_MAX, 0}; return true;
}
static void bucket_remove(WireWork *w, size_t heads[16], size_t i)
{
    WireNode *n = &w->nodes[i];
    if (n->previous == SIZE_MAX) heads[n->power] = n->next;
    else w->nodes[n->previous].next = n->next;
    if (n->next != SIZE_MAX) w->nodes[n->next].previous = n->previous;
    n->next = n->previous = SIZE_MAX;
}
static void bucket_insert(WireWork *w, size_t heads[16], size_t i)
{
    WireNode *n = &w->nodes[i]; n->previous = SIZE_MAX; n->next = heads[n->power];
    if (n->next != SIZE_MAX) w->nodes[n->next].previous = i;
    heads[n->power] = i;
}
static bool solve(World *world, WireWork *w, BlockPos root)
{
    size_t start = w->count;
    if (!add_node(w, root)) return false;
    for (size_t cursor = start; cursor < w->count; ++cursor) {
        BlockPos neighbors[MAX_WIRE_NEIGHBORS];
        size_t count = wire_neighbors(world, w->nodes[cursor].pos, neighbors);
        for (size_t i = 0; i < count; ++i)
            if (!posmap_get(&w->visited, neighbors[i], NULL) && !add_node(w, neighbors[i])) return false;
    }
    ++world->sim->stats.components_solved;
    world->sim->stats.wires_visited += w->count - start;
    size_t heads[POWER_LEVELS];
    for (unsigned i = 0; i < POWER_LEVELS; ++i) heads[i] = SIZE_MAX;
    for (size_t i = start; i < w->count; ++i) {
        w->nodes[i].power = wire_external_power(world, w->nodes[i].pos);
        if (w->nodes[i].power) bucket_insert(w, heads, i);
    }
    for (int level = 15; level > 0; --level) while (heads[level] != SIZE_MAX) {
        size_t index = heads[level]; bucket_remove(w, heads, index);
        if (level == 1) continue;
        BlockPos neighbors[MAX_WIRE_NEIGHBORS];
        size_t count = wire_neighbors(world, w->nodes[index].pos, neighbors);
        for (size_t i = 0; i < count; ++i) {
            /* Keep the component graph undirected, but make power flow match
             * Java Edition: dust supported by glass or another non-conductor
             * cannot send power diagonally downward. It can still receive
             * power from the lower dust, so the same glass step works upward. */
            if (neighbors[i].y < w->nodes[index].pos.y &&
                !block_is_conductor(world,
                    block_offset(w->nodes[index].pos, DIR_DOWN))) continue;
            size_t next;
            if (!posmap_get(&w->visited, neighbors[i], &next)) return false;
            if (w->nodes[next].power >= level - 1) continue;
            if (w->nodes[next].power) bucket_remove(w, heads, next);
            w->nodes[next].power = (uint8_t)(level - 1); bucket_insert(w, heads, next);
        }
    }
    for (size_t i = start; i < w->count; ++i) {
        WireNode *n = &w->nodes[i]; BlockState state = world_get(world, n->pos);
        if (state.data.wire.power == n->power) continue;
        state.data.wire.power = n->power;
        if (!world_write_derived(world, n->pos, state)) return false;
        ++world->sim->stats.wire_changes; sim_notify_power(world, n->pos);
    }
    return true;
}
void wire_solver_flush(World *world)
{
    WireWork work = {0}; BlockPos root;
    while (queue_pop(&world->sim->wire_roots, &root)) {
        if (world_get(world, root).id != BLOCK_REDSTONE_WIRE || posmap_get(&work.visited, root, NULL)) continue;
        if (!solve(world, &work, root)) { if (world->sim->error == RS_OK) world->sim->error = RS_OUT_OF_MEMORY; break; }
    }
    free(work.nodes); posmap_destroy(&work.visited);
}
