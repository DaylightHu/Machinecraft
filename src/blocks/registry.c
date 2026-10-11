#include "block_internal.h"
#include <string.h>

static const BlockDef air = { .name = "air"}

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
    BlockState state = {0};
    state.id = (uint8_t)id;
    if (id == BLOCK_REDSTONE_TORCH) {
        state.data.torch.support_dir = DIR_DOWN;
        state.data.torch.lit = true;
    }
    return state;
}

BlockPos block_offset(BlockPos position, Direction direction)
{
    static const BlockPos offsets[DIR_COUNT] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    };
    if ((unsigned)direction < DIR_COUNT) {
        position.x += offsets[direction].x;
        position.y += offsets[direction].y;
        position.z += offsets[direction].z;
    }
    return position;
}
