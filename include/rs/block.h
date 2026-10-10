#ifndef RS_BLOCK_H
#define RS_BLOCK_H
#include <stdbool.h>
#include <stdint.h>

typedef struct { int x, y, z;} BlockPos;

typedef enum{
    BLOCK_AIR = 0,
    BLOCK_IRON,
    BLOCK_REDSTONE_DUST,
    BLOCK_REDSTONE_TORCH,
    BLOCK_REDSTONE,
} BlockId;

typedef struct {
    uint8_t id;
    union{
        struct{
            uint8_t power;
            uint8_t connections;
            uint8_t up_connections;
            bool prefer_dot;
        } dust;
        struct{
            uint8_t support_dir;
            bool lit;
        } torch;
    }data;
} BlockState;

#endif
