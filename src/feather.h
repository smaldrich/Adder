#pragma once

#include "snooze.h"

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
    FTH_CK_OUTSIDE,
    FTH_CK_INSIDE,
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
        fth_Cell* inner;
        fth_CellBorder offset;
    } inners[FTH_CELL_OFFSETS_COUNT];
    int16_t innerKinds;
    // wasting a lot of bytes if many outer/inner cells, but it makes lookups faster so who knows
    // FIXME: profile an irregular vs. regular struct setup
} fth_Cell;

typedef HMM_Vec3(*fth_ClosestPointFunc) (HMM_Vec3 pos);

struct {
    HMM_Mat4 transform;
    HMM_Mat4 inverseTransform;
    float radius;
} _fth_closestPointArgsSphere;

void fth_closestPointSetupSphere(HMM_Mat4 model, float radius) {
    _fth_closestPointArgsSphere.transform = model;
    _fth_closestPointArgsSphere.inverseTransform = HMM_InvGeneralM4(model);
    _fth_closestPointArgsSphere.radius = radius;
}

HMM_Vec3 fth_closestPointFuncSphere(HMM_Vec3 sourcePt) {
    HMM_Vec4 sourcePt4 = HMM_V4(sourcePt.X, sourcePt.Y, sourcePt.Z, 1);
    HMM_Vec3 pos = HMM_Mul(_fth_closestPointArgsSphere.inverseTransform, sourcePt4).XYZ;

    HMM_Vec4 onSphere = HMM_V4(0, 0, 0, 1);
    onSphere.XYZ = HMM_Mul(HMM_Norm(pos), _fth_closestPointArgsSphere.radius);
    HMM_Vec3 final = HMM_Mul(_fth_closestPointArgsSphere.transform, onSphere).XYZ;
    return final;
}

void fth_closestPointFuncToCells(fth_Cell* cell, fth_CellBound bound, fth_ClosestPointFunc func) {
    SNZ_ASSERT(cell->innerKinds == 0, "closestPointFuncToCells requires parent cell to be zeroed.");
    float innerSize = bound.size / 2;
    HMM_Vec3 innerCenterOffset = HMM_V3(innerSize / 2, innerSize / 2, innerSize / 2);
    for (int i = 0; i < FTH_CELL_OFFSETS_COUNT; i++) {
        HMM_Vec3 innerStart = HMM_Add(bound.origin, HMM_MulV3F(fth_cellOffsets[i], innerSize));
        HMM_Vec3 innerCenter = HMM_Add(innerStart, innerCenterOffset);

        HMM_Vec3 closestPoint = func(innerCenter);
        HMM_Vec3 diff = HMM_Sub(closestPoint, innerCenter);
        bool withinCell = true;
        for (int ax = 0; ax < 3; ax++) {
            if (diff.Elements[ax] > innerSize / 2) {
                withinCell = false;
                break;
            }
        }
    } // end looping over all 8 cells
}
