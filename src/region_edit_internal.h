#ifndef MC_REGION_EDIT_INTERNAL_H
#define MC_REGION_EDIT_INTERNAL_H

#include "rs/region_edit.h"

/* Apply a complete source-region snapshot to a destination editor. This is
 * an internal bridge for structure writeback; it is not part of the public
 * editor API. */
RsResult region_editor_transfer(RegionEditor *source, RegionEditor *target,
                                BlockPos first, BlockPos second,
                                BlockPos anchor, bool skip_air,
                                size_t *changed);

#endif
