#pragma once

#include "snooze.h"

typedef enum {
    FTH_CF_PARENT,
    FTH_CF_BORDER,
    FTH_CF_EMPTY,
    FTH_CF_SOLID,
} fth_CellKind;

HMM_Vec3 fth_cellOffsets[] = {
    {.X = 1, .Y = 1, .Z = 1 },
    {.X = 1, .Y = 1, .Z = -1 },
    {.X = 1, .Y = -1, .Z = -1 },
    {.X = 1, .Y = -1, .Z = 1 },

    {.X = -1, .Y = 1, .Z = 1 },
    {.X = -1, .Y = 1, .Z = -1 },
    {.X = -1, .Y = -1, .Z = -1 },
    {.X = -1, .Y = -1, .Z = 1 },
};
#define FTH_CELL_OFFSETS_COUNT 8

#define FTH_REFINE_MAX 10

typedef struct {
    /*
    one cell:
    starts with one byte for kind

    if border, next 2 bytes are a uint16_t angle idx, and a uint8_t offset for the plane
    | xFF | xFF FF | xFF |

    if parent, the inner 8 cells are written in order, entirely recursing on each, order stored in fth_cellOffsets
    | xFF | (inner cell) (inner cell 2), ... |

    if solid or empty, nothing else
    | XFF |
    */
    uint8_t* cells;
    int64_t cellsSize;
    HMM_Vec3 origin;

    // all solids origins should be aligned to the (---) corner of a one width grid.
    // i.e. origins are integer values, cube extends in the +++ direction, widths are always 1 (meters)
} fth_Solid;

// recursive call to generate cells for a one section within a bound of a cube
void _fth_cube(
    int64_t level, int64_t maxRefine,
    HMM_Vec3 boundStart, float boundSideLength,
    HMM_Vec3 cubeOrigin, float cubeHalfWidth,
    snz_Arena* arena) {
    for (int i = 0; i < FTH_CELL_OFFSETS_COUNT; i++) {

    }
}

fth_Solid fth_cube(HMM_Vec3 origin, float halfWidth, int maxRefine, snz_Arena* arena) {
    SNZ_ASSERTF(maxRefine > 0, "Invalid refinement of %d, should be > 0", maxRefine);

    for (int i = 0; i < maxRefine; i++) {

    }
}

void _fth_transform(int64_t start, int refine, HMM_Mat4 transform, const fth_Solid* original, snz_Arena* arena) {
    SNZ_ASSERT(refine < FTH_REFINE_MAX, "solid passed max refine.");
    SNZ_ASSERTF(start < original->cellsSize, "Cell out of bounds. Tried: %lld, max: %lld", start, original->cellsSize);
    uint8_t kind = original->cells[start];
}

fth_Solid fth_transform(HMM_Mat4 transform, const fth_Solid* original, snz_Arena* arena) {

}

fth_Solid fth_halfSpace() {

}

fth_Solid fth_cylinder() {

}

fth_Solid fth_extrude(const fth_Solid* src, HMM_Vec3 normal, float distance) {

}

void fth_solidToRenderable() {

}
