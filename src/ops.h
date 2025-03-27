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
    ops_GeoRefKind kind;
    safeOpRef op; // via incrementing ids
    union {
        struct {
            sketch_Ref skGeo; // again, incrementing ids
        } sketch;

        struct {
            int diffInt;
            ops_GeoRef* diffGeo1;
            ops_GeoRef* diffGeo2;
        } opResult;

        struct {
            int faceOrEdgeOrCorner;
            int idx;
        } baseGeo;
    } ref;
};


static _ops_GeoPtr _ops_geoRefDerefToGeoPtr(const mesh_Mesh* m, ops_GeoRef ref) {
    if (ref.kind == OPS_GRK_BASE_GEO) {
        _ops_GeoPtr out = (_ops_GeoPtr){
            .kind = ref.kind,
        };

    } else if (ref.kind == OPS_GRK_SKETCH) {

    } else if (ref.kind == OPS_GRK_RESULT) {

    } else {
        SNZ_ASSERTF(false, "unreachable. code: %d", ref.kind);
    }

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
