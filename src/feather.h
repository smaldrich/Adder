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

int fth_octantToCellIdx(bool x, bool y, bool z) {
    return (x << 2) + (y << 1) + z;
}

typedef enum {
    FTH_CK_PARENT,
    FTH_CK_BORDER,
    FTH_CK_SOLID,
    FTH_CK_EMPTY,
} fth_CellKind;

typedef struct {
    // represent percent offsets within the cell to the point on the surface
    uint16_t x;
    uint16_t y;
    uint16_t z;
} fth_CellBorder;

// point expected to be relative to bound origin
fth_CellBorder fth_pointToCellBorder(HMM_Vec3 point, float cellSize) {
    HMM_Vec3 pct = HMM_DivV3F(point, cellSize);
    fth_CellBorder out = (fth_CellBorder){
        (int16_t)(pct.X * UINT16_MAX),
        (int16_t)(pct.Y * UINT16_MAX),
        (int16_t)(pct.Z * UINT16_MAX),
    };
    return out;
}

HMM_Vec3 fth_cellBorderToPoint(fth_CellBorder cell, HMM_Vec3 boundOrigin, float boundSize) {
    HMM_Vec3 pt = HMM_V3(cell.x, cell.y, cell.z);
    pt = HMM_DivV3F(pt, UINT16_MAX);
    pt = HMM_MulV3F(pt, boundSize);
    pt = HMM_Add(pt, boundOrigin);
    return pt;
}

typedef struct {
    union {
        fth_Cell* ptr;
        fth_CellBorder border;
    } inners[FTH_CELL_OFFSETS_COUNT];
    int16_t innerKinds;
    // wasting a lot of bytes if many outer/inner cells, but it makes lookups faster so who knows
    // FIXME: profile regular vs. irregular setup
} fth_Cell;

HMM_Vec3 _fth_sampleSphere(HMM_Vec3 pos, float radius, bool* outWithin) {
    HMM_Vec3 out = pos;
    out = HMM_Mul(HMM_Norm(out), radius);
    *outWithin = HMM_Len(pos) < radius;
    return out;
}

void _fth_sphereToSolidRecurse(fth_Cell* parent, snz_Arena* arena, float radius, HMM_Vec3 cellOrigin, int maxSubdivs, int subdivision) {
    float childCellSize = powf(0.5, subdivision);
    for (int i = FTH_CELL_OFFSETS_COUNT - 1; i >= 0; i--) {
        HMM_Vec3 childOrigin = HMM_Add(cellOrigin, HMM_MulV3F(fth_cellOffsets[i], childCellSize));
        float halfSize = childCellSize / 2;
        HMM_Vec3 childCenter = HMM_Add(childOrigin, HMM_V3(halfSize, halfSize, halfSize));
        bool within = false;
        HMM_Vec3 surface = HMM_Sub(_fth_sampleSphere(childCenter, radius, &within), childOrigin);

        bool outOfCell = surface.X > childCellSize || surface.X < 0;
        outOfCell |= surface.Y > childCellSize || surface.Y < 0;
        outOfCell |= surface.Z > childCellSize || surface.Z < 0;
        fth_CellKind kind = within ? FTH_CK_SOLID : FTH_CK_EMPTY;
        if (outOfCell) { // outside of cell
            // << default case for kind
        } else if (subdivision < maxSubdivs) { // if we still should subdivide, do that
            kind = FTH_CK_PARENT;
            fth_Cell* child = SNZ_ARENA_PUSH(arena, fth_Cell);
            parent->inners[i].ptr = child;
            _fth_sphereToSolidRecurse(child, arena, radius, childOrigin, maxSubdivs, subdivision + 1);
        } else {
            kind = FTH_CK_BORDER;
            parent->inners[i].border = fth_pointToCellBorder(surface, childCellSize);
        }
        parent->innerKinds = (parent->innerKinds << 2) | (0b11 & kind);
    }
}

const fth_Cell* fth_sphereToSolid(snz_Arena* arena, float radius, int subdivCount) {
    fth_Cell* cell = SNZ_ARENA_PUSH(arena, fth_Cell);
    _fth_sphereToSolidRecurse(cell, arena, radius, HMM_V3(0, 0, 0), 7, 1);
    return cell;
}

fth_CellKind fth_solidGetCell(const fth_Cell* solid, HMM_Vec3 pos, fth_CellBorder* outBorder) {
    fth_Cell* cell = solid;
    HMM_Vec3 cellOrigin = HMM_V3(0, 0, 0);
    float cellSize = 1;

    while (true) { // FIXME: cutoff
        cellSize /= 2;
        HMM_Vec3 center = HMM_Add(cellOrigin, HMM_V3(cellSize, cellSize, cellSize));
        HMM_Vec3 diff = HMM_Sub(pos, center);
        int childIdx = fth_octantToCellIdx(pos.X > 0, pos.Y > 0, pos.Z > 0); // FIXME: how does floating point imprecision interact with border samples????

        cellOrigin = HMM_Add(cellOrigin, HMM_MulV3(HMM_V3(cellSize, cellSize, cellSize), fth_cellOffsets[childIdx]));

        fth_CellKind innerKind = (cell->innerKinds >> (2 * childIdx)) & 0b11;
        if (innerKind == FTH_CK_PARENT) {
            cell = cell->inners[childIdx].ptr;
            continue;
        }

        if (innerKind == FTH_CK_BORDER) {
            *outBorder = cell->inners[childIdx].border;
        }
        return innerKind;
    }
}

ren3d_Mesh fth_solidToRenderable(fth_Cell* solid, snz_Arena* scratch) {

}