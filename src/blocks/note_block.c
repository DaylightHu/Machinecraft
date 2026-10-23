#include "block_internal.h"
#include "sim_internal.h"

static void note_block_check_input(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    if (state.id != BLOCK_NOTE_BLOCK) return;

    bool powered = power_received(world, position, SIGNAL_ALL) > 0;
    if (state.data.note_block.powered == powered) return;

    state.data.note_block.powered = powered;
    if (!world_set_block(world, position, state, 3)) return;

    /* Both edges are real block-state transitions. This wakes observers and
     * keeps neighboring redstone synchronized, while deliberately producing
     * no sound, pitch state, instrument state, or particle. */
}

const BlockDef rs_note_block_def = {
    .name = "note_block",
    .full_cube = true,
    .conductor = true,
    .supports_wire = true,
    .push_reaction = PUSH_NORMAL,
    .neighbor_changed = note_block_check_input,
    .input_changed = note_block_check_input
};
