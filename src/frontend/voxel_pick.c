#include "voxel_pick.h"
#include "block_shape.h"

#include <limits.h>
#include <math.h>

static Direction face_entered(int axis, int step)
{
    if (axis == 0) return step > 0 ? DIR_WEST  : DIR_EAST;
    if (axis == 1) return step > 0 ? DIR_DOWN  : DIR_UP;
    return step > 0 ? DIR_NORTH : DIR_SOUTH;
}

static VoxelHit pick_shape(BlockPos pos, const BlockShape *shape,
                           const double origin[3], const double direction[3],
                           double max_distance)
{
    VoxelHit result = {.face = DIR_COUNT};
    double nearest = max_distance;
    const double center[] = {pos.x, pos.y, pos.z};
    for (unsigned i = 0; i < shape->count; ++i) {
        double enter = -INFINITY, leave = INFINITY;
        Direction face = DIR_COUNT;
        bool intersects = true;
        for (int axis = 0; axis < 3; ++axis) {
            double low = center[axis]+shape->boxes[i].minimum[axis]/16.0-0.5;
            double high = center[axis]+shape->boxes[i].maximum[axis]/16.0-0.5;
            if (direction[axis] == 0.0) {
                if (origin[axis] < low || origin[axis] > high) intersects = false;
                continue;
            }
            double first = (low-origin[axis])/direction[axis];
            double last = (high-origin[axis])/direction[axis];
            if (first > last) { double temp = first; first = last; last = temp; }
            if (first > enter) { enter = first; face = face_entered(axis, direction[axis] > 0 ? 1 : -1); }
            if (last < leave) leave = last;
        }
        if (!intersects || leave < enter || leave <= 0) continue;
        double distance = enter > 0 ? enter : 0;
        if (distance > nearest) continue;
        nearest = distance;
        result = (VoxelHit){.hit = true, .has_face = enter >= 0 && face != DIR_COUNT,
                            .pos = pos, .face = face, .distance = (float)distance};
    }
    return result;
}

VoxelHit voxel_pick(const World *world, PickVec3 origin,
                    PickVec3 direction, float max_distance)
{
    VoxelHit miss = {0};
    miss.face = DIR_COUNT;
    if (!world || !(max_distance > 0.0f) || !isfinite(max_distance))
        return miss;

    double o[3] = {origin.x, origin.y, origin.z};
    double d[3] = {direction.x, direction.y, direction.z};

    double length = sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (!(length > 0.0) || !isfinite(length)) return miss;

    int cell[3], step[3];
    double next[3], delta[3];

    for (int axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(o[axis])) return miss;
        d[axis] /= length;

        /* Transform block centres at integers into ordinary unit grid. */
        double grid = o[axis] + 0.5;
        double index = floor(grid);
        if (d[axis] < 0.0 && grid == index) index -= 1.0;

        if (index <= (double)INT_MIN + 1.0 ||
            index >= (double)INT_MAX - 1.0) return miss;
        cell[axis] = (int)index;

        step[axis] = (d[axis] > 0.0) - (d[axis] < 0.0);
        if (step[axis] == 0)
        {
            next[axis] = INFINITY;
            delta[axis] = INFINITY;
        }
        else
        {
            double boundary = (double)cell[axis] +
                              (step[axis] > 0 ? 0.5 : -0.5);
            next[axis] = (boundary - o[axis])/d[axis];
            delta[axis] = 1.0/fabs(d[axis]);
        }
    }

    for (;;)
    {
        BlockPos pos = {cell[0], cell[1], cell[2]};
        if (world_in_bounds(world, pos))
        {
            BlockState state = world_get(world, pos);
            if (state.id != BLOCK_AIR) {
                BlockShape shape;
                block_outline_shape(state, &shape);
                VoxelHit hit = pick_shape(pos, &shape, o, d, max_distance);
                if (hit.hit) {
                    block_interaction_shape(state, &shape);
                    if (shape.count) {
                        VoxelHit interaction = pick_shape(pos, &shape, o, d, max_distance);
                        if (interaction.hit && interaction.has_face &&
                            interaction.distance < hit.distance)
                            hit.face = interaction.face;
                    }
                    return hit;
                }
            }
        }

        double t_next = fmin(next[0], fmin(next[1], next[2]));
        if (!isfinite(t_next) || t_next > (double)max_distance)
            return miss;

        /* At a grid edge/corner, advance all tied axes together.
         * This avoids selecting cells touched at only a zero-area edge. */
        for (int axis = 0; axis < 3; ++axis)
        {
            if (fabs(next[axis] - t_next) <= 1e-9)
            {
                if ((step[axis] > 0 && cell[axis] == INT_MAX) ||
                    (step[axis] < 0 && cell[axis] == INT_MIN))
                    return miss;

                cell[axis] += step[axis];
                next[axis] += delta[axis];
            }
        }
    }
}
