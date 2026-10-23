#include "sim_internal.h"

#include <stdlib.h>

static const Direction neighbors[] = {
    DIR_WEST, DIR_EAST, DIR_DOWN, DIR_UP, DIR_NORTH, DIR_SOUTH
};
static const Direction shapes[] = {
    DIR_WEST, DIR_EAST, DIR_NORTH, DIR_SOUTH, DIR_DOWN, DIR_UP
};
static const Direction values[] = {
    DIR_DOWN, DIR_UP, DIR_NORTH, DIR_SOUTH, DIR_WEST, DIR_EAST
};

bool block_states_equal(BlockState a, BlockState b)
{
    if (a.id != b.id) return false;
    /* Compare the hot dust path without encoding state IDs. prefer_dot and
     * the piston's input indicator are not vanilla block-state properties. */
    if (a.id == BLOCK_REDSTONE_WIRE)
        return a.data.wire.power == b.data.wire.power &&
               a.data.wire.connections == b.data.wire.connections &&
               a.data.wire.up_connections == b.data.wire.up_connections;
    if (a.id == BLOCK_REDSTONE_COMPARATOR)
        return a.data.comparator.facing == b.data.comparator.facing &&
               a.data.comparator.subtract == b.data.comparator.subtract &&
               a.data.comparator.powered == b.data.comparator.powered;
    if (a.id == BLOCK_NOTE_BLOCK)
        return a.data.note_block.note == b.data.note_block.note &&
               a.data.note_block.instrument == b.data.note_block.instrument &&
               a.data.note_block.powered == b.data.note_block.powered;
    if (block_is_rail((BlockId)a.id))
        return a.data.rail.powered == b.data.rail.powered &&
               a.data.rail.ascending == b.data.rail.ascending &&
               (a.data.rail.ascending ?
                    a.data.rail.facing == b.data.rail.facing :
                    a.data.rail.facing/2 == b.data.rail.facing/2);
    if (block_is_piston_base((BlockId)a.id) || block_is_piston_head((BlockId)a.id))
        return a.data.piston.facing == b.data.piston.facing &&
               a.data.piston.extended == b.data.piston.extended;
    StateId x, y;
    return state_encode(a, &x) && state_encode(b, &y) && x == y;
}

void sim_neighbor_changed(World *w, BlockPos pos)
{
    if (w->sim->error != RS_OK || !world_in_bounds(w, pos)) return;
    const BlockDef *d = block_definition((BlockId)world_get(w, pos).id);
    if (d->neighbor_changed) {
        ++w->sim->stats.neighbor_checks;
        d->neighbor_changed(w, pos);
    }
}

void sim_update_neighbors_at(World *w, BlockPos pos, Direction except)
{
    for (unsigned i = 0; i < DIR_COUNT; ++i)
        if (neighbors[i] != except)
            sim_neighbor_changed(w, block_offset(pos, neighbors[i]));
}

void sim_update_output_neighbors(World *w, BlockPos pos, Direction output)
{
    BlockPos front = block_offset(pos, output);
    sim_neighbor_changed(w, front);
    sim_update_neighbors_at(w, front, direction_opposite(output));
}

static void torch_neighbors(World *w, BlockPos pos)
{
    for (unsigned i = 0; i < DIR_COUNT; ++i)
        sim_update_neighbors_at(w, block_offset(pos, values[i]), DIR_COUNT);
}

static void removed(World *w, StateChange c, bool changed_type)
{
    bool moving = (c.flags & UPDATE_MOVING) != 0;
    switch ((BlockId)c.previous.id) {
    case BLOCK_REDSTONE_WIRE:
        if (changed_type && !moving) wire_on_remove(w, c.pos, c.previous);
        break;
    case BLOCK_REDSTONE_TORCH:
        if (!moving) torch_neighbors(w, c.pos);
        break;
    case BLOCK_REDSTONE_REPEATER:
    case BLOCK_REDSTONE_COMPARATOR:
        if (changed_type && !moving)
            sim_update_output_neighbors(w, c.pos,
                (Direction)(c.previous.id == BLOCK_REDSTONE_REPEATER ?
                    c.previous.data.repeater.facing : c.previous.data.comparator.facing));
        break;
    case BLOCK_OBSERVER:
        if (changed_type && c.previous.data.observer.powered &&
            scheduler_pending(&w->sim->scheduler, c.pos,
                               scheduler_block_type(c.previous)))
            sim_update_output_neighbors(w, c.pos,
                (Direction)c.previous.data.observer.facing);
        break;
    case BLOCK_POWERED_RAIL:
    case BLOCK_ACTIVATOR_RAIL:
        if (!moving) {
            if (c.previous.data.rail.ascending)
                sim_update_neighbors_at(w, block_offset(c.pos, DIR_UP), DIR_COUNT);
            sim_update_neighbors_at(w, c.pos, DIR_COUNT);
            sim_update_neighbors_at(w, block_offset(c.pos, DIR_DOWN), DIR_COUNT);
        }
        break;
    default: break;
    }
    if (changed_type && container_is_block((BlockId)c.previous.id))
        sim_notify_analog(w, c.pos);
}

static void placed(World *w, StateChange c, bool changed_type)
{
    switch ((BlockId)c.current.id) {
    case BLOCK_REDSTONE_WIRE:
        if (changed_type) wire_on_place(w, c.pos, c.current);
        break;
    case BLOCK_REDSTONE_TORCH:
        torch_neighbors(w, c.pos);
        break;
    case BLOCK_REDSTONE_REPEATER:
    case BLOCK_REDSTONE_COMPARATOR:
        sim_update_output_neighbors(w, c.pos,
            (Direction)(c.current.id == BLOCK_REDSTONE_REPEATER ?
                c.current.data.repeater.facing : c.current.data.comparator.facing));
        break;
    case BLOCK_PISTON:
    case BLOCK_STICKY_PISTON:
        if (changed_type || (c.flags & UPDATE_MOVING)) piston_check_input(w, c.pos);
        break;
    case BLOCK_HOPPER:
        if (changed_type) sim_mark_input(w, c.pos);
        break;
    case BLOCK_POWERED_RAIL:
    case BLOCK_ACTIVATOR_RAIL:
        if (changed_type) rail_on_place(w, c.pos, c.current);
        break;
    case BLOCK_OBSERVER:
        if (changed_type && c.current.data.observer.powered &&
            !sim_has_scheduled_tick(w, c.pos)) {
            BlockState state = c.current;
            state.data.observer.powered = false;
            if (world_set_block(w, c.pos, state, 18))
                sim_update_output_neighbors(w, c.pos,
                    (Direction)state.data.observer.facing);
        }
        break;
    case BLOCK_TARGET:
        if (changed_type && c.current.data.target.power &&
            !sim_has_scheduled_tick(w, c.pos)) {
            BlockState state = c.current;
            state.data.target.power = 0;
            (void)world_set_block(w, c.pos, state, 18);
        }
        break;
    default: break;
    }
}

static BlockState update_shape(World *w, BlockPos pos, BlockState state,
                                Direction toward, BlockPos source)
{
    if (state.id == BLOCK_REDSTONE_WIRE)
        state = wire_update_shape(w, pos, state, toward);
    else if (state.id == BLOCK_REDSTONE_REPEATER) {
        if (toward/2 != state.data.repeater.facing/2)
            state.data.repeater.locked = repeater_is_locked(w, pos, state);
    } else if (state.id == BLOCK_NOTE_BLOCK) {
        if (toward == DIR_DOWN)
            state.data.note_block.instrument = (uint8_t)note_instrument(world_get(w, source));
    } else if (state.id == BLOCK_REDSTONE_TORCH) {
        Direction support = (Direction)state.data.torch.support_dir;
        if (toward == support && !torch_supported(w, pos, support))
            state = block_default(BLOCK_AIR);
    } else if (block_is_piston_head((BlockId)state.id)) {
        Direction facing = (Direction)state.data.piston.facing;
        if (toward == direction_opposite(facing)) {
            BlockState base = world_get(w, source);
            if (!block_piston_pair((BlockId)base.id, (BlockId)state.id) ||
                !base.data.piston.extended || base.data.piston.facing != facing)
                state = block_default(BLOCK_AIR);
        }
    }
    const BlockDef *d = block_definition((BlockId)state.id);
    if (d->observed_changed) d->observed_changed(w, pos, state, source);
    return state;
}

BlockState sim_update_from_neighbor_shapes(World *w, BlockPos pos, BlockState state)
{
    /* Block.updateFromNeighbourShapes evaluates a carried state while its
     * world cell still contains MOVING_PISTON, in W/E/N/S/D/U order. */
    for (unsigned i = 0; i < DIR_COUNT && w->sim->error == RS_OK; ++i)
        state = update_shape(w, pos, state, shapes[i], block_offset(pos, shapes[i]));
    return state;
}

void sim_update_shape_at(World *w, BlockPos pos, Direction toward,
                          BlockPos source, unsigned flags, unsigned limit)
{
    if (w->sim->error != RS_OK || !world_in_bounds(w, pos)) return;
    BlockState previous = world_get(w, pos);
    BlockState state = update_shape(w, pos, previous, toward, source);
    if (!block_states_equal(previous, state))
        (void)world_set_block_depth(w, pos, state,
            state.id == BLOCK_AIR ? 3 : flags & ~32u, limit);
}

void sim_state_changed(World *w, StateChange c)
{
    bool changed_type = scheduler_block_type(c.previous) != scheduler_block_type(c.current);
    removed(w, c, changed_type);
    if (world_get(w, c.pos).id != c.current.id || w->sim->error != RS_OK) return;
    placed(w, c, changed_type);
    if (!block_states_equal(world_get(w, c.pos), c.current) ||
        w->sim->error != RS_OK) return;
    if (c.flags & UPDATE_NEIGHBORS) {
        sim_update_neighbors_at(w, c.pos, DIR_COUNT);
        if (block_has_analog_output(w, c.pos)) sim_notify_analog(w, c.pos);
    }
    if (!(c.flags & UPDATE_KNOWN_SHAPE) && c.remaining_shapes) {
        unsigned flags = c.flags & ~33u, limit = c.remaining_shapes - 1;
        if (c.previous.id == BLOCK_REDSTONE_WIRE)
            wire_indirect_shapes(w, c.pos, c.previous, flags, limit);
        for (unsigned i = 0; i < DIR_COUNT; ++i)
            sim_update_shape_at(w, block_offset(c.pos, shapes[i]),
                direction_opposite(shapes[i]), c.pos, flags, limit);
        if (c.current.id == BLOCK_REDSTONE_WIRE)
            wire_indirect_shapes(w, c.pos, c.current, flags, limit);
    }
}

bool sim_hold_change(World *w, StateChange change)
{
    Simulator *s = w->sim;
    if (s->held_count == s->held_capacity) {
        size_t cap = s->held_capacity ? s->held_capacity*2 : 32;
        if (cap < s->held_capacity || cap > SIZE_MAX/sizeof(*s->held_changes))
            goto fail;
        StateChange *next = realloc(s->held_changes, cap*sizeof(*next));
        if (!next) goto fail;
        s->held_changes = next; s->held_capacity = cap;
    }
    s->held_changes[s->held_count++] = change;
    return true;
fail:
    s->error = RS_OUT_OF_MEMORY;
    return false;
}

void sim_begin_updates(World *w) { ++w->sim->hold_updates; }
void sim_end_updates(World *w)
{
    Simulator *s = w->sim;
    if (!s->hold_updates || --s->hold_updates) return;
    for (size_t i = 0; i < s->held_count && s->error == RS_OK; ++i)
        sim_state_changed(w, s->held_changes[i]);
    s->held_count = 0;
}
