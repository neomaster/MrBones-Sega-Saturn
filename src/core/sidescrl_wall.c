#include "core/sidescrl_wall.h"

/* True when a solid pixel lies in `col` within the rows [row_lo, row_hi). */
static bool column_blocked(SidescrlWallFn is_solid_wall, void *ctx, int64_t col,
                           int64_t row_lo, int64_t row_hi) {
    int64_t row;

    if (col < INT32_MIN || col > INT32_MAX)
        return true; /* world edge */
    for (row = row_lo; row < row_hi; row++) {
        if (row < INT32_MIN || row > INT32_MAX)
            continue; /* no such pixel */
        if (is_solid_wall(ctx, (int32_t)col, (int32_t)row))
            return true;
    }
    return false;
}

int32_t sidescrl_wall_clamp_dx(const BoundingBox *actor, int32_t desired_dx,
                               int32_t max_climb_step,
                               SidescrlWallFn is_solid_wall, void *ctx) {
    const bool right = desired_dx > 0;
    int64_t want, step, row_lo, row_hi, first_col, free_cols;

    if (actor == NULL || desired_dx == 0)
        return 0;

    want = right ? (int64_t)desired_dx : -(int64_t)desired_dx;
    if (want > SIDESCRL_WALL_MAX_SCAN)
        want = SIDESCRL_WALL_MAX_SCAN;

    if (is_solid_wall == NULL || actor->w <= 0 || actor->h <= 0)
        return (int32_t)(right ? want : -want);
    if (actor->h > SIDESCRL_WALL_MAX_SCAN)
        return 0;

    step = max_climb_step > 0 ? (int64_t)max_climb_step : 0;
    row_lo = actor->y;
    row_hi = (int64_t)actor->y + actor->h - step;
    if (row_hi <= row_lo) /* the whole body is within the climbable rows */
        return (int32_t)(right ? want : -want);

    first_col = right ? (int64_t)actor->x + actor->w : (int64_t)actor->x - 1;
    for (free_cols = 0; free_cols < want; free_cols++) {
        int64_t col = right ? first_col + free_cols : first_col - free_cols;
        if (column_blocked(is_solid_wall, ctx, col, row_lo, row_hi))
            break;
    }
    return (int32_t)(right ? free_cols : -free_cols);
}
