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
