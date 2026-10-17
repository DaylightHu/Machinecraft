#include "block_internal.h"

#include <string.h>
#include <limits.h>

static const BlockDef air = { .name = "air" };

const BlockDef *block_definition(BlockId id)
{
    static const BlockDef *const definitions[BLOCK_COUNT] = {
        [BLOCK_AIR] = &air,
        [BLOCK_IRON] = &rs_iron_def,
        [BLOCK_REDSTONE_BLOCK] = &rs_redstone_block_def,
        [BLOCK_REDSTONE_WIRE] = &rs_wire_def,
        [BLOCK_REDSTONE_TORCH] = &rs_torch_def,
        [BLOCK_GLASS] = &rs_glass_def
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
        int64_t x = (int64_t)position.x + offsets[direction].x;
        int64_t y = (int64_t)position.y + offsets[direction].y;
        int64_t z = (int64_t)position.z + offsets[direction].z;
        if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX || z < INT_MIN || z > INT_MAX)
            return (BlockPos){INT_MIN, INT_MIN, INT_MIN};
        position = (BlockPos){(int)x, (int)y, (int)z};
    }
    return position;
}
