#pragma once

#include "snooze.h"
#include "render3d.h"
#include "ui.h"

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

typedef struct fth_Cell fth_Cell;
struct fth_Cell {
    union {
        fth_Cell* ptr;
        fth_CellBorder border;
    } inners[FTH_CELL_OFFSETS_COUNT];
    int16_t innerKinds;
    // wasting a lot of bytes if many outer/inner cells, but it makes lookups faster so who knows
    // FIXME: profile regular vs. irregular setup
};

fth_CellKind fth_cellGetInnerKind(const fth_Cell* cell, int idx) {
    return (cell->innerKinds >> (2 * idx)) & 0x3; // 0b11, masks off last two bits
}

HMM_Mat4 _fth_sphereTransform = { 0 };
HMM_Vec3 _fth_sampleSphere(HMM_Vec3 pos, float radius, bool* outWithin) {
    HMM_Vec4 pos4 = HMM_V4(pos.X, pos.Y, pos.Z, 1);
    HMM_Vec3 transformed = HMM_Mul(HMM_InvGeneral(_fth_sphereTransform), pos4).XYZ;

    HMM_Vec3 out = transformed;
    out = HMM_Mul(HMM_Norm(out), radius);
    *outWithin = HMM_Len(transformed) < radius;

    HMM_Vec4 out4 = HMM_V4(out.X, out.Y, out.Z, 1);
    return HMM_Mul(_fth_sphereTransform, out4).XYZ;
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
        // 0x3 is 0b11, masks off last two bits of kind
        parent->innerKinds = (parent->innerKinds << 2) | (0x3 & kind);
    }
}

const fth_Cell* fth_sphereToSolid(snz_Arena* arena, float radius, int subdivCount) {
    fth_Cell* cell = SNZ_ARENA_PUSH(arena, fth_Cell);
    _fth_sphereToSolidRecurse(cell, arena, radius, HMM_V3(0, 0, 0), subdivCount, 1);
    return cell;
}

// where xPath, yPath, zPath are bitstrings that represent the direction to go on each axis of the octree per level.
// i.e. 0100 means left right left left, where the least sig. bit is the one at the base of the tree.
fth_CellKind fth_solidGetCellByPath(const fth_Cell* solid, int targetDepth, uint32_t xPath, uint32_t yPath, uint32_t zPath, fth_CellBorder* outBorder) {
    SNZ_ASSERT(targetDepth <= 32, "why do you have more than 32 subdivisions");
    const fth_Cell* cell = solid;
    int depth = 1;
    while (depth <= targetDepth) {
        bool x = xPath & 1;
        bool y = yPath & 1;
        bool z = zPath & 1;
        int childIdx = fth_octantToCellIdx(x, y, z);
        fth_CellKind kind = fth_cellGetInnerKind(cell, childIdx);

        if (kind != FTH_CK_PARENT) {
            if (kind == FTH_CK_BORDER) {
                *outBorder = cell->inners[childIdx].border;
                SNZ_ASSERTF(depth == targetDepth, "Expected depth for border nodes was %d, ended at %d instead.", targetDepth, depth);
            }
            return kind;
        }

        cell = cell->inners[childIdx].ptr;
        depth++;
        xPath >>= 1;
        yPath >>= 1;
        zPath >>= 1;
    }
    SNZ_ASSERTF(false, "Went past target subdivision depth of %d", targetDepth);
    return false;
}

void fth_solidDrawAsBillboards(const fth_Cell* cell, HMM_Vec3 boundOrigin, float boundSize, HMM_Mat4 vp, HMM_Vec2 screenSize, snz_Arena* scratch) {
    float innerSize = boundSize / 2;
    for (int i = 0; i < FTH_CELL_OFFSETS_COUNT; i++) {
        HMM_Vec3 innerOrigin = HMM_Add(boundOrigin, HMM_Mul(HMM_V3(innerSize, innerSize, innerSize), fth_cellOffsets[i]));
        // HMM_Vec3 pts[4] = {
        //     innerOrigin,
        //     HMM_Add(innerOrigin, HMM_V3(innerSize / 2, 0, 0)),
        //     HMM_Add(innerOrigin, HMM_V3(0, innerSize / 2, 0)),
        //     HMM_Add(innerOrigin, HMM_V3(0, 0, innerSize / 2)),
        // };
        // for (int i = 0; i < 3; i++) {
        //     HMM_Vec4 drawPts[2] = { 0 };
        //     drawPts[0].XYZ = pts[0];
        //     drawPts[1].XYZ = pts[i + 1];
        //     snzr_drawLine(drawPts, 2, ui_colorText, 4, vp);
        // }
        fth_CellKind kind = fth_cellGetInnerKind(cell, i);
        if (kind == FTH_CK_PARENT) {
            fth_solidDrawAsBillboards(cell->inners[i].ptr, innerOrigin, innerSize, vp, screenSize, scratch);
        } else if (kind == FTH_CK_BORDER) {
            HMM_Vec3 position = fth_cellBorderToPoint(cell->inners[i].border, innerOrigin, innerSize);
            ren3d_drawBillboard(vp, screenSize, *ui_cornerTexture, ui_colorAccent, position, HMM_V2(50, 50));
        }
    }
}