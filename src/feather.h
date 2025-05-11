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
    float firstCellSize; // full side lengths off the parent cell
} fth_Solid;

fth_Solid fth_cube(HMM_Vec3 origin, float halfWidth, int refinement, snz_Arena* arena) {
    SNZ_ASSERTF(refinement > 0, "Invalid refinement of %d, should be > 0", refinement);

    for (int i = 0; i < refinement; i++) {

    }
}

fth_Solid fth_halfSpace() {

}

fth_Solid fth_cylinder() {

}

fth_Solid fth_extrude(const fth_Solid* src, HMM_Vec3 normal, float distance) {

}

void fth_solidToRenderable() {

}