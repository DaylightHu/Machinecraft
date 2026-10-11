#include "wire_solver.h"
#include "sim_internal.h"

#include <string.h>

enum { POWER_LEVELS = 16, MAX_WIRE_NEIGHBORS = 12 };

/* Intrusive buckets: raising a queued wire removes its previous entry first. */
static void bucket_remove(Simulator *sim, uint32_t heads[POWER_LEVELS],
                          uint32_t index, uint8_t power)
{
    uint32_t previous = sim->bucket_prev[index];
    uint32_t next = sim->bucket_next[index];
    if (previous == RS_NONE) {
        heads[power] = next;
    } else {
        sim->bucket_next[previous] = next;
    }
    if (next != RS_NONE) {
        sim->bucket_prev[next] = previous;
    }
    sim->bucket_prev[index] = RS_NONE;
    sim->bucket_next[index] = RS_NONE;
}

static void bucket_insert(Simulator *sim, uint32_t heads[POWER_LEVELS],
                          uint32_t index, uint8_t power)
{
    sim->bucket_prev[index] = RS_NONE;
    sim->bucket_next[index] = heads[power];
    if (heads[power] != RS_NONE) {
        sim->bucket_prev[heads[power]] = index;
    }
    heads[power] = index;
}

static size_t collect_component(World *world, uint32_t root)
{
    Simulator *sim = world->sim;
    size_t count = 1;
    sim->nodes[0] = root;
    sim->visited[root] = sim->epoch;

    for (size_t cursor = 0; cursor < count; ++cursor) {
        uint32_t index = sim->nodes[cursor];
        sim->staged_power[index] = 0;
        sim->bucket_next[index] = RS_NONE;
        sim->bucket_prev[index] = RS_NONE;

        uint32_t neighbors[MAX_WIRE_NEIGHBORS];
        size_t neighbor_count = wire_neighbors(world, world_position(world, index), neighbors);
        for (size_t neighbor = 0; neighbor < neighbor_count; ++neighbor) {
            uint32_t next = neighbors[neighbor];
            if (sim->visited[next] == sim->epoch) {
                continue;
            }
            sim->visited[next] = sim->epoch;
            sim->nodes[count++] = next;
        }
    }
    ++sim->stats.components_solved;
    sim->stats.wires_visited += count;
    return count;
}

static void seed_sources(World *world, size_t count, uint32_t heads[POWER_LEVELS])
{
    Simulator *sim = world->sim;
    for (size_t cursor = 0; cursor < count; ++cursor) {
        uint32_t index = sim->nodes[cursor];
        uint8_t power = wire_external_power(world, world_position(world, index));
        sim->staged_power[index] = power;
        if (power) {
            bucket_insert(sim, heads, index, power);
        }
    }
}

static void propagate_power(World *world, uint32_t heads[POWER_LEVELS])
{
    Simulator *sim = world->sim;
    for (int level = POWER_LEVELS - 1; level > 0; --level) {
        while (heads[level] != RS_NONE) {
            uint32_t index = heads[level];
            bucket_remove(sim, heads, index, (uint8_t)level);
            if (level == 1) {
                continue;
            }

            uint32_t neighbors[MAX_WIRE_NEIGHBORS];
            size_t count = wire_neighbors(world, world_position(world, index), neighbors);
            for (size_t cursor = 0; cursor < count; ++cursor) {
                uint32_t next = neighbors[cursor];
                uint8_t previous = sim->staged_power[next];
                uint8_t candidate = (uint8_t)(level - 1);
                if (candidate <= previous) {
                    continue;
                }
                if (previous) {
                    bucket_remove(sim, heads, next, previous);
                }
                sim->staged_power[next] = candidate;
                bucket_insert(sim, heads, next, candidate);
            }
        }
    }
}

static void commit_power(World *world, size_t count)
{
    Simulator *sim = world->sim;
    for (size_t cursor = 0; cursor < count; ++cursor) {
        uint32_t index = sim->nodes[cursor];
        BlockState state = world->cells[index];
        if (state.data.wire.power == sim->staged_power[index]) {
            continue;
        }
        state.data.wire.power = sim->staged_power[index];
        world_write_derived(world, index, state);
        ++sim->stats.wire_changes;
        sim_notify_power(world, world_position(world, index));
    }
}

static void solve_component(World *world, uint32_t root)
{
    uint32_t heads[POWER_LEVELS];
    for (size_t level = 0; level < POWER_LEVELS; ++level) {
        heads[level] = RS_NONE;
    }

    size_t count = collect_component(world, root);
    seed_sources(world, count, heads);
    propagate_power(world, heads);
    commit_power(world, count);
}

void wire_solver_flush(World *world)
{
    Simulator *sim = world->sim;
    if (!sim->wire_count) {
        return;
    }
    if (++sim->epoch == 0) {
        memset(sim->visited, 0, world->volume * sizeof(*sim->visited));
        sim->epoch = 1;
    }

    for (size_t cursor = 0; cursor < sim->wire_count; ++cursor) {
        uint32_t index = sim->wire_roots[cursor];
        sim->root_flags[index] &= (uint8_t)~ROOT_WIRE;
        if (world->cells[index].id == BLOCK_REDSTONE_WIRE && sim->visited[index] != sim->epoch) {
            solve_component(world, index);
        }
    }
    sim->wire_count = 0;
}
