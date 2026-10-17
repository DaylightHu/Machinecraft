#ifndef RS_BLOCK_INTERNAL_H
#define RS_BLOCK_INTERNAL_H
#include "world_internal.h"
typedef enum { SIGNAL_ALL, SIGNAL_WITHOUT_WIRE } SignalMode;
typedef struct { BlockState state; bool changed; uint32_t next_delay; } TickAction;
typedef uint8_t (*SignalFn)(const World *, BlockPos, Direction, SignalMode);
typedef void (*NeighborFn)(World *, BlockPos);
typedef TickAction (*TickFn)(World *, BlockPos);
typedef struct {
    const char *name;
    bool full_cube, conductor, supports_wire;
    SignalFn signal, direct_signal;
    NeighborFn neighbor_changed;
    NeighborFn input_changed; /* Called after the zero-delay network settles. */
    TickFn scheduled_tick;
} BlockDef;
const BlockDef *block_definition(BlockId id);
extern const BlockDef rs_iron_def, rs_redstone_block_def, rs_wire_def, rs_torch_def,
                      rs_glass_def;
uint8_t power_signal(const World *, BlockPos, Direction, SignalMode);
uint8_t power_direct(const World *, BlockPos, Direction, SignalMode);
uint8_t power_received(const World *, BlockPos, SignalMode);
uint8_t power_conducted(const World *, BlockPos, SignalMode);
bool block_is_conductor(const World *, BlockPos);
bool block_supports_wire(const World *, BlockPos);
bool block_supports_torch(const World *, BlockPos);
bool wire_supported(const World *, BlockPos);
bool torch_supported(const World *, BlockPos, Direction);
size_t wire_neighbors(const World *, BlockPos, BlockPos out[12]);
uint8_t wire_external_power(const World *, BlockPos);
void wire_refresh_shape(World *, BlockPos);
void torch_check_input(World *, BlockPos);
#endif
