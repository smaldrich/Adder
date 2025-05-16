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

int _fth_quadrantToCellOffsetIdx(bool x, bool y, bool z) {
    return (x << 2) + (y << 1) + z;
}

typedef enum {
    FTH_CK_PARENT,
    FTH_CK_DISTANCE,
    FTH_CK_OUTSIDE,
    FTH_CK_INSIDE,
} fth_CellKind;

typedef struct {
    uint8_t* data;
    int64_t dataLength;
    /*
    data format:
        irregular to maximize space efficency
        if FTH_CK_PARENT: (by default, the start of stream is)
            int16_t for the innerKinds, two bits each, where cell 0 is least significant
            i.e. 00 00 00 00 00 00 00 00 is all of the kinds in bin, rightmost is cell 0
            followed by inner data, where any cell marked as a parent is a pointer, others
            are as follows
        if FTH_CK_DISTANCE:
            float for distance to surface
        if FTH_CK_OUTSIDE or FTH_CK_INSIDE:
            nothing, indicates that this cell isnt a border, can be skipped surfacing
    */

    HMM_Vec3 origin;
    float size;
} fth_Solid;

typedef float (*fth_DistFunc)(HMM_Vec3 pos);

HMM_Mat4 _fth_distFuncCubeInvTransform = { 0 };
HMM_Vec3 _fth_distFuncCubeHalfWidths = { 0 };
void _fth_distFuncCubeSetup(HMM_Mat4 transform, HMM_Vec3 halfwidths) {
    _fth_distFuncCubeInvTransform = HMM_InvGeneralM4(transform);
    _fth_distFuncCubeHalfWidths = halfwidths;
}
// https://iquilezles.org/articles/distfunctions/
float _fth_distFuncCube(HMM_Vec3 pos) {
    HMM_Vec4 posV4 = HMM_V4(pos.X, pos.Y, pos.Z, 0);
    pos = HMM_MulM4V4(_fth_distFuncCubeInvTransform, posV4).XYZ;

    HMM_Vec3 rel = (HMM_Vec3){
        .X = HMM_ABS(pos.X - _fth_distFuncCubeHalfWidths.X),
        .Y = HMM_ABS(pos.Y - _fth_distFuncCubeHalfWidths.Y),
        .Z = HMM_ABS(pos.Z - _fth_distFuncCubeHalfWidths.Z),
    };

    HMM_Vec3 relMaxxed = (HMM_Vec3){
        .X = HMM_MAX(rel.X, 0),
        .Y = HMM_MAX(rel.Y, 0),
        .Z = HMM_MAX(rel.Z, 0),
    };
    float maxAxis = HMM_MAX(rel.X, HMM_MAX(rel.Y, rel.Z)); // only take if negative
    return HMM_Len(relMaxxed) + HMM_MIN(0, maxAxis);
}

HMM_Mat4 _fth_distFuncSphereInvTransform = { 0 };
float _fth_distFuncSphereRadius = 0;
void _fth_distFuncSphereSetup(HMM_Mat4 transform, float radius) {
    _fth_distFuncSphereInvTransform = HMM_InvGeneralM4(transform);
    _fth_distFuncSphereRadius = radius;
}
float _fth_distFuncSphere(HMM_Vec3 pos) {
    HMM_Vec4 posV4 = HMM_V4(pos.X, pos.Y, pos.Z, 0);
    pos = HMM_MulM4V4(_fth_distFuncCubeInvTransform, posV4).XYZ;
    return HMM_Len(pos) - _fth_distFuncSphereRadius;
}

const fth_Solid* _fth_distFuncSolidSolid = NULL;
HMM_Mat4 _fth_distFuncSolidInvTransform = { 0 };
void _fth_distFuncSolidSetup(HMM_Mat4 transform, const fth_Solid* solid) {
    _fth_distFuncSolidInvTransform = HMM_InvGeneralM4(transform);
    _fth_distFuncSolidSolid = solid;
}
float _fth_distFuncSolid(HMM_Vec3 pos) {
    const fth_Solid* solid = _fth_distFuncSolidSolid;

    HMM_Vec3 cellOrigin = solid->origin;
    float cellSize = solid->size;
    uint8_t* cellStart = solid->data;
    fth_CellKind cellKind = FTH_CK_PARENT;

    uint8_t* data = NULL;
    while (cellKind == FTH_CK_PARENT) { // FIXME: cutoff
        float innerSize = cellSize / 2;
        HMM_Vec3 center = HMM_Add(cellOrigin, HMM_V3(innerSize, innerSize, innerSize));
        HMM_Vec3 diff = HMM_Sub(pos, center);
        int offsetIdx = _fth_quadrantToCellOffsetIdx(diff.X > 0, diff.Y > 0, diff.Z > 0);

        uint16_t kinds = *((uint16_t*)cellStart);
        cellStart += sizeof(uint16_t);
        kinds >>= 2 * offsetIdx;
        fth_CellKind innerKind = kinds & 0b11;

        cellSize = innerSize;
        cellOrigin = HMM_Add(cellOrigin, HMM_MulV3F(fth_cellOffsets[offsetIdx], innerSize));
        cellKind = innerKind;
    }

    if (cellKind == FTH_CK_INSIDE) {
        return -INFINITY;
    } else if (cellKind == FTH_CK_OUTSIDE) {
        return INFINITY;
    } else if (cellKind == FTH_CK_DISTANCE) {
        fix this;
    }
    SNZ_ASSERTF(false, "unreachable. case: %d", cellKind);
    return 0;
}

// recursive call to generate cells within a parent cell based on some distance function
// assumes it starts as writing a parent (won't lead with a kind)
void _fth_distFuncToSolid(float detailCutoff,
                          HMM_Vec3 outerBoundStart, float outerBoundWidth,
                          fth_DistFunc func,
                          snz_Arena* arena) {
    float innerBoundWidth = outerBoundWidth / 2;
    float sampleOffset = innerBoundWidth / 2;

    uint16_t kinds = 0;
    float distances[FTH_CELL_OFFSETS_COUNT] = { 0 };
    for (int i = FTH_CELL_OFFSETS_COUNT - 1; i >= 0; i--) {
        HMM_Vec3 innerBoundStart = HMM_Add(outerBoundStart, fth_cellOffsets[i]);
        HMM_Vec3 samplePos = HMM_Add(innerBoundStart, HMM_V3(sampleOffset, sampleOffset, sampleOffset));
        float dist = func(samplePos);
        distances[i] = dist;

        fth_CellKind kind = FTH_CK_PARENT;
        if (fabsf(dist) > innerBoundWidth * sqrtf(3)) { // distance larger than this cell, mark inner or outer
            kind = (dist > 0) ? FTH_CK_OUTSIDE : FTH_CK_INSIDE;
        } else if (fabsf(dist) < detailCutoff) { // below stop line, record
            kind = FTH_CK_DISTANCE;
        }

        // reverse order loop means kinds get added so cell 0 is least significant
        kinds <<= 2;
        kinds |= 0b11 & kind;
    }
    *(uint16_t*)SNZ_ARENA_PUSH_ARR(arena, sizeof(uint16_t), uint8_t) = kinds;

    for (int i = 0; i < FTH_CELL_OFFSETS_COUNT; (i++, kinds >>= 2)) {
        fth_CellKind kind = kinds & 0b11;
        if (kind == FTH_CK_DISTANCE) {
            // float for dist
        } else if (kind == FTH_CK_PARENT) {
            // ptr
        }
        // outside marked and inside marked don't need additional data
    } // end extra data
} // end _fth_distFuncToSolid

fth_Solid fth_cube(HMM_Mat4 model, HMM_Vec3 halfWidths, float detailCutoff, snz_Arena* arena) {
    _fth_distFuncCubeSetup(model, halfWidths);
    SNZ_ARENA_ARR_BEGIN(arena, uint8_t);
    _fth_distFuncToSolid(detailCutoff, HMM_V3(0, 0, 0), 4, _fth_distFuncCube, arena);
    uint8_tSlice data = SNZ_ARENA_ARR_END(arena, uint8_t);
    fth_Solid out = (fth_Solid){
        .data = data.elems,
        .dataLength = data.count,
        .origin = HMM_V3(0, 0, 0),
        .size = 4,
    };
    return out;
}

fth_Solid fth_sphere(HMM_Mat4 model, float radius, float detailCutoff, snz_Arena* arena) {
    _fth_distFuncSphereSetup(model, radius);
    SNZ_ARENA_ARR_BEGIN(arena, uint8_t);
    _fth_distFuncToSolid(detailCutoff, HMM_V3(0, 0, 0), 4, _fth_distFuncSphere, arena);
    uint8_tSlice data = SNZ_ARENA_ARR_END(arena, uint8_t);
    fth_Solid out = (fth_Solid){
        .data = data.elems,
        .dataLength = data.count,
        .origin = HMM_V3(0, 0, 0),
        .size = 4,
    };
    return out;
}

void fth_solidToRenderable() {

}
