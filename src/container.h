#ifndef RS_CONTAINER_H
#define RS_CONTAINER_H
#include "rs/world.h"
#include "posmap.h"

typedef struct {
    BlockPos pos;
    ContainerStack *slots;
    uint64_t cooldown_until, last_tick;
    size_t tick_slot;
    uint8_t id, facing;
    bool enabled;
} ContainerRuntime;
typedef struct {
    PosMap positions;
    ContainerRuntime *items;
    size_t count, capacity;
    uint64_t dropper_random;
} ContainerTable;

bool container_is_block(BlockId id);
size_t container_slot_count(BlockId id);
ContainerRuntime *container_runtime(const World *world, BlockPos pos);
bool container_attach(World *world, BlockPos pos, BlockState state);
void container_forget(World *world, BlockPos pos);
void container_destroy(World *world);
void container_sync_hopper(World *world, BlockPos pos, BlockState state);
void container_tick_hopper(World *world, BlockPos pos);
void container_dispense(World *world, BlockPos pos);
uint8_t container_level(const ContainerRuntime *container);
bool container_stack_valid(ContainerStack stack);
bool container_chunk_pinned(const World *world, BlockPos chunk);
RsResult container_set_barrel_level(World *world, BlockPos pos, uint8_t level);
#endif
