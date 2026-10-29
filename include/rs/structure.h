#ifndef RS_STRUCTURE_H
#define RS_STRUCTURE_H

#include "world.h"

typedef struct Structure Structure;
typedef enum {
    STRUCTURE_OK = 0, STRUCTURE_INVALID_ARGUMENT, STRUCTURE_OUT_OF_BOUNDS,
    STRUCTURE_TOO_LARGE, STRUCTURE_OUT_OF_MEMORY, STRUCTURE_IO_ERROR,
    STRUCTURE_EXISTS, STRUCTURE_CORRUPT, STRUCTURE_UNSUPPORTED,
    STRUCTURE_SIMULATION_ERROR
} StructureResult;
enum { STRUCTURE_VERSION = 1, STRUCTURE_BLOCK_LIMIT = 4*1024*1024 };
typedef struct {
    BlockPos origin, anchor; /* Absolute source coordinates; anchor may be outside. */
    uint32_t width, height, length;
    uint64_t source_tick; /* Informational; this file is not a running checkpoint. */
    size_t volume, container_count;
} StructureInfo;

Structure *structure_capture(const World *world, BlockPos first, BlockPos second,
                             const BlockPos *anchor, StructureResult *result);
void structure_destroy(Structure *structure);
StructureInfo structure_info(const Structure *structure);
/* Local coordinates: [0,width), [0,height), [0,length). False on bad arguments. */
bool structure_get(const Structure *structure, BlockPos local, BlockState *state);
size_t structure_container_size(const Structure *structure, BlockPos local);
StructureResult structure_get_container_slot(const Structure *structure,
    BlockPos local, size_t slot, ContainerStack *stack);

StructureResult structure_write(const Structure *structure, const char *path,
                                 bool overwrite);
Structure *structure_read(const char *path, StructureResult *result);
const char *structure_result_name(StructureResult result);
World *structure_load(const char *path, StructureResult *result);
/* Load with the saved anchor placed at anchor. A NULL anchor preserves the
 * file's original coordinates. */
World *structure_load_at(const char *path, const BlockPos *anchor,
                         StructureResult *result);
#endif
