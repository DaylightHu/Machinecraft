#include "sim_internal.h"

#include <stdlib.h>

static bool compact_motions(World *world)
{
    PistonMotions *table = &world->sim->piston_motions;
    if (table->active == table->count) return true;

    posmap_clear(&table->slots);
    size_t output = 0;
    for (size_t index = 0; index < table->count; ++index) {
        if (!table->items[index].used) continue;
        if (output != index) table->items[output] = table->items[index];
        if (!posmap_set(&table->slots, table->items[output].pos, output)) {
            world->sim->error = RS_OUT_OF_MEMORY;
            return false;
        }
        ++output;
    }
    table->count = output;
    return true;
}

bool sim_prepare_piston_motions(World *world, size_t additional)
{
    if (!world || world->sim->error != RS_OK) return false;
    if (!sim_prepare_block_entities(world, additional)) return false;
    PistonMotions *table = &world->sim->piston_motions;
    if (additional > SIZE_MAX - table->active) goto fail;

    size_t needed = table->active + additional;
    if (!posmap_reserve(&table->slots, needed)) goto fail;
    if (needed <= table->capacity &&
        additional <= table->capacity - table->count)
        return true;
    if (!compact_motions(world)) return false;
    if (needed <= table->capacity) return true;

    size_t capacity = table->capacity ? table->capacity : 16;
    while (capacity < needed) {
        if (capacity > SIZE_MAX/2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX/sizeof(*table->items)) goto fail;
    PistonMotion *next = realloc(table->items,
                                 capacity*sizeof(*table->items));
    if (!next) goto fail;
    table->items = next;
    table->capacity = capacity;
    return true;

fail:
    world->sim->error = RS_OUT_OF_MEMORY;
    return false;
}

const PistonMotion *sim_piston_motion(const World *world, BlockPos position)
{
    size_t slot;
    if (!world || !posmap_get(&world->sim->piston_motions.slots,
                              position, &slot))
        return NULL;
    return &world->sim->piston_motions.items[slot];
}

bool sim_add_piston_motion(World *world, PistonMotion motion)
{
    BlockState placeholder;
    StateId moved_id;
    bool source_payload = motion.extending ?
        block_is_piston_head((BlockId)motion.moved_state.id) :
        block_is_piston_base((BlockId)motion.moved_state.id);
    if (!world || world->sim->error != RS_OK ||
        sim_piston_motion(world, motion.pos) ||
        (unsigned)motion.direction >= DIR_COUNT ||
        motion.moved_state.id == BLOCK_AIR ||
        motion.moved_state.id == BLOCK_MOVING_PISTON ||
        !state_encode(motion.moved_state, &moved_id) ||
        (!motion.source && motion.moved_state.id >= BLOCK_INTERNAL_FIRST) ||
        (motion.source &&
         (!source_payload ||
          (motion.extending &&
           motion.moved_state.data.piston.facing != motion.direction) ||
          (!motion.extending && motion.moved_state.data.piston.extended))) ||
        motion.progress > 2 || motion.previous_progress > motion.progress ||
        motion.progress - motion.previous_progress > 1) {
        if (world) world->sim->error = RS_INTERNAL_ERROR;
        return false;
    }
    placeholder = world_get(world, motion.pos);
    bool sticky_payload = motion.moved_state.id == BLOCK_STICKY_PISTON ||
                          motion.moved_state.id == BLOCK_STICKY_PISTON_HEAD;
    if (placeholder.id != BLOCK_MOVING_PISTON ||
        placeholder.data.moving_piston.facing != motion.direction ||
        placeholder.data.moving_piston.sticky !=
            (motion.source && sticky_payload)) {
        world->sim->error = RS_INTERNAL_ERROR;
        return false;
    }
    if (!sim_prepare_piston_motions(world, 1)) return false;

    PistonMotions *table = &world->sim->piston_motions;
    size_t slot = table->count;
    motion.used = true;
    if (!posmap_set(&table->slots, motion.pos, slot)) {
        world->sim->error = RS_OUT_OF_MEMORY;
        return false;
    }
    if (!sim_add_block_entity(world, motion.pos, BLOCK_MOVING_PISTON,
                               &motion.tick_slot)) {
        posmap_remove(&table->slots, motion.pos);
        return false;
    }
    table->items[table->count++] = motion;
    ++table->active;
    return true;
}

void sim_discard_piston_motion(World *world, BlockPos position)
{
    if (!world) return;
    PistonMotions *table = &world->sim->piston_motions;
    size_t slot;
    if (!posmap_get(&table->slots, position, &slot)) return;
    posmap_remove(&table->slots, position);
    if (table->items[slot].used) {
        sim_discard_block_entity(world, table->items[slot].tick_slot);
        table->items[slot].used = false;
        --table->active;
    }
}

bool sim_finish_piston_motion(World *world, BlockPos position, bool forced)
{
    const PistonMotion *found = sim_piston_motion(world, position);
    if (!found) return false;
    /* finalTick does nothing once progressO has already reached 1. */
    if (forced && found->previous_progress >= 2) return true;
    PistonMotion motion = *found;
    sim_discard_piston_motion(world, position);

    if (world_get(world, position).id != BLOCK_MOVING_PISTON) return true;
    BlockState settled = forced && motion.source ? block_default(BLOCK_AIR) :
        sim_update_from_neighbor_shapes(world, position, motion.moved_state);
    if (world->sim->error != RS_OK) return true;
    if (!forced && settled.id == BLOCK_AIR) {
        /* Normal completion installs the carried state with vanilla flags 84
         * (including MOVING and KNOWN_SHAPE), then destroys it with flags 3. */
        if (world_set_block(world, position, motion.moved_state, 84))
            world_remove_internal(world, position);
        return true;
    }
    unsigned flags = forced ? UPDATE_NEIGHBORS | UPDATE_CLIENTS :
                              UPDATE_NEIGHBORS | UPDATE_CLIENTS | UPDATE_MOVING;
    if (!world_set_block(world, position, settled, flags)) return true;
    /* PistonMovingBlockEntity explicitly invokes neighborChanged on the state
     * which replaces MOVING_PISTON, in addition to ordinary neighbor updates. */
    sim_neighbor_changed(world, position);
    return true;
}

void sim_tick_piston_motion(World *world, BlockPos position)
{
    PistonMotions *table = &world->sim->piston_motions;
    size_t index;
    if (!posmap_get(&table->slots, position, &index)) return;
    PistonMotion *motion = &table->items[index];
    if (world_get(world, position).id != BLOCK_MOVING_PISTON) {
        sim_discard_piston_motion(world, position);
        return;
    }
    motion->last_ticked = world->tick;
    motion->previous_progress = motion->progress;
    if (motion->previous_progress >= 2)
        (void)sim_finish_piston_motion(world, position, false);
    else
        ++motion->progress;
}
