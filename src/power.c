#include "blocks/block_internal.h"
#include "sim_internal.h"

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

typedef struct { int min[3], max[3]; } SupportBox;
typedef enum { SUPPORT_FULL, SUPPORT_CENTER, SUPPORT_RIGID } SupportType;

static SupportBox piston_base_box(Direction facing, bool extended)
{
    SupportBox box = {{0, 0, 0}, {16, 16, 16}};
    if (extended) {
        if (facing % 2) box.min[facing/2] = 4;
        else box.max[facing/2] = 12;
    }
    return box;
}

static void piston_head_boxes(Direction facing, bool short_head, SupportBox boxes[2])
{
    unsigned axis = facing/2;
    boxes[0] = (SupportBox){{0, 0, 0}, {16, 16, 16}};
    boxes[1] = (SupportBox){{6, 6, 6}, {10, 10, 10}};
    if (facing % 2) {
        boxes[0].max[axis] = 4;
        boxes[1].min[axis] = 4;
        boxes[1].max[axis] = short_head ? 16 : 20;
    } else {
        boxes[0].min[axis] = 12;
        boxes[1].min[axis] = short_head ? 0 : -4;
        boxes[1].max[axis] = 12;
    }
}

static bool moving_support(const World *world, BlockPos position,
                            Direction face, SupportType type)
{
    const PistonMotion *motion = sim_piston_motion(world, position);
    if (!motion) return false;
    SupportBox boxes[3];
    unsigned count = 0, moving_start = 0;
    BlockState state = motion->moved_state;
    Direction facing = (Direction)motion->direction;
    if (motion->source && !motion->extending) {
        boxes[count++] = piston_base_box((Direction)state.data.piston.facing, true);
        moving_start = count;
    }
    if (motion->source) {
        bool short_head = motion->extending != (motion->progress == 2);
        piston_head_boxes(facing, short_head, boxes + count);
        count += 2;
    } else if (block_is_piston_base((BlockId)state.id)) {
        boxes[count++] = piston_base_box((Direction)state.data.piston.facing,
                                          state.data.piston.extended);
    } else if (state.id == BLOCK_HONEY) {
        boxes[count++] = (SupportBox){{1, 0, 1}, {15, 15, 15}};
    } else if (block_definition((BlockId)state.id)->full_cube) {
        boxes[count++] = (SupportBox){{0, 0, 0}, {16, 16, 16}};
    }
    int offset = motion->extending ? (int)motion->progress*8 - 16 :
                                    16 - (int)motion->progress*8;
    if (facing % 2) offset = -offset;
    for (unsigned i = moving_start; i < count; ++i) {
        boxes[i].min[facing/2] += offset;
        boxes[i].max[facing/2] += offset;
    }
    unsigned axis = face/2, first = (axis + 1)%3, second = (axis + 2)%3;
    uint16_t rows[16] = {0};
    for (unsigned i = 0; i < count; ++i) {
        SupportBox box = boxes[i];
        bool intersects = face % 2 ? box.min[axis] <= 0 && box.max[axis] > 0 :
                                     box.min[axis] < 16 && box.max[axis] >= 16;
        if (!intersects) continue;
        int x0 = box.min[first], x1 = box.max[first];
        int y0 = box.min[second], y1 = box.max[second];
        if (type == SUPPORT_FULL && (x0 < 0 || x1 > 16 || y0 < 0 || y1 > 16))
            return false;
        if (x0 < 0) x0 = 0;
        if (x1 > 16) x1 = 16;
        if (y0 < 0) y0 = 0;
        if (y1 > 16) y1 = 16;
        if (x0 >= x1 || y0 >= y1) continue;
        uint16_t mask = (uint16_t)(((UINT32_C(1) << (x1 - x0)) - 1) << x0);
        for (int row = y0; row < y1; ++row) rows[row] |= mask;
    }
    for (unsigned row = 0; row < 16; ++row) {
        uint16_t required = UINT16_MAX;
        if (type == SUPPORT_CENTER)
            required = row >= 7 && row < 9 ? UINT16_C(0x0180) : 0;
        else if (type == SUPPORT_RIGID && row >= 2 && row < 14)
            required = UINT16_C(0xc003);
        if ((rows[row] & required) != required) return false;
    }
    return true;
}

bool block_face_sturdy(const World *world, BlockPos position, Direction face)
{
    if ((unsigned)face >= DIR_COUNT) return false;
    BlockState state = world_get(world, position);
    if (state.id == BLOCK_MOVING_PISTON)
        return moving_support(world, position, face, SUPPORT_FULL);
    if (block_is_piston_base((BlockId)state.id) && state.data.piston.extended)
        return face == direction_opposite((Direction)state.data.piston.facing);
    if (block_is_piston_head((BlockId)state.id))
        return face == (Direction)state.data.piston.facing;
    return block_definition((BlockId)state.id)->full_cube;
}

bool block_supports_wire(const World *world, BlockPos position)
{
    return world_get(world, position).id == BLOCK_HOPPER ||
           block_face_sturdy(world, position, DIR_UP);
}

bool block_supports_rigid(const World *world, BlockPos position)
{
    if (world_get(world, position).id == BLOCK_MOVING_PISTON)
        return moving_support(world, position, DIR_UP, SUPPORT_RIGID);
    return block_supports_wire(world, position);
}

bool block_supports_torch(const World *world, BlockPos position, Direction face)
{
    BlockState state = world_get(world, position);
    if (face != DIR_UP) return block_face_sturdy(world, position, face);
    if (state.id == BLOCK_MOVING_PISTON)
        return moving_support(world, position, face, SUPPORT_CENTER);
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
