#include "blocks/block_internal.h"
#include "rs/sim.h"

static uint8_t max_power(uint8_t first, uint8_t second)
{
    return first > second ? first : second;
}

uint8_t power_signal(const World *world, BlockPos position, Direction face, SignalMode mode)
{
    if (!world_in_bounds(world, position) || (unsigned)face >= DIR_COUNT) {
        return 0;
    }
    const BlockDef *definition = block_definition((BlockId)world_get(world, position).id);
    return definition->signal ? definition->signal(world, position, face, mode) : 0;
}

uint8_t power_direct(const World *world, BlockPos position, Direction face, SignalMode mode)
{
    if (!world_in_bounds(world, position) || (unsigned)face >= DIR_COUNT) {
        return 0;
    }
    const BlockDef *definition = block_definition((BlockId)world_get(world, position).id);
    return definition->direct_signal ? definition->direct_signal(world, position, face, mode) : 0;
}

uint8_t power_received(const World *world, BlockPos position, SignalMode mode)
{
    uint8_t best = 0;
    for (Direction face = 0; face < DIR_COUNT; ++face) {
        BlockPos neighbor = block_offset(position, face);
        best = max_power(best, power_signal(world, neighbor, direction_opposite(face), mode));
    }
    return best;
}

uint8_t power_conducted(const World *world, BlockPos position, SignalMode mode)
{
    uint8_t best = 0;
    for (Direction face = 0; face < DIR_COUNT; ++face) {
        BlockPos neighbor = block_offset(position, face);
        best = max_power(best, power_direct(world, neighbor, direction_opposite(face), mode));
    }
    return best;
}

bool block_is_conductor(const World *world, BlockPos position)
{
    return block_definition((BlockId)world_get(world, position).id)->conductor;
}

bool block_supports_wire(const World *world, BlockPos position)
{
    return block_definition((BlockId)world_get(world, position).id)->supports_wire;
}

bool block_supports_torch(const World *world, BlockPos position)
{
    return block_definition((BlockId)world_get(world, position).id)->full_cube;
}

bool block_is_diode(const World *world, BlockPos position)
{
    return block_definition((BlockId)world_get(world, position).id)->diode;
}

SolidPower redstone_solid_power(const World *world, BlockPos position)
{
    if (!world || !block_is_conductor(world, position)) {
        return (SolidPower){0, 0};
    }
    return (SolidPower){
        power_received(world, position, SIGNAL_ALL),
        power_conducted(world, position, SIGNAL_WITHOUT_WIRE)
    };
}

uint8_t redstone_signal(const World *world, BlockPos position, Direction face)
{
    return world ? power_signal(world, position, face, SIGNAL_ALL) : 0;
}
