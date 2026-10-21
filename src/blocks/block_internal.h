#ifndef RS_BLOCK_INTERNAL_H
#define RS_BLOCK_INTERNAL_H
#include "world_internal.h"
typedef enum { SIGNAL_ALL, SIGNAL_WITHOUT_WIRE } SignalMode;
typedef enum {
    PUSH_BLOCK = 0,
    PUSH_NORMAL,
    PUSH_DESTROY,
    PUSH_IGNORE,
    PUSH_ONLY
} PushReaction;
typedef struct { BlockState state; bool changed; uint32_t next_delay; } TickAction;
typedef uint8_t (*SignalFn)(const World *, BlockPos, Direction, SignalMode);
typedef uint8_t (*AnalogFn)(const World *, BlockPos);
typedef void (*NeighborFn)(World *, BlockPos);
typedef void (*ObservedFn)(World *, BlockPos, BlockPos);
typedef TickAction (*TickFn)(World *, BlockPos);
typedef struct {
    const char *name;
    bool full_cube, conductor, supports_wire, signal_source, diode;
    PushReaction push_reaction;
    SignalFn signal, direct_signal;
    AnalogFn analog_output;
    NeighborFn neighbor_changed;
    ObservedFn observed_changed; /* A cell changed directly in the watched neighborhood. */
    NeighborFn input_changed; /* Called after the zero-delay network settles. */
    TickFn scheduled_tick;
} BlockDef;
const BlockDef *block_definition(BlockId id);
extern const BlockDef rs_iron_def, rs_redstone_block_def, rs_wire_def, rs_torch_def,
                      rs_glass_def, rs_repeater_def, rs_comparator_def,
                      rs_powered_rail_def, rs_activator_rail_def,
                      rs_observer_def, rs_piston_def, rs_sticky_piston_def,
                      rs_piston_head_def, rs_sticky_piston_head_def,
                      rs_barrel_def, rs_slime_def, rs_note_block_def,
                      rs_honey_def, rs_target_def, rs_hopper_def, rs_dropper_def;
uint8_t power_signal(const World *, BlockPos, Direction, SignalMode);
uint8_t power_direct(const World *, BlockPos, Direction, SignalMode);
uint8_t power_received(const World *, BlockPos, SignalMode);
uint8_t power_conducted(const World *, BlockPos, SignalMode);
bool block_is_conductor(const World *, BlockPos);
bool block_supports_wire(const World *, BlockPos);
bool block_supports_torch(const World *, BlockPos);
bool block_is_diode(const World *, BlockPos);
bool block_is_signal_source(const World *, BlockPos);
bool block_has_analog_output(const World *, BlockPos);
uint8_t block_analog_output(const World *, BlockPos);
bool wire_supported(const World *, BlockPos);
bool torch_supported(const World *, BlockPos, Direction);
bool repeater_supported(const World *, BlockPos);
bool comparator_supported(const World *, BlockPos);
bool rail_supported(const World *, BlockPos);
bool block_is_rail(BlockId);
size_t rail_neighbors(const World *, BlockPos, bool same_type, BlockPos out[6]);
void rail_refresh_shape(World *, BlockPos);
size_t wire_neighbors(const World *, BlockPos, BlockPos out[12]);
uint8_t wire_external_power(const World *, BlockPos);
void wire_refresh_shape(World *, BlockPos);
void torch_check_input(World *, BlockPos);
void repeater_check_input(World *, BlockPos);
void comparator_check_input(World *, BlockPos);
void rail_check_input(World *, BlockPos);
void piston_check_input(World *, BlockPos);
#endif
