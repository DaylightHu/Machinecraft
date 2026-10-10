#include "rs/block.h"

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
