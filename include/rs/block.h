#ifndef RS_BLOCK_H
#define RS_BLOCK_H
#include <stdbool.h>
#include <stdint.h>

typedef struct { int x, y, z; } BlockPos;
typedef enum {
    BLOCK_AIR = 0, /* Empty storage cell, never a placeable block. */
    BLOCK_IRON, BLOCK_REDSTONE_BLOCK, BLOCK_REDSTONE_WIRE, BLOCK_REDSTONE_TORCH,
    BLOCK_GLASS, BLOCK_REDSTONE_REPEATER, BLOCK_REDSTONE_COMPARATOR,
    BLOCK_POWERED_RAIL, BLOCK_ACTIVATOR_RAIL,
    BLOCK_COUNT
} BlockId;

/* All faces are FROM the emitting block TOWARD the receiving block.
 * Torch support_dir is FROM the torch TOWARD its supporting block. */
typedef enum {
    DIR_EAST = 0, DIR_WEST, DIR_UP, DIR_DOWN, DIR_SOUTH, DIR_NORTH, DIR_COUNT
} Direction;

typedef struct {
    uint8_t id; /* BlockId: C enums themselves need not occupy one byte. */
    union {
        struct {
            uint8_t power;           /* Derived 0..15, simulation writes it. */
            uint8_t connections;     /* Direction bits; horizontal only. */
            uint8_t up_connections;  /* Subset of connections. */
            bool prefer_dot;         /* User choice for an isolated wire. */
        } wire;
        struct {
            uint8_t support_dir;     /* DIR_DOWN or a horizontal face. */
            bool lit;
        } torch;
        struct {
            uint8_t facing;         /* Horizontal output direction. */
            uint8_t delay;          /* 1..4 redstone ticks (2..8 game ticks). */
            bool powered;
            bool locked;
        } repeater;
        struct {
            uint8_t facing;         /* Horizontal output direction. */
            uint8_t output;         /* Stored analog output, 0..15. */
            bool subtract;          /* false: compare, true: subtract. */
        } comparator;
        struct {
            uint8_t facing;         /* Texture top; uphill direction on slopes. */
            bool ascending;
            bool powered;
        } rail;
    } data;
} BlockState;

BlockState block_default(BlockId id);
const char *block_name(BlockId id);
BlockPos block_offset(BlockPos pos, Direction direction);
Direction direction_opposite(Direction direction);
bool direction_horizontal(Direction direction);
#endif
