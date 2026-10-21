#include "block_internal.h"
#include "sim_internal.h"

enum {
    PISTON_MOTION_TICKS = 2,
    PISTON_PUSH_LIMIT = 12,
    PISTON_DESTROY_LIMIT = PISTON_PUSH_LIMIT*DIR_COUNT
};

typedef struct {
    World *world;
    BlockPos piston_position;
    BlockPos start_position;
    Direction piston_direction;
    Direction push_direction;
    BlockPos to_push[PISTON_PUSH_LIMIT];
    size_t push_count;
    BlockPos to_destroy[PISTON_DESTROY_LIMIT];
    size_t destroy_count;
    bool extending;
} PistonResolver;

static const Direction piston_directions[] = {
    DIR_DOWN, DIR_UP, DIR_NORTH, DIR_SOUTH, DIR_WEST, DIR_EAST
};

static bool piston_input_powered(const World *world, BlockPos position,
                                 Direction facing)
{
    if ((unsigned)facing >= DIR_COUNT) return false;

    /* Vanilla pistons ignore power entering through their moving face. */
    for (unsigned i = 0; i < DIR_COUNT; ++i) {
        Direction direction = piston_directions[i];
        if (direction == facing) continue;
        BlockPos source = block_offset(position, direction);
        if (power_signal(world, source, direction_opposite(direction),
                         SIGNAL_ALL) > 0)
            return true;
    }

    /* Java Edition quasi-connectivity: also sample the neighbors of the cell
     * above the piston, except the piston cell itself. */
    BlockPos above = block_offset(position, DIR_UP);
    for (unsigned i = 0; i < DIR_COUNT; ++i) {
        Direction direction = piston_directions[i];
        if (direction == DIR_DOWN) continue;
        BlockPos source = block_offset(above, direction);
        if (power_signal(world, source, direction_opposite(direction),
                         SIGNAL_ALL) > 0)
            return true;
    }
    return false;
}

static PushReaction piston_reaction(BlockState state)
{
    BlockId id = (BlockId)state.id;
    if (id == BLOCK_AIR) return PUSH_IGNORE;
    if (block_is_piston_base(id) && state.data.piston.extended)
        return PUSH_BLOCK;
    return block_definition(id)->push_reaction;
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

static int piston_position_index(const BlockPos positions[], size_t count,
                                 BlockPos position)
{
    for (size_t index = 0; index < count; ++index)
        if (pos_equal(positions[index], position)) return (int)index;
    return -1;
}

static BlockPos piston_offset(BlockPos position, Direction direction,
                              size_t distance)
{
    while (distance--) position = block_offset(position, direction);
    return position;
}

static bool piston_is_sticky(BlockState state)
{
    return state.id == BLOCK_SLIME || state.id == BLOCK_HONEY;
}

static bool piston_blocks_stick(BlockState first, BlockState second)
{
    if ((first.id == BLOCK_SLIME && second.id == BLOCK_HONEY) ||
        (first.id == BLOCK_HONEY && second.id == BLOCK_SLIME))
        return false;
    return piston_is_sticky(first) || piston_is_sticky(second);
}

static bool piston_same_axis(Direction first, Direction second)
{
    return (unsigned)first < DIR_COUNT && (unsigned)second < DIR_COUNT &&
           first/2 == second/2;
}

static bool piston_is_pushable(const PistonResolver *resolver,
                               BlockPos position, Direction movement,
                               bool allow_destroy,
                               Direction piston_direction)
{
    if (!world_in_bounds(resolver->world, position)) return false;
    BlockState state = world_get(resolver->world, position);
    if (state.id == BLOCK_AIR) return true;

    BlockPos destination = block_offset(position, movement);
    if (!world_in_bounds(resolver->world, destination)) return false;

    PushReaction reaction = piston_reaction(state);
    if (reaction == PUSH_BLOCK) return false;
    if (reaction == PUSH_DESTROY) return allow_destroy;
    if (reaction == PUSH_ONLY) return movement == piston_direction;
    return true;
}

static bool piston_add_destroy(PistonResolver *resolver, BlockPos position)
{
    if (piston_position_index(resolver->to_destroy,
                              resolver->destroy_count, position) >= 0)
        return true;
    if (resolver->destroy_count == PISTON_DESTROY_LIMIT) return false;
    resolver->to_destroy[resolver->destroy_count++] = position;
    return true;
}

static void piston_reorder_collision(PistonResolver *resolver,
                                     size_t line_count,
                                     size_t collision_index)
{
    BlockPos reordered[PISTON_PUSH_LIMIT];
    size_t output = 0;
    size_t tail = resolver->push_count - line_count;

    for (size_t index = 0; index < collision_index; ++index)
        reordered[output++] = resolver->to_push[index];
    for (size_t index = tail; index < resolver->push_count; ++index)
        reordered[output++] = resolver->to_push[index];
    for (size_t index = collision_index; index < tail; ++index)
        reordered[output++] = resolver->to_push[index];

    for (size_t index = 0; index < resolver->push_count; ++index)
        resolver->to_push[index] = reordered[index];
}

static bool piston_add_branching_blocks(PistonResolver *, BlockPos);

static bool piston_add_block_line(PistonResolver *resolver,
                                  BlockPos position,
                                  Direction piston_direction)
{
    BlockState state = world_get(resolver->world, position);
    if (state.id == BLOCK_AIR) return true;
    if (!piston_is_pushable(resolver, position, resolver->push_direction,
                            false, piston_direction))
        return true;
    if (pos_equal(position, resolver->piston_position)) return true;
    if (piston_position_index(resolver->to_push,
                              resolver->push_count, position) >= 0)
        return true;

    size_t line_count = 1;
    if (line_count + resolver->push_count > PISTON_PUSH_LIMIT) return false;

    while (piston_is_sticky(state)) {
        BlockPos behind = piston_offset(
            position, direction_opposite(resolver->push_direction),
            line_count);
        BlockState previous = state;
        state = world_get(resolver->world, behind);
        if (state.id == BLOCK_AIR ||
            !piston_blocks_stick(previous, state) ||
            !piston_is_pushable(
                resolver, behind, resolver->push_direction, false,
                direction_opposite(resolver->push_direction)) ||
            pos_equal(behind, resolver->piston_position))
            break;
        ++line_count;
        if (line_count + resolver->push_count > PISTON_PUSH_LIMIT)
            return false;
    }

    for (size_t index = line_count; index > 0; --index) {
        resolver->to_push[resolver->push_count++] = piston_offset(
            position, direction_opposite(resolver->push_direction),
            index - 1);
    }

    size_t added = line_count;
    for (size_t distance = 1;; ++distance) {
        BlockPos forward = piston_offset(position, resolver->push_direction,
                                         distance);
        int collision = piston_position_index(
            resolver->to_push, resolver->push_count, forward);
        if (collision >= 0) {
            piston_reorder_collision(resolver, added, (size_t)collision);
            size_t branch_limit = (size_t)collision + added;
            for (size_t index = 0; index <= branch_limit; ++index) {
                BlockPos candidate = resolver->to_push[index];
                if (piston_is_sticky(
                        world_get(resolver->world, candidate)) &&
                    !piston_add_branching_blocks(resolver, candidate))
                    return false;
            }
            return true;
        }

        if (!world_in_bounds(resolver->world, forward)) return false;
        state = world_get(resolver->world, forward);
        if (state.id == BLOCK_AIR) return true;
        if (!piston_is_pushable(
                resolver, forward, resolver->push_direction, true,
                resolver->push_direction) ||
            pos_equal(forward, resolver->piston_position))
            return false;
        if (piston_reaction(state) == PUSH_DESTROY)
            return piston_add_destroy(resolver, forward);
        if (resolver->push_count == PISTON_PUSH_LIMIT) return false;
        resolver->to_push[resolver->push_count++] = forward;
        ++added;
    }
}

static bool piston_add_branching_blocks(PistonResolver *resolver,
                                        BlockPos position)
{
    BlockState sticky = world_get(resolver->world, position);
    for (unsigned i = 0; i < DIR_COUNT; ++i) {
        Direction direction = piston_directions[i];
        if (piston_same_axis(direction, resolver->push_direction)) continue;
        BlockPos neighbor = block_offset(position, direction);
        BlockState state = world_get(resolver->world, neighbor);
        if (piston_blocks_stick(state, sticky) &&
            !piston_add_block_line(resolver, neighbor, direction))
            return false;
    }
    return true;
}

static bool piston_resolve(PistonResolver *resolver)
{
    resolver->push_count = 0;
    resolver->destroy_count = 0;
    BlockState start = world_get(resolver->world, resolver->start_position);

    if (!piston_is_pushable(
            resolver, resolver->start_position, resolver->push_direction,
            false, resolver->piston_direction)) {
        return resolver->extending &&
               piston_reaction(start) == PUSH_DESTROY &&
               piston_add_destroy(resolver, resolver->start_position);
    }
    if (!piston_add_block_line(resolver, resolver->start_position,
                               resolver->push_direction))
        return false;

    for (size_t index = 0; index < resolver->push_count; ++index) {
        BlockPos position = resolver->to_push[index];
        if (piston_is_sticky(world_get(resolver->world, position)) &&
            !piston_add_branching_blocks(resolver, position))
            return false;
    }
    return true;
}

static bool piston_move_blocks(World *world, BlockPos position,
                               Direction facing, bool extending,
                               bool *pushed_block)
{
    PistonResolver resolver = {
        .world = world,
        .piston_position = position,
        .start_position = piston_offset(position, facing,
                                        extending ? 1 : 2),
        .piston_direction = facing,
        .push_direction = extending ? facing : direction_opposite(facing),
        .extending = extending
    };
    BlockState moving[PISTON_PUSH_LIMIT];

    if (pushed_block) *pushed_block = false;
    if (!piston_resolve(&resolver)) return false;

    for (size_t index = 0; index < resolver.push_count; ++index)
        moving[index] = world_get(world, resolver.to_push[index]);

    for (size_t index = resolver.destroy_count; index > 0; --index) {
        world_remove_internal(world, resolver.to_destroy[index - 1]);
        if (world->sim->error != RS_OK) return false;
    }

    for (size_t index = resolver.push_count; index > 0; --index) {
        BlockPos source = resolver.to_push[index - 1];
        BlockPos destination = block_offset(source,
                                            resolver.push_direction);
        BlockState state = moving[index - 1];
        /* A moved target has no reset tick at its new position. Java 1.16.2
         * clears its projectile output when placing it there. */
        if (state.id == BLOCK_TARGET) state.data.target.power = 0;
        if (!piston_write_cell(world, destination, state)) return false;
        if (!piston_write_cell(world, source, block_default(BLOCK_AIR)))
            return false;
        piston_reinitialize_moved_block(world, destination, state);
    }

    if (pushed_block) *pushed_block = resolver.push_count != 0;
    return true;
}

static bool piston_set_extended(World *world, BlockPos position,
                                BlockState state, bool extended,
                                bool pull_block, bool *pushed_block)
{
    Direction facing = (Direction)state.data.piston.facing;
    BlockPos head_position = block_offset(position, facing);
    BlockId base_id = (BlockId)state.id;
    BlockId head_id = base_id == BLOCK_STICKY_PISTON ?
                      BLOCK_STICKY_PISTON_HEAD : BLOCK_PISTON_HEAD;

    if (pushed_block) *pushed_block = false;

    if (extended) {
        bool moved_block = false;
        if (!piston_move_blocks(world, position, facing, true,
                                &moved_block)) return false;
        BlockState head = block_default(head_id);
        head.data.piston.facing = state.data.piston.facing;
        head.data.piston.powered = state.data.piston.powered;
        if (!piston_write_cell(world, head_position, head)) return false;
        state.data.piston.extended = true;
        if (!world_write_derived(world, position, state)) return false;
        sim_notify_change(world, position);
        if (pushed_block) *pushed_block = moved_block;
        return true;
    }

    BlockState head = world_get(world, head_position);
    bool had_head = block_piston_pair(base_id, (BlockId)head.id) &&
                    head.data.piston.facing == state.data.piston.facing;
    if (had_head) {
        if (!piston_write_cell(world, head_position,
                               block_default(BLOCK_AIR))) return false;
    }

    if (base_id == BLOCK_STICKY_PISTON && had_head && pull_block) {
        BlockPos source = block_offset(head_position, facing);
        if (world_in_bounds(world, source)) {
            BlockState pulled = world_get(world, source);
            if (piston_reaction(pulled) == PUSH_NORMAL) {
                if (!piston_move_blocks(world, position, facing, false,
                                        NULL) &&
                    world->sim->error != RS_OK)
                    return false;
            }
        }
    }
    state.data.piston.extended = false;
    if (!world_write_derived(world, position, state)) return false;
    sim_notify_change(world, position);
    return true;
}

static bool piston_update_indicator(World *world, BlockPos position,
                                     BlockState *current, bool powered)
{
    BlockState state = *current;
    Direction facing = (Direction)state.data.piston.facing;
    bool changed = state.data.piston.powered != powered;

    if (changed) {
        state.data.piston.powered = powered;
        if (!world_write_visual(world, position, state)) return false;

        if (state.data.piston.extended) {
            BlockPos head_position = block_offset(position, facing);
            BlockState head = world_get(world, head_position);
            if (block_piston_pair((BlockId)state.id, (BlockId)head.id) &&
                head.data.piston.facing == state.data.piston.facing &&
                head.data.piston.powered != powered) {
                head.data.piston.powered = powered;
                if (!world_write_visual(world, head_position, head)) return false;
            }
        }
    }
    *current = state;
    return true;
}

void piston_check_input(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    if (!block_is_piston_base((BlockId)state.id)) return;
    Direction facing = (Direction)state.data.piston.facing;
    bool powered = piston_input_powered(world, position, facing);
    if (!piston_update_indicator(world, position, &state, powered)) return;
    if (powered && !state.data.piston.extended) {
        PistonResolver resolver = {
            .world = world, .piston_position = position,
            .start_position = block_offset(position, facing),
            .piston_direction = facing, .push_direction = facing,
            .extending = true
        };
        if (piston_resolve(&resolver))
            (void)sim_queue_piston_event(world,
                (PistonEvent){position, state.id, 0, (uint8_t)facing});
    } else if (!powered && state.data.piston.extended) {
        /* A retraction requested while extension is still moving is event 2
         * in Java 1.16.2. Sticky pistons retract but leave the moved block. */
        bool extension_in_progress =
            sim_has_scheduled_tick(world, position) || sim_will_tick(world, position);
        (void)sim_queue_piston_event(world,
            (PistonEvent){position, state.id, extension_in_progress ? 2 : 1,
                           (uint8_t)facing});
    }
}

void piston_trigger_event(World *world, PistonEvent event)
{
    BlockState state = world_get(world, event.pos);
    if (state.id != event.block) return;
    Direction facing = (Direction)state.data.piston.facing;
    bool powered = piston_input_powered(world, event.pos, facing);
    if (!piston_update_indicator(world, event.pos, &state, powered)) return;
    /* A request records an edge, not permission to move later. Vanilla
     * rechecks the actual power when the block-event FIFO reaches this entry. */
    if (event.type == 0) {
        if (!powered) return;
        bool pushed_block = false;
        if (piston_set_extended(world, event.pos, state, true, false, &pushed_block) &&
            pushed_block)
            sim_schedule(world, event.pos, PISTON_MOTION_TICKS);
    } else {
        if (powered) return;
        sim_cancel(world, event.pos);
        (void)piston_set_extended(world, event.pos, state, false, event.type == 1, NULL);
    }
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

static TickAction piston_motion_tick(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    TickAction action = {state, false, 0};
    /* The event marks the end of the two-game-tick movement interval. The
     * compact renderer has no moving block entity, so no state write is
     * needed here; absence of the pending event is the completed state. */
    return action;
}

const BlockDef rs_piston_def = {
    .name = "piston",
    .full_cube = true,
    .supports_wire = true,
    .push_reaction = PUSH_NORMAL,
    .neighbor_changed = piston_neighbor_changed,
    .input_changed = piston_check_input,
    .scheduled_tick = piston_motion_tick
};

const BlockDef rs_sticky_piston_def = {
    .name = "sticky_piston",
    .full_cube = true,
    .supports_wire = true,
    .push_reaction = PUSH_NORMAL,
    .neighbor_changed = piston_neighbor_changed,
    .input_changed = piston_check_input,
    .scheduled_tick = piston_motion_tick
};

const BlockDef rs_piston_head_def = {
    .name = "piston_head",
    .push_reaction = PUSH_BLOCK,
    .neighbor_changed = piston_head_neighbor_changed
};

const BlockDef rs_sticky_piston_head_def = {
    .name = "sticky_piston_head",
    .push_reaction = PUSH_BLOCK,
    .neighbor_changed = piston_head_neighbor_changed
};
