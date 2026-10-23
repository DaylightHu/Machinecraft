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
    uint8_t signal = definition->signal ?
                     definition->signal(world, position, face, mode) : 0;
    /* Level.getSignal combines the block's own output with direct power
     * received by a conductor. Direct queries never relay conductors, so
     * this cannot turn a line of solid blocks into a signal network. */
    if (signal < 15 && definition->conductor)
        signal = max_power(signal, power_conducted(world, position, mode));
    return signal;
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
        if (best == 15) break;
    }
    return best;
}

uint8_t power_conducted(const World *world, BlockPos position, SignalMode mode)
{
    uint8_t best = 0;
    for (Direction face = 0; face < DIR_COUNT; ++face) {
        BlockPos neighbor = block_offset(position, face);
        best = max_power(best, power_direct(world, neighbor, direction_opposite(face), mode));
        if (best == 15) break;
    }
    return best;
}

bool block_is_conductor(const World *world, BlockPos position)
{
    return block_definition((BlockId)world_get(world, position).id)->conductor;
}

bool block_face_sturdy(BlockState state, Direction face)
{
    if ((unsigned)face >= DIR_COUNT) return false;
    if (block_is_piston_base((BlockId)state.id) && state.data.piston.extended)
        return face == direction_opposite((Direction)state.data.piston.facing);
    if (block_is_piston_head((BlockId)state.id))
        return face == (Direction)state.data.piston.facing;
    return block_definition((BlockId)state.id)->full_cube;
}

bool block_state_supports_wire(BlockState state)
{
    /* RedStoneWireBlock.canSurviveOn uses a full top face, with the hopper
     * as an explicit exception. Extended pistons and heads have one full
     * face each; neither is an unconditional full cube. */
    return state.id == BLOCK_HOPPER || block_face_sturdy(state, DIR_UP);
}

bool block_supports_wire(const World *world, BlockPos position)
{
    return block_state_supports_wire(world_get(world, position));
}

bool block_supports_torch(const World *world, BlockPos position, Direction face)
{
    BlockState state = world_get(world, position);
    if (face != DIR_UP) return block_face_sturdy(state, face);
    if (block_is_piston_base((BlockId)state.id) && state.data.piston.extended)
        return state.data.piston.facing != DIR_UP;
    if (block_is_piston_head((BlockId)state.id))
        return state.data.piston.facing/2 == DIR_UP/2;
    return block_definition((BlockId)state.id)->full_cube;
}

bool block_is_diode(const World *world, BlockPos position)
{
    return block_definition((BlockId)world_get(world, position).id)->diode;
}

bool block_is_signal_source(const World *world, BlockPos position)
{
    return block_definition((BlockId)world_get(world, position).id)
        ->signal_source;
}

bool block_has_analog_output(const World *world, BlockPos position)
{
    return block_definition((BlockId)world_get(world, position).id)
        ->analog_output != NULL;
}

uint8_t block_analog_output(const World *world, BlockPos position)
{
    const BlockDef *definition = block_definition(
        (BlockId)world_get(world, position).id);
    return definition->analog_output ?
           definition->analog_output(world, position) : 0;
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
