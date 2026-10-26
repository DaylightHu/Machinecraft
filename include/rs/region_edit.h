#ifndef RS_REGION_EDIT_H
#define RS_REGION_EDIT_H

#include "world.h"

typedef struct RegionEditor RegionEditor;
typedef struct { BlockPos min, max; size_t volume; } RegionBounds;

/* Per-session selection and clipboard; neither is written into world saves.
 * Edits retain block configuration and container contents, not source timers.
 * Overlapping moves read a complete snapshot before changing any cells. */
RegionEditor *region_editor_create(World *world);
void region_editor_destroy(RegionEditor *editor);
RsResult region_editor_select(RegionEditor *editor, unsigned point, BlockPos pos);
unsigned region_editor_selection(const RegionEditor *editor, BlockPos *first,
                                  BlockPos *second);
bool region_editor_bounds(const RegionEditor *editor, RegionBounds *bounds);
/* A NULL copy anchor uses the minimum corner. Paste keeps this anchor offset. */
RsResult region_editor_copy(RegionEditor *editor, const BlockPos *anchor);
RsResult region_editor_paste(RegionEditor *editor, BlockPos anchor, bool skip_air,
                             size_t *changed);
RsResult region_editor_stack(RegionEditor *editor, uint32_t copies,
                             Direction direction, size_t *changed);
RsResult region_editor_move(RegionEditor *editor, uint32_t distance,
                            Direction direction, size_t *changed);
const char *region_editor_error(const RegionEditor *editor);

#endif
