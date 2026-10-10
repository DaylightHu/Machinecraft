#include "rs/world.h"

#include <stdio.h>
#include <stdlib.h>

/* A candidate signal from one neighbor, for this straight-line example. */
static int calculate_power(BlockState neighbor)
{
    if (neighbor.id == BLOCK_REDSTONE) {
        return 15;
    }
    if (neighbor.id == BLOCK_REDSTONE_DUST && neighbor.data.dust.power > 0) {
        return neighbor.data.dust.power - 1;
    }
    return 0;
}

/* The source is west of the line. Update from west to east. */
static bool update_line(World *world, BlockPos first, int count)
{
    BlockPos position = first;
    for (int i = 0; i < count; ++i) {
        BlockState dust = world_get(world, position);
        if (dust.id == BLOCK_REDSTONE_DUST) {
            BlockPos neighbor_position = block_offset(position, DIR_WEST);
            BlockState neighbor = world_get(world, neighbor_position);
            dust.data.dust.power = (uint8_t)calculate_power(neighbor);
            if (!world_set(world, position, dust)) {
                return false;
            }
        }
        position = block_offset(position, DIR_EAST);
    }
    return true;
}

static void print_line(const World *world, const char *label,
                       BlockPos first, int count)
{
    printf("%s:", label);
    BlockPos position = first;
    for (int i = 0; i < count; ++i) {
        BlockState dust = world_get(world, position);
        printf(" %u", (unsigned)dust.data.dust.power);
        position = block_offset(position, DIR_EAST);
    }
    putchar('\n');
}

int main(void)
{
    enum { DUST_COUNT = 5 };
    const BlockPos source_position = {1, 1, 2};
    const BlockPos first_dust = {2, 1, 2};
    World *world = world_create(world_config_default());
    if (!world) {
        fputs("Cannot create world.\n", stderr);
        return EXIT_FAILURE;
    }
    int result = EXIT_FAILURE;

    BlockPos position = first_dust;
    for (int i = 0; i < DUST_COUNT; ++i) {
        if (!world_set(world, position, block_default(BLOCK_REDSTONE_DUST))) {
            goto done;
        }
        position = block_offset(position, DIR_EAST);
    }
    print_line(world, "Before", first_dust, DUST_COUNT);

    if (!world_set(world, source_position, block_default(BLOCK_REDSTONE)) ||
        !update_line(world, first_dust, DUST_COUNT)) {
        goto done;
    }
    print_line(world, "Powered", first_dust, DUST_COUNT);

    if (!world_set(world, source_position, block_default(BLOCK_AIR)) ||
        !update_line(world, first_dust, DUST_COUNT)) {
        goto done;
    }
    print_line(world, "Removed", first_dust, DUST_COUNT);
    result = EXIT_SUCCESS;

done:
    if (result != EXIT_SUCCESS) {
        fputs("Cannot update the example circuit.\n", stderr);
    }
    world_destroy(world);
    return result;
}

