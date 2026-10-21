#ifndef RS_CONTAINER_H
#define RS_CONTAINER_H
#include "rs/world.h"
#include "posmap.h"

typedef struct {
    BlockPos pos;
    ContainerStack *slots;
    uint64_t cooldown_until, last_tick;
    uint8_t id, facing;
    bool enabled;
} ContainerRuntime;
typedef struct {
    PosMap positions;
    ContainerRuntime *items; /* In block-entity insertion order. */
    size_t count, capacity;
} ContainerTable;

bool container_is_block(BlockId id);
ContainerRuntime *container_runtime(const World *world, BlockPos pos);
bool container_attach(World *world, BlockPos pos, BlockState state);
void container_forget(World *world, BlockPos pos);
void container_destroy(World *world);
void container_sync_hopper(World *world, BlockPos pos, BlockState state);
void container_step(World *world);
uint8_t container_level(const ContainerRuntime *container);
bool container_stack_valid(ContainerStack stack);
bool container_chunk_pinned(const World *world, BlockPos chunk);
RsResult container_set_barrel_level(World *world, BlockPos pos, uint8_t level);
#endif
