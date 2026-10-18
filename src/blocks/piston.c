#include "block_internal.h"
#include "sim_internal.h"

enum { PISTON_DELAY = 2, PISTON_PUSH_LIMIT = 12 };

typedef enum {
    PISTON_AIR,
    PISTON_MOVE,
    PISTON_DESTROY,
    PISTON_BLOCK
} PistonReaction;

static bool piston_input_powered(const World *world, BlockPos position,
                                 Direction facing)
{
    if ((unsigned)facing >= DIR_COUNT) return false;

    /* Vanilla pistons ignore power entering through their moving face. */
    for (Direction direction = 0; direction < DIR_COUNT; ++direction) {
        if (direction == facing) continue;
        BlockPos source = block_offset(position, direction);
        if (power_signal(world, source, direction_opposite(direction),
                         SIGNAL_ALL) > 0)
            return true;
    }

    /* Java Edition quasi-connectivity: also sample the neighbors of the cell
     * above the piston, except the piston cell itself. */
    BlockPos above = block_offset(position, DIR_UP);
    for (Direction direction = 0; direction < DIR_COUNT; ++direction) {
        if (direction == DIR_DOWN) continue;
        BlockPos source = block_offset(above, direction);
        if (power_signal(world, source, direction_opposite(direction),
                         SIGNAL_ALL) > 0)
            return true;
    }
    return false;
}

static PistonReaction piston_reaction(BlockState state)
{
    switch ((BlockId)state.id) {
    case BLOCK_AIR:
        return PISTON_AIR;
    case BLOCK_IRON:
    case BLOCK_REDSTONE_BLOCK:
    case BLOCK_GLASS:
    case BLOCK_OBSERVER:
        return PISTON_MOVE;
    case BLOCK_PISTON:
    case BLOCK_STICKY_PISTON:
        return state.data.piston.extended ? PISTON_BLOCK : PISTON_MOVE;
    case BLOCK_REDSTONE_WIRE:
    case BLOCK_REDSTONE_TORCH:
    case BLOCK_REDSTONE_REPEATER:
    case BLOCK_REDSTONE_COMPARATOR:
    case BLOCK_POWERED_RAIL:
    case BLOCK_ACTIVATOR_RAIL:
        return PISTON_DESTROY;
    case BLOCK_PISTON_HEAD:
    case BLOCK_STICKY_PISTON_HEAD:
    default:
        return PISTON_BLOCK;
    }
}

static void piston_reinitialize_moved_block(World *world, BlockPos position,
                                             BlockState state)
{
    if (state.id == BLOCK_OBSERVER && state.data.observer.powered)
        sim_schedule(world, position, 2);
    if (block_is_piston_base((BlockId)state.id))
        sim_mark_input(world, position);
}

static bool piston_write_cell(World *world, BlockPos position,
                              BlockState state)
{
    sim_forget_cell(world, position);
    if (!world_write_derived(world, position, state)) return false;
    sim_notify_change(world, position);
    return true;
}

static bool piston_push(World *world, BlockPos position, Direction facing)
{
    BlockPos moving[PISTON_PUSH_LIMIT];
    size_t moving_count = 0;
    BlockPos destroyed = {0};
    bool destroys = false;
    BlockPos cursor = position;

    for (;;) {
        cursor = block_offset(cursor, facing);
        if (!world_in_bounds(world, cursor)) return false;
        BlockState state = world_get(world, cursor);
        PistonReaction reaction = piston_reaction(state);
        if (reaction == PISTON_AIR) break;
        if (reaction == PISTON_BLOCK) return false;
        if (reaction == PISTON_DESTROY) {
            destroyed = cursor;
            destroys = true;
            break;
        }
        if (moving_count == PISTON_PUSH_LIMIT) return false;
        moving[moving_count++] = cursor;
    }

    if (destroys) world_remove_internal(world, destroyed);
    if (world->sim->error != RS_OK) return false;

    for (size_t index = moving_count; index > 0; --index) {
        BlockPos source = moving[index - 1];
        BlockPos destination = block_offset(source, facing);
        BlockState state = world_get(world, source);
        if (!piston_write_cell(world, destination, state)) return false;
        if (!piston_write_cell(world, source, block_default(BLOCK_AIR)))
            return false;
        piston_reinitialize_moved_block(world, destination, state);
    }
    return true;
}

static void piston_set_extended(World *world, BlockPos position,
                                BlockState state, bool extended)
{
    Direction facing = (Direction)state.data.piston.facing;
    BlockPos head_position = block_offset(position, facing);
    BlockId base_id = (BlockId)state.id;
    BlockId head_id = base_id == BLOCK_STICKY_PISTON ?
                      BLOCK_STICKY_PISTON_HEAD : BLOCK_PISTON_HEAD;

    if (extended) {
        if (!piston_push(world, position, facing)) return;
        BlockState head = block_default(head_id);
        head.data.piston.facing = state.data.piston.facing;
        head.data.piston.powered = state.data.piston.powered;
        if (!piston_write_cell(world, head_position, head)) return;
        state.data.piston.extended = true;
        if (!world_write_derived(world, position, state)) return;
        sim_notify_change(world, position);
        return;
    }

    BlockState head = world_get(world, head_position);
    bool had_head = block_piston_pair(base_id, (BlockId)head.id) &&
                    head.data.piston.facing == state.data.piston.facing;
    if (had_head) {
        if (!piston_write_cell(world, head_position,
                               block_default(BLOCK_AIR))) return;
    }

    if (base_id == BLOCK_STICKY_PISTON && had_head) {
        BlockPos source = block_offset(head_position, facing);
        if (world_in_bounds(world, source)) {
            BlockState pulled = world_get(world, source);
            if (piston_reaction(pulled) == PISTON_MOVE) {
                if (!piston_write_cell(world, head_position, pulled)) return;
                if (!piston_write_cell(world, source,
                                       block_default(BLOCK_AIR))) return;
                piston_reinitialize_moved_block(world, head_position, pulled);
            }
        }
    }
    state.data.piston.extended = false;
    if (!world_write_derived(world, position, state)) return;
    sim_notify_change(world, position);
}

void piston_check_input(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    if (!block_is_piston_base((BlockId)state.id)) return;
    Direction facing = (Direction)state.data.piston.facing;
    bool powered = piston_input_powered(world, position, facing);
    bool changed = state.data.piston.powered != powered;

    if (changed) {
        state.data.piston.powered = powered;
        if (!world_write_visual(world, position, state)) return;

        if (state.data.piston.extended) {
            BlockPos head_position = block_offset(position, facing);
            BlockState head = world_get(world, head_position);
            if (block_piston_pair((BlockId)state.id, (BlockId)head.id) &&
                head.data.piston.facing == state.data.piston.facing &&
                head.data.piston.powered != powered) {
                head.data.piston.powered = powered;
                if (!world_write_visual(world, head_position, head)) return;
            }
        }
        sim_cancel(world, position);
    }

    if (powered != state.data.piston.extended &&
        !scheduler_pending(&world->sim->scheduler, position))
        sim_schedule(world, position, PISTON_DELAY);
}

static void piston_neighbor_changed(World *world, BlockPos position)
{
    sim_mark_input(world, position);
}

static void piston_head_neighbor_changed(World *world, BlockPos position)
{
    BlockState head = world_get(world, position);
    if (!block_is_piston_head((BlockId)head.id)) return;
    Direction facing = (Direction)head.data.piston.facing;
    BlockPos base_position = block_offset(position,
                                           direction_opposite(facing));
    BlockState base = world_get(world, base_position);
    if (!block_piston_pair((BlockId)base.id, (BlockId)head.id) ||
        !base.data.piston.extended ||
        base.data.piston.facing != head.data.piston.facing)
        world_remove_internal(world, position);
}

static TickAction piston_tick(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    TickAction action = {state, false, 0};
    if (!block_is_piston_base((BlockId)state.id)) return action;

    Direction facing = (Direction)state.data.piston.facing;
    bool powered = piston_input_powered(world, position, facing);
    if (state.data.piston.powered != powered) {
        state.data.piston.powered = powered;
        if (!world_write_visual(world, position, state)) return action;
    }
    if (powered != state.data.piston.extended)
        piston_set_extended(world, position, state, powered);
    return action;
}

static uint8_t piston_signal(const World *world, BlockPos position,
                             Direction face, SignalMode mode)
{
    (void)face;
    return power_conducted(world, position, mode);
}

const BlockDef rs_piston_def = {
    .name = "piston",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    .signal = piston_signal,
    .neighbor_changed = piston_neighbor_changed,
    .input_changed = piston_check_input,
    .scheduled_tick = piston_tick
};

const BlockDef rs_sticky_piston_def = {
    .name = "sticky_piston",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    .signal = piston_signal,
    .neighbor_changed = piston_neighbor_changed,
    .input_changed = piston_check_input,
    .scheduled_tick = piston_tick
};

const BlockDef rs_piston_head_def = {
    .name = "piston_head",
    .neighbor_changed = piston_head_neighbor_changed
};

const BlockDef rs_sticky_piston_head_def = {
    .name = "sticky_piston_head",
    .neighbor_changed = piston_head_neighbor_changed
};
