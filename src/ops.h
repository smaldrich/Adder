#pragma once

#include "timeline.h"
#include "mesh.h"

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
} ops_GeoPtr;

typedef enum {
    OPS_GRK_SKETCH,
    OPS_GRK_BASE_GEO,
    OPS_GRK_RESULT,
} ops_GeoRefKind;

/*
Geo refs are the backend tech to make the parametric part of this thing work.
there is one embedded in every piece of geometry, describing it in terms of the operations that created it.
Other operations use georefs to find the geometry they are supposed to be operating on at solve time
*/

typedef struct ops_GeoRef ops_GeoRef;
struct ops_GeoRef {
    ops_GeoKind geoKind;
    int64_t opUniqueId; // uid of the operation that created/last changed this piece of geo
    int64_t sourceUniqueId; // if from a sketch or base geo node, a uid matching what part
    // used to label different parts of an operation
    // (i.e. extrude labels corners and edges from one source corner with the same source geo, but a different diffInt)
    int64_t differentiationInt;

    // used for unions and other operations that need to reference another piece of geometry to refer to a resulting piece of geometry
    ops_GeoRef* diffGeo1;
    ops_GeoRef* diffGeo2;
};

bool _ops_geoRefEqual(ops_GeoRef a, ops_GeoRef b) {
    bool equal = false;
    equal &= a.geoKind == b.geoKind;
    equal &= a.opUniqueId == b.opUniqueId;
    equal &= a.sourceUniqueId == b.sourceUniqueId;
    equal &= a.differentiationInt == b.differentiationInt;
    equal &= (a.diffGeo1 == NULL) == (b.diffGeo1 == NULL);
    equal &= (a.diffGeo2 == NULL) == (b.diffGeo2 == NULL);

    if (a.diffGeo1) {
        equal &= _ops_geoRefEqual(*a.diffGeo1, *b.diffGeo1);
    }
    if (a.diffGeo2) {
        equal &= _ops_geoRefEqual(*a.diffGeo2, *b.diffGeo2);
    }
    return equal;
}

// searches the given mesh for a piece of geometry with a matching geo ref to the one given
// return will have a null ptr and kind of OPS_GK_DEREF_FAILED if nothing is found
// FIXME: for a perf boost, use a hashtable for geoRef -> geo in each mesh instead of looping
ops_GeoPtr ops_geoRefDerefToGeoPtr(const mesh_Mesh* m, ops_GeoRef ref) {
    ops_GeoPtr out = (ops_GeoPtr){
        .kind = ref.geoKind,
    };

    if (out.kind == OPS_GK_CORNER) {
        for (int64_t i = 0; i < m->corners.count; i++) {
            mesh_Corner* corner = &m->corners.elems[i];
            if (_ops_geoRefEqual(ref, corner)) {
                out.corner = corner;
                return out;
            }
        }
    } else if (out.kind == OPS_GK_EDGE) {
        for (mesh_Edge* e = m->firstEdge; e; e = e->next) {
            if (_ops_geoRefEqual(ref, e)) {
                out.edge = e;
                return out;
            }
        }
    } else if (out.kind == OPS_GK_FACE) {
        for (mesh_Face* f = m->firstFace; f; f = f->next) {
            if (_ops_geoRefEqual(ref, f)) {
                out.face = f;
                return out;
            }
        }
    } else {
        SNZ_ASSERTF(false, "unreachable. kind: %d", ref.geoKind);
    }

    out.kind = OPS_GK_DEREF_FAILED;
    return out;
}
