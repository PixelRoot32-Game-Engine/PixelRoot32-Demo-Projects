/*
 * Visplane bookkeeping of the DOOM port (r_plane.h).
 *
 * This is upstream's R_FindPlane()/R_CheckPlane(), with the arrays moved into
 * the caller and the I_Error() overflow turned into a return code. The
 * algorithms are otherwise line for line: the same key comparison, the same
 * sky collapse, the same intersection scan that decides between widening a
 * plane and splitting a new one.
 *
 * Both arrays are filled with R_PLANE_EMPTY when a plane is allocated. Upstream
 * fills only top[]; filling bottom[] too makes a dumped plane deterministic
 * without changing a pixel, because no consumer reads a column whose top is
 * empty. r_plane.h states the reasoning.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include <stddef.h>

#include "r_plane.h"

void r_plane_clear(r_plane_set_t* set) {
    if (set != NULL) {
        set->count = 0;
    }
}

r_plane_error_t r_plane_find(r_plane_set_t* set, fixed_t height, r_name_t picNum,
                             int16_t lightLevel, r_name_t skyPicNum,
                             r_visplane_t** out) {
    r_visplane_t* check;
    uint16_t index;

    if (set == NULL) {
        return R_PLANE_ERR_NULL_SET;
    }
    if (set->planes == NULL) {
        return R_PLANE_ERR_NULL_PLANES;
    }
    if (out == NULL) {
        return R_PLANE_ERR_NULL_OUT;
    }

    /* All sky flats share one plane, whatever their sector height or light. */
    if (picNum == skyPicNum) {
        height = 0;
        lightLevel = 0;
    }

    for (index = 0; index < set->count; ++index) {
        check = &set->planes[index];
        if (check->height == height && check->picNum == picNum &&
            check->lightLevel == lightLevel) {
            *out = check;
            return R_PLANE_OK;
        }
    }

    if (set->count >= set->capacity) {
        return R_PLANE_ERR_NO_ROOM;
    }

    check = &set->planes[set->count++];
    check->height = height;
    check->picNum = picNum;
    check->lightLevel = lightLevel;
    check->minX = R_SCREEN_WIDTH;
    check->maxX = -1;

    for (index = 0; index < R_SCREEN_WIDTH; ++index) {
        check->top[index] = R_PLANE_EMPTY;
        check->bottom[index] = R_PLANE_EMPTY;
    }

    *out = check;
    return R_PLANE_OK;
}

r_plane_error_t r_plane_check(r_plane_set_t* set, r_visplane_t* plane, int start,
                              int stop, r_visplane_t** out) {
    r_visplane_t* split;
    int intrl;
    int intrh;
    int unionl;
    int unionh;
    int x;

    if (set == NULL) {
        return R_PLANE_ERR_NULL_SET;
    }
    if (set->planes == NULL) {
        return R_PLANE_ERR_NULL_PLANES;
    }
    if (plane == NULL) {
        return R_PLANE_ERR_NULL_PLANE;
    }
    if (out == NULL) {
        return R_PLANE_ERR_NULL_OUT;
    }
    if (start < 0 || stop >= R_SCREEN_WIDTH || start > stop) {
        return R_PLANE_ERR_RANGE;
    }

    if (start < plane->minX) {
        intrl = plane->minX;
        unionl = start;
    } else {
        unionl = plane->minX;
        intrl = start;
    }

    if (stop > plane->maxX) {
        intrh = plane->maxX;
        unionh = stop;
    } else {
        unionh = plane->maxX;
        intrh = stop;
    }

    /* If the intersection of the two ranges holds no recorded column, the run
     * is disjoint from this plane and can share it. */
    for (x = intrl; x <= intrh; ++x) {
        if (plane->top[x] != R_PLANE_EMPTY) {
            break;
        }
    }

    if (x > intrh) {
        plane->minX = (int16_t)unionl;
        plane->maxX = (int16_t)unionh;
        *out = plane;
        return R_PLANE_OK;
    }

    if (set->count >= set->capacity) {
        return R_PLANE_ERR_NO_ROOM;
    }

    split = &set->planes[set->count++];
    split->height = plane->height;
    split->picNum = plane->picNum;
    split->lightLevel = plane->lightLevel;
    split->minX = (int16_t)start;
    split->maxX = (int16_t)stop;

    for (x = 0; x < R_SCREEN_WIDTH; ++x) {
        split->top[x] = R_PLANE_EMPTY;
        split->bottom[x] = R_PLANE_EMPTY;
    }

    *out = split;
    return R_PLANE_OK;
}
