#include "block_internal.h"
#include "sim_internal.h"

NoteInstrument note_instrument(BlockState state)
{
    switch ((BlockId)state.id) {
    case BLOCK_IRON: return NOTE_IRON_XYLOPHONE;
    case BLOCK_GLASS: return NOTE_HAT;
    case BLOCK_OBSERVER:
    case BLOCK_DROPPER: return NOTE_BASEDRUM;
    case BLOCK_BARREL:
    case BLOCK_NOTE_BLOCK: return NOTE_BASS;
    case BLOCK_SLIME:
    case BLOCK_HONEY: return NOTE_FLUTE;
    default: return NOTE_HARP;
    }
}

const char *note_instrument_name(NoteInstrument instrument)
{
    static const char *const names[NOTE_INSTRUMENT_COUNT] = {
        "harp", "basedrum", "snare", "hat", "bass", "flute", "bell", "guitar",
        "chime", "xylophone", "iron_xylophone", "cow_bell", "didgeridoo",
        "bit", "banjo", "pling"
    };
    return (unsigned)instrument < NOTE_INSTRUMENT_COUNT ? names[instrument] : "unknown";
}

static void note_block_check_input(World *world, BlockPos position)
{
    BlockState state = world_get(world, position);
    if (state.id != BLOCK_NOTE_BLOCK) return;

    bool powered = power_received(world, position, SIGNAL_ALL) > 0;
    if (state.data.note_block.powered == powered) return;

    state.data.note_block.powered = powered;
    (void)world_set_block(world, position, state, 3);
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
