#pragma once

#include "snooze.h"
#include "render3d.h"

#define FTH_CELL_OFFSETS_COUNT 8

HMM_Vec3 fth_cellOffsets[FTH_CELL_OFFSETS_COUNT] = {
    {.X = 0, .Y = 0, .Z = 0 },
    {.X = 0, .Y = 0, .Z = 1 },
    {.X = 0, .Y = 1, .Z = 0 },
    {.X = 0, .Y = 1, .Z = 1 },

    {.X = 1, .Y = 0, .Z = 0 },
    {.X = 1, .Y = 0, .Z = 1 },
    {.X = 1, .Y = 1, .Z = 0 },
    {.X = 1, .Y = 1, .Z = 1 },
};

int fth_quadrantToCellOffsetIdx(bool x, bool y, bool z) {
    return (x << 2) + (y << 1) + z;
}

typedef enum {
    FTH_CK_PARENT,
    FTH_CK_BORDER,
    FTH_CK_EMPTY,
} fth_CellKind;

typedef struct {
    HMM_Vec3 origin;
    float size;
} fth_CellBound;

typedef struct {
    // represent percent offsets within the cell to the point on the surface
    uint16_t x;
    uint16_t y;
    uint16_t z;
} fth_CellBorder;

fth_CellBorder fth_pointToCellBorder(HMM_Vec3 point, fth_CellBound bound) {
    HMM_Vec3 diff = HMM_Sub(point, bound.origin);
    HMM_Vec3 pct = HMM_DivV3F(point, bound.size);

    fth_CellBorder out = (fth_CellBorder){
        (int16_t)(pct.X * UINT16_MAX),
        (int16_t)(pct.Y * UINT16_MAX),
        (int16_t)(pct.Z * UINT16_MAX),
    };
    return out;
}

HMM_Vec3 fth_cellBorderToPoint(fth_CellBorder cell, fth_CellBound bound) {
    HMM_Vec3 pt = HMM_V3(cell.x, cell.y, cell.z);
    pt = HMM_DivV3F(pt, UINT16_MAX);
    pt = HMM_MulV3F(pt, bound.size);
    pt = HMM_Add(pt, bound.origin);
    return pt;
}

typedef struct {
    union {
        fth_Cell* ptr;
        fth_CellBorder offset;
    } inners[FTH_CELL_OFFSETS_COUNT];
    int16_t innerKinds;
    // wasting a lot of bytes if many outer/inner cells, but it makes lookups faster so who knows
    // FIXME: profile regular vs. irregular setup
} fth_Cell;

HMM_Vec3 _fth_sampleSphere(HMM_Vec3 pos, float radius) {
    HMM_Vec3 out = pos;
    out = HMM_Mul(HMM_Norm(out), radius);
    return out;
}

void _fth_sphereToSolidRecurse(fth_Cell* parent, snz_Arena* arena, float radius, HMM_Vec3 cellOrigin, int maxSubdivs, int subdivision) {
    float childCellSize = powf(0.5, subdivision);
    for (int i = FTH_CELL_OFFSETS_COUNT - 1; i >= 0; i--) {
        HMM_Vec3 childOrigin = HMM_Add(cellOrigin, HMM_MulV3F(fth_cellOffsets[i], childCellSize));
        float halfSize = childCellSize / 2;
        HMM_Vec3 childCenter = HMM_Add(childOrigin, HMM_V3(halfSize, halfSize, halfSize));
        HMM_Vec3 surface = HMM_Sub(_fth_sampleSphere(childCenter, radius), childOrigin);

        bool outOfCell = surface.X > childCellSize || surface.X < 0;
        outOfCell |= surface.Y > childCellSize || surface.Y < 0;
        outOfCell |= surface.Z > childCellSize || surface.Z < 0;
        fth_CellKind kind = FTH_CK_EMPTY;
        if (outOfCell) { // outside of cell
            // << default case for kind
        } else if (subdivision < maxSubdivs) { // if we still should subdivide, do that
            kind = FTH_CK_PARENT;
            fth_Cell* child = SNZ_ARENA_PUSH(arena, fth_Cell);
            parent->inners[i].ptr = child;
            _fth_sphereToSolidRecurse(child, arena, radius, childOrigin, maxSubdivs, subdivision + 1);
        } else {
            kind = FTH_CK_BORDER;
        }
        parent->innerKinds = (parent->innerKinds << 2) | (0b11 & kind);
    }
}

const fth_Cell* fth_sphereToSolid(snz_Arena* arena, float radius, int subdivCount) {
    fth_Cell* cell = SNZ_ARENA_PUSH(arena, fth_Cell);
    _fth_sphereToSolidRecurse(cell, arena, radius, HMM_V3(0, 0, 0), 7, 1);
    return cell;
}

void _fth_cellToRenderable(const fth_Cell* cell, snz_Arena* scratch) {

}

ren3d_Mesh fth_solidToRenderable(fth_Cell* solid, snz_Arena* scratch) {

}