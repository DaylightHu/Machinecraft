#include "mcmux.h"
#include "region_edit_internal.h"
#include "save_file.h"

#include <stdio.h>
#include <stdlib.h>

static GameSessionResult structure_writeback_result(StructureResult result)
{
    switch (result) {
    case STRUCTURE_INVALID_ARGUMENT:
    case STRUCTURE_OUT_OF_BOUNDS:
    case STRUCTURE_TOO_LARGE:
        return GAME_SESSION_INVALID_ARGUMENT;
    case STRUCTURE_OUT_OF_MEMORY:
        return GAME_SESSION_OUT_OF_MEMORY;
    case STRUCTURE_IO_ERROR:
        return GAME_SESSION_IO_ERROR;
    case STRUCTURE_CORRUPT:
        return GAME_SESSION_CORRUPT_SAVE;
    case STRUCTURE_UNSUPPORTED:
        return GAME_SESSION_UNSUPPORTED_SAVE;
    case STRUCTURE_SIMULATION_ERROR:
        return GAME_SESSION_SIMULATION_ERROR;
    case STRUCTURE_OK:
    case STRUCTURE_EXISTS:
    default:
        return GAME_SESSION_INVALID_ARGUMENT;
    }
}

static GameSessionResult region_writeback_result(RsResult result)
{
    switch (result) {
    case RS_OK:
    case RS_UNCHANGED:
        return GAME_SESSION_OK;
    case RS_OUT_OF_MEMORY:
        return GAME_SESSION_OUT_OF_MEMORY;
    case RS_OUT_OF_BOUNDS:
    case RS_INVALID_ARGUMENT:
        return GAME_SESSION_INVALID_ARGUMENT;
    case RS_UNSUPPORTED:
    case RS_INTERNAL_ERROR:
    default:
        return GAME_SESSION_SIMULATION_ERROR;
    }
}

World *load_mcs(const char *path)
{
    StructureResult result;
    World *world = structure_load(path, &result);
    if (!world) {
        fprintf(stderr, "Error loading '%s': %s\n",
                path ? path : "(null)", structure_result_name(result));
        exit(EXIT_FAILURE);
    }
    printf("%s loaded. \n", path);
    return world;
}

GameSessionResult save_mcw(World *world, const char *path)
{
    if (!world || !path || !*path)
        return GAME_SESSION_INVALID_ARGUMENT;

    if (sim_flush(world) != RS_OK)
        return GAME_SESSION_SIMULATION_ERROR;

    return save_file_write(world, path);
}

GameSessionResult writeback_mcs(World *world, const char *mcs_path,
                                const char *mcw_path)
{
    if (!world || !mcs_path || !*mcs_path || !mcw_path || !*mcw_path)
        return GAME_SESSION_INVALID_ARGUMENT;

    StructureResult structure_result;
    Structure *structure = structure_read(mcs_path, &structure_result);
    if (!structure) return structure_writeback_result(structure_result);
    StructureInfo info = structure_info(structure);
    structure_destroy(structure);

    GameSessionResult save_result;
    World *target = save_file_read(mcw_path, &save_result);
    if (!target) return save_result;

    RegionEditor *source_editor = region_editor_create(world);
    RegionEditor *target_editor = region_editor_create(target);
    if (!source_editor || !target_editor) {
        region_editor_destroy(source_editor);
        region_editor_destroy(target_editor);
        world_destroy(target);
        return GAME_SESSION_OUT_OF_MEMORY;
    }

    BlockPos first = info.origin;
    BlockPos second = {
        info.origin.x + (int)info.width - 1,
        info.origin.y + (int)info.height - 1,
        info.origin.z + (int)info.length - 1
    };
    RsResult result = region_editor_transfer(source_editor, target_editor,
                                             first, second, first, false,
                                             NULL);

    region_editor_destroy(source_editor);
    region_editor_destroy(target_editor);
    if (result != RS_OK && result != RS_UNCHANGED) {
        world_destroy(target);
        return region_writeback_result(result);
    }

    save_result = save_mcw(target, mcw_path);
    world_destroy(target);
    return save_result;
}
