#pragma once

#include "timeline.h"
#include "mesh.h"

/*
Geo references

need to break if dependency disappears
i.e. get/deref fn
also lazy evals of edges and corners / face loops would be great - but thats xcr

has safe node ptrs to the expected geometry
or safe sketch geo ptrs for sources
and a differentiator for different comps of operation results
theres gonna be a big assert in there to make sure only one piece of geometry matches every ptr
*/

typedef enum {
    OPS_GK_DEREF_FAILED,
    OPS_GK_CORNER,
    OPS_GK_EDGE,
    OPS_GK_FACE,
} ops_GeoKind;

typedef struct {
    ops_GeoKind kind;
    union {
        mesh_Corner* corner;
        mesh_Edge* edge;
        mesh_Face* face;
    };
} _ops_GeoPtr;

typedef enum {
    OPS_GRK_SKETCH,
    OPS_GRK_BASE_GEO,
    OPS_GRK_RESULT,
} ops_GeoRefKind;

typedef struct ops_GeoRef ops_GeoRef;
struct ops_GeoRef {
    ops_GeoKind geoKind;
    tl_Op* op;
    int64_t opUniqueId;

    ops_GeoRefKind refKind;
    union {
        struct {
            union {
                struct {
                    sk_Point* ptr;
                    uint64_t uniqueId;
                } point;

                struct {
                    sk_Line* ptr;
                    uint64_t uniqueId;
                } line;

                struct {
                    uint64_t lineUIDsHash;
                } face;
            } ptr;
        } sketch;

        struct {
            int diffInt;
            ops_GeoRef* diffGeo1;
            ops_GeoRef* diffGeo2;
        } opResult;

        struct {
            int idx;
        } baseGeo;
    } ref;
};

static _ops_GeoPtr _ops_geoRefDerefToGeoPtr(const mesh_Mesh* m, ops_GeoRef ref) {
    if (!ref.op || ref.op->uniqueId != ref.opUniqueId) {
        return (_ops_GeoPtr) { .kind = OPS_GK_DEREF_FAILED };
    }

    _ops_GeoPtr out = { .kind = ref.geoKind };
    if (ref.refKind == OPS_GRK_BASE_GEO) {
        SNZ_ASSERTF(
            ref.op->kind == TL_OPK_BASE_GEOMETRY,
            "Geo ref op has the wrong kind. Expected %d was %d.",
            TL_OPK_BASE_GEOMETRY, ref.op->kind);

        mesh_Mesh* m = &ref.op->val.baseGeometry.mesh;
        int64_t index = ref.ref.baseGeo.idx;

        if (ref.geoKind == OPS_GK_CORNER) {
            SNZ_ASSERTF(
                index < m->corners.count,
                "Ref index out of bounds. %lld corners in base mesh, wanted idx %lld.",
                m->corners.count, index);
            out.corner = &m->corners.elems[index];
        } else if (ref.geoKind == OPS_GK_EDGE) {
            int64_t i = 0;
            for (out.edge = m->firstEdge; i < index; i++) {
                out.edge = out.edge->next;
                if (out.edge == NULL) {
                    SNZ_ASSERTF(false, "Ref index of %lld out of bounds.", i);
                }
            }
        } else if (ref.geoKind == OPS_GK_FACE) {
            int64_t i = 0;
            for (out.face = m->firstFace; i < index; i++) {
                out.face = out.face->next;
                if (out.face == NULL) {
                    SNZ_ASSERTF(false, "Ref index of %lld out of bounds.", i);
                }
            }
        } else {
            SNZ_ASSERTF(false, "unreachable. kind: %d", ref.geoKind);
        }
    } else if (ref.refKind == OPS_GRK_SKETCH) {
    } else if (ref.refKind == OPS_GRK_RESULT) {
    } else {
        SNZ_ASSERTF(false, "unreachable. code: %d", ref.refKind);
    }
    return out;
}

mesh_Corner* mesh_geoRefDerefToCorner(const mesh_Mesh* m, ops_GeoRef ref) {
    _ops_GeoPtr geo = _ops_geoRefDerefToGeoPtr(m, ref);
    SNZ_ASSERTF(geo.kind == OPS_GK_CORNER, "Derefing a geoRef failed. Kind expected: %d, got %d.", OPS_GK_CORNER, geo.kind);
    return geo.corner;
}

mesh_Edge* mesh_geoRefDerefToEdge(const mesh_Mesh* m, ops_GeoRef ref) {
    _ops_GeoPtr geo = _ops_geoRefDerefToGeoPtr(m, ref);
    SNZ_ASSERTF(geo.kind == OPS_GK_EDGE, "Derefing a geoRef failed. Kind expected: %d, got %d.", OPS_GK_EDGE, geo.kind);
    return geo.edge;
}

mesh_Face* mesh_geoRefDerefToFace(const mesh_Mesh* m, ops_GeoRef ref) {
    _ops_GeoPtr geo = _ops_geoRefDerefToGeoPtr(m, ref);
    SNZ_ASSERTF(geo.kind == OPS_GK_FACE, "Derefing a geoRef failed. Kind expected: %d, got %d.", OPS_GK_FACE, geo.kind);
    return geo.face;
}
