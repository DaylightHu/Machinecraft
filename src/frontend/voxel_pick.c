#include "voxel_pick.h"
#include "hopper_shape.h"

#include <limits.h>
#include <math.h>

static Direction face_entered(int axis, int step)
{
    if (axis == 0) return step > 0 ? DIR_WEST  : DIR_EAST;
    if (axis == 1) return step > 0 ? DIR_DOWN  : DIR_UP;
    return step > 0 ? DIR_NORTH : DIR_SOUTH;
}

static VoxelHit pick_hopper(BlockPos pos, Direction facing,
                            const double origin[3], const double direction[3],
                            double max_distance)
{
    HopperBox boxes[HOPPER_BOX_COUNT];
    hopper_shape(facing, boxes);
    VoxelHit result = {0};
    double nearest = max_distance;
    const double center[] = {pos.x, pos.y, pos.z};
    for (int i = 0; i < HOPPER_BOX_COUNT; ++i) {
        double enter = -INFINITY, leave = INFINITY;
        Direction face = DIR_COUNT;
        bool intersects = true;
        for (int axis = 0; axis < 3; ++axis) {
            double low = center[axis]+boxes[i].minimum[axis]/16.0-0.5;
            double high = center[axis]+boxes[i].maximum[axis]/16.0-0.5;
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
        if (!intersects || leave < enter || leave < 0) continue;
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
    Direction face = DIR_COUNT;

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

            /* If the ray starts exactly on a cell face, that face is known. */
            if (grid == floor(grid) && face == DIR_COUNT)
                face = face_entered(axis, step[axis]);
        }
    }

    double t = 0.0;
    for (;;)
    {
        BlockPos pos = {cell[0], cell[1], cell[2]};
        if (world_in_bounds(world, pos))
        {
            BlockState state = world_get(world, pos);
            if (state.id == BLOCK_HOPPER) {
                VoxelHit hopper = pick_hopper(pos, (Direction)state.data.hopper.facing,
                                              o, d, max_distance);
                if (hopper.hit) return hopper;
            }
            else if (state.id != BLOCK_AIR)
            {
                VoxelHit result = {0};
                result.hit = true;
                result.has_face = face != DIR_COUNT;
                result.pos = pos;
                result.face = face;
                result.distance = (float)t;
                return result;
            }
        }

        double t_next = fmin(next[0], fmin(next[1], next[2]));
        if (!isfinite(t_next) || t_next > (double)max_distance)
            return miss;

        /* At a grid edge/corner, advance all tied axes together.
         * This avoids selecting cells touched at only a zero-area edge. */
        Direction entered = DIR_COUNT;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (fabs(next[axis] - t_next) <= 1e-9)
            {
                if ((step[axis] > 0 && cell[axis] == INT_MAX) ||
                    (step[axis] < 0 && cell[axis] == INT_MIN))
                    return miss;

                cell[axis] += step[axis];
                next[axis] += delta[axis];
                if (entered == DIR_COUNT)
                    entered = face_entered(axis, step[axis]);
            }
        }
        t = t_next;
        face = entered;
    }
}
