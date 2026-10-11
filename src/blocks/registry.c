#include "block_internal.h"

#include <string.h>

static const BlockDef air = { .name = "air" };

const BlockDef *block_definition(BlockId id)
{
    static const BlockDef *const definitions[BLOCK_COUNT] = {
        &air, &rs_iron_def, &rs_redstone_block_def, &rs_wire_def, &rs_torch_def
    };
    return (unsigned)id < BLOCK_COUNT ? definitions[id] : &air;
}

const char *block_name(BlockId id)
{
    return block_definition(id)->name;
}

BlockState block_default(BlockId id)
{
    BlockState state;
    memset(&state, 0, sizeof(state));
    state.id = (uint8_t)id;
    if (id == BLOCK_REDSTONE_TORCH) {
        state.data.torch.support_dir = DIR_DOWN;
        state.data.torch.lit = true;
    }
    return state;
}

bool direction_horizontal(Direction direction)
{
    return direction == DIR_EAST || direction == DIR_WEST ||
           direction == DIR_SOUTH || direction == DIR_NORTH;
}

Direction direction_opposite(Direction direction)
{
    static const Direction opposite[DIR_COUNT] = {
        DIR_WEST, DIR_EAST, DIR_DOWN, DIR_UP, DIR_NORTH, DIR_SOUTH
    };
    return (unsigned)direction < DIR_COUNT ? opposite[direction] : DIR_COUNT;
}

BlockPos block_offset(BlockPos position, Direction direction)
{
    static const BlockPos offsets[DIR_COUNT] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    };
    if ((unsigned)direction < DIR_COUNT) {
        position.x += offsets[direction].x;
        position.y += offsets[direction].y;
        position.z += offsets[direction].z;
    }
    return position;
}
