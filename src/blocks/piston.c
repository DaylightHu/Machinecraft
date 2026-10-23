#include "block_internal.h"
#include "sim_internal.h"

enum {
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

static bool piston_write_cell(World *world, BlockPos position,
                              BlockState state)
{
    sim_forget_cell(world, position);
    if (!world_set_block(world, position, state, 3 | UPDATE_MOVING)) return false;
    return true;
}

static bool piston_start_motion(World *world, BlockPos position,
                                BlockState moved_state, Direction direction,
                                bool extending, bool source, bool sticky)
{
    BlockState placeholder = block_default(BLOCK_MOVING_PISTON);
    placeholder.data.moving_piston.facing = (uint8_t)direction;
    placeholder.data.moving_piston.sticky = sticky;
    if (!piston_write_cell(world, position, placeholder)) return false;
    return sim_add_piston_motion(world, (PistonMotion){
        .pos = position,
        .moved_state = moved_state,
        .direction = (uint8_t)direction,
        .extending = extending,
        .source = source
    });
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

static bool piston_apply_resolved(PistonResolver *resolver)
{
    World *world = resolver->world;
    BlockState moving[PISTON_PUSH_LIMIT];

    if (!sim_prepare_piston_motions(world, resolver->push_count)) return false;

    for (size_t index = 0; index < resolver->push_count; ++index)
        moving[index] = world_get(world, resolver->to_push[index]);

    for (size_t index = resolver->destroy_count; index > 0; --index) {
        world_remove_internal(world, resolver->to_destroy[index - 1]);
        if (world->sim->error != RS_OK) return false;
    }

    for (size_t index = resolver->push_count; index > 0; --index) {
        BlockPos source = resolver->to_push[index - 1];
        BlockPos destination = block_offset(source,
                                            resolver->push_direction);
        BlockState state = moving[index - 1];
        /* A moved target has no reset tick at its new position. Java 1.16.2
         * clears its projectile output when placing it there. */
        if (state.id == BLOCK_TARGET) state.data.target.power = 0;
        if (!piston_start_motion(world, destination, state,
                                 resolver->piston_direction,
                                 resolver->extending, false, false))
            return false;
        if (!piston_write_cell(world, source, block_default(BLOCK_AIR)))
            return false;
    }
    return true;
}

static bool piston_extend(World *world, BlockPos position, BlockState state)
{
    Direction facing = (Direction)state.data.piston.facing;
    BlockPos head_position = block_offset(position, facing);
    BlockId base_id = (BlockId)state.id;
    BlockId head_id = base_id == BLOCK_STICKY_PISTON ?
                      BLOCK_STICKY_PISTON_HEAD : BLOCK_PISTON_HEAD;
    PistonResolver resolver = {
        .world = world,
        .piston_position = position,
        .start_position = head_position,
        .piston_direction = facing,
        .push_direction = facing,
        .extending = true
    };

    if (!piston_resolve(&resolver) ||
        !sim_prepare_piston_motions(world, resolver.push_count + 1) ||
        !piston_apply_resolved(&resolver))
        return false;
    BlockState head = block_default(head_id);
    head.data.piston.facing = state.data.piston.facing;
    head.data.piston.powered = state.data.piston.powered;
    if (!piston_start_motion(world, head_position, head, facing, true, true,
                             base_id == BLOCK_STICKY_PISTON))
        return false;
    state.data.piston.extended = true;
    return world_set_block(world, position, state, 3 | UPDATE_MOVING);
}

static bool piston_retract(World *world, BlockPos position, BlockState state,
                           bool pull_block)
{
    Direction facing = (Direction)state.data.piston.facing;
    BlockPos head_position = block_offset(position, facing);
    BlockId base_id = (BlockId)state.id;

    /* Retraction always creates a source moving piston, and can create up to
     * twelve cargo motions. Reserve before the first observable write. */
    if (!sim_prepare_piston_motions(world, PISTON_PUSH_LIMIT + 1)) return false;

    (void)sim_finish_piston_motion(world, head_position, true);
    if (world->sim->error != RS_OK) return false;

    state.data.piston.extended = false;
    if (!piston_start_motion(world, position, state, facing, false, true,
                             base_id == BLOCK_STICKY_PISTON))
        return false;

    if (base_id == BLOCK_STICKY_PISTON) {
        BlockPos source = block_offset(head_position, facing);
        const PistonMotion *motion = sim_piston_motion(world, source);
        BlockState placeholder = world_get(world, source);
        bool intercepted = placeholder.id == BLOCK_MOVING_PISTON && motion &&
                           motion->direction == facing && motion->extending;
        if (intercepted) {
            (void)sim_finish_piston_motion(world, source, true);
            return world->sim->error == RS_OK;
        }

        if (pull_block && world_in_bounds(world, source) &&
            piston_reaction(world_get(world, source)) == PUSH_NORMAL) {
            /* moveBlocks removes the completed head before resolving the
             * backwards move; otherwise the head itself blocks the cargo. */
            if (world_get(world, head_position).id != BLOCK_AIR &&
                !piston_write_cell(world, head_position,
                                   block_default(BLOCK_AIR)))
                return false;
            PistonResolver resolver = {
                .world = world,
                .piston_position = position,
                .start_position = source,
                .piston_direction = facing,
                .push_direction = direction_opposite(facing),
                .extending = false
            };
            if (piston_resolve(&resolver)) {
                return piston_apply_resolved(&resolver);
            }
        }
    }

    BlockState head = world_get(world, head_position);
    if (head.id != BLOCK_AIR) {
        if (!piston_write_cell(world, head_position,
                               block_default(BLOCK_AIR))) return false;
    }
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

static bool piston_extension_is_short(const World *world, BlockPos position,
                                      Direction facing)
{
    BlockPos moving_position = piston_offset(position, facing, 2);
    BlockState placeholder = world_get(world, moving_position);
    const PistonMotion *motion = sim_piston_motion(world, moving_position);
    return placeholder.id == BLOCK_MOVING_PISTON &&
           placeholder.data.moving_piston.facing == facing && motion &&
           motion->direction == facing && motion->extending &&
           (motion->previous_progress < 1 ||
            motion->last_ticked == world->tick ||
            world->sim->handling_tick);
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
        /* Java checks the extending moving block entity two cells ahead. Its
         * getProgress(0) value is previous_progress, not the render value. */
        bool extension_in_progress =
            piston_extension_is_short(world, position, facing);
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
        sim_begin_updates(world);
        (void)piston_extend(world, event.pos, state);
        sim_end_updates(world);
    } else {
        if (powered) return;
        sim_begin_updates(world);
        (void)piston_retract(world, event.pos, state, event.type == 1);
        sim_end_updates(world);
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
    if (block_piston_pair((BlockId)base.id, (BlockId)head.id) &&
        base.data.piston.extended &&
        base.data.piston.facing == head.data.piston.facing)
        sim_mark_input(world, base_position);
}

const BlockDef rs_piston_def = {
    .name = "piston",
    .full_cube = true,
    .supports_wire = true,
    .push_reaction = PUSH_NORMAL,
    .neighbor_changed = piston_neighbor_changed,
    .input_changed = piston_check_input
};

const BlockDef rs_sticky_piston_def = {
    .name = "sticky_piston",
    .full_cube = true,
    .supports_wire = true,
    .push_reaction = PUSH_NORMAL,
    .neighbor_changed = piston_neighbor_changed,
    .input_changed = piston_check_input
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
