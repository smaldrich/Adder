#pragma once

#include "geometry.h"
#include "sketches2.h"
#include "sketchTriangulation.h"
#include "snooze.h"
#include "ui.h"

typedef struct {
    geo_Align orbitOrigin;
    HMM_Vec2 orbitAngle;
    float orbitDist;
} tl_Scene;

tl_Scene tl_sceneInit() {
    // FIXME: initialize this to not be inside of geometry
    tl_Scene out = (tl_Scene){
        .orbitDist = 5,
        .orbitOrigin = geo_alignZero(),
    };
    return out;
}

typedef enum {
    TL_OPK_NONE,
    TL_OPK_SKETCH,
    TL_OPK_BASE_GEOMETRY,
    TL_OPK_EXTRUDE,

    TL_OPK_COUNT,
} tl_OpKind;

const char* tl_opKindNames[] = {
    [TL_OPK_SKETCH] = "sketch",
    [TL_OPK_BASE_GEOMETRY] = "geometry",
    [TL_OPK_EXTRUDE] = "extrude",
};

typedef enum {
    TL_OPAK_NONE = 0,
    TL_OPAK_NUMBER = (1 << 0),
    TL_OPAK_GEOID_CORNER = (1 << 1),
    TL_OPAK_GEOID_EDGE = (1 << 2),
    TL_OPAK_GEOID_FACE = (1 << 3),
} tl_OpArgKind;

bool tl_opArgKindExpectsDependency(tl_OpArgKind argKind) {
    if (argKind == TL_OPAK_NONE) {
        return false;
    } else if (argKind == TL_OPAK_NUMBER) {
        return false;
    } else if (argKind == TL_OPAK_GEOID_CORNER) {
        return true;
    } else if (argKind == TL_OPAK_GEOID_EDGE) {
        return true;
    } else if (argKind == TL_OPAK_GEOID_FACE) {
        return true;
    }
    SNZ_ASSERTF(false, "unreachable. kind: %d", argKind);
    return false;
}

typedef struct tl_Op tl_Op;
typedef struct {
    tl_OpArgKind kind;
    float number;
    mesh_GeoID geoId;
} tl_OpArg;

#define TL_OP_ARG_MAX_COUNT 5
// lookup from opKind -> opArgKinds
int tl_opArgKindsExpected[][TL_OP_ARG_MAX_COUNT] = {
    [TL_OPK_SKETCH] = { 0 },
    [TL_OPK_BASE_GEOMETRY] = { 0 },
    [TL_OPK_EXTRUDE] = {TL_OPAK_GEOID_FACE, TL_OPAK_NUMBER},
};

// lookup from opKind -> arg names
const char* tl_opArgNames[][TL_OP_ARG_MAX_COUNT] = {
    [TL_OPK_SKETCH] = { 0 },
    [TL_OPK_BASE_GEOMETRY] = { 0 },
    [TL_OPK_EXTRUDE] = { "face", "distance" },
};

struct tl_Op {
    tl_Op* next;
    int64_t uniqueId; // incrementing number to safely identify tl nodes in geo refs (and break them when tl_ops are deleted)
    bool markedForDeletion;

    struct {
        HMM_Vec2 pos;
        ui_SelectionState sel;
        snzu_Interaction inter;
    } ui;

    tl_OpKind kind;
    union {
        sk_Sketch sketch;
        mesh_FaceSlice baseGeometry;
    } val;
    tl_OpArg args[TL_OP_ARG_MAX_COUNT];

    tl_Scene scene;

    mesh_FaceSlice solvedFaces;
};

SNZ_SLICE_NAMED(tl_Op*, tl_OpPtrSlice);

typedef struct {
    tl_Op* firstOp;
    tl_Op* activeOp;
    int64_t nextUniqueId; // used to safely id nodes even if the address is reused
    HMM_Vec2 camPos;
    float camHeight;
    snz_Arena* operationArena;

    snz_Arena* generatedArena;
    PoolAlloc* generatedPool;

    tl_OpArg takenArgSignal; // set by scs to take geo, unset by handling code in argbar
} tl_Timeline;

tl_Timeline tl_timelineInit(snz_Arena* opArena, snz_Arena* generatedArena, PoolAlloc* generatedPool) {
    tl_Timeline out = {
        .operationArena = opArena,
        .generatedArena = generatedArena,
        .generatedPool = generatedPool,
        .camHeight = 1000,
        .camPos = HMM_V2(0, 0),
        .nextUniqueId = 1,
    };
    return out;
}

// pushes to list for the TL, sets a good uniqueID, and inits scene to a default
static tl_Op* _tl_timelinePushOp(tl_Timeline* tl) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->operationArena, tl_Op);
    *out = (tl_Op){
        .next = tl->firstOp,
        .uniqueId = tl->nextUniqueId,
        .scene = tl_sceneInit(),
    };
    tl->firstOp = out;
    tl->nextUniqueId++;
    return out;
}

// FIXME: make it a hash lookup
tl_Op* tl_timelineGetOpByUID(tl_Timeline* tl, int64_t uid) {
    if (uid == 0) { // temporary, for perf
        return NULL;
    }

    for (tl_Op* op = tl->firstOp; op; op = op->next) {
        if (op->uniqueId == uid) {
            return op;
        }
    }
    return NULL;
}

tl_Op* tl_timelinePushSketch(tl_Timeline* tl, HMM_Vec2 pos, sk_Sketch sketch) {
    tl_Op* out = _tl_timelinePushOp(tl);
    out->ui.pos = pos;
    out->kind = TL_OPK_SKETCH;
    out->val.sketch = sketch;
    return out;
}

// generates geoIds for faces - which shouldn't be retained after this call
tl_Op* tl_timelinePushBaseGeometry(tl_Timeline* tl, HMM_Vec2 pos, mesh_FaceSlice faces) {
    tl_Op* out = _tl_timelinePushOp(tl);
    out->ui.pos = pos;
    out->kind = TL_OPK_BASE_GEOMETRY;

    int64_t nextId = 1;
    for (int64_t i = 0; i < faces.count; i++) {
        mesh_Face* f = &faces.elems[i];
        f->id = (mesh_GeoID){
            .geoKind = MESH_GK_FACE,
            .baseNodeId = nextId,
            .opUniqueId = out->uniqueId,
        };
        nextId++;
    }
    out->val.baseGeometry = faces;
    return out;
}

tl_Op* tl_timelinePushExtrude(tl_Timeline* tl, HMM_Vec2 pos) {
    tl_Op* out = _tl_timelinePushOp(tl);
    out->ui.pos = pos;
    out->kind = TL_OPK_EXTRUDE;
    return out;
}

void tl_timelineDeselectAll(tl_Timeline* tl) {
    for (tl_Op* op = tl->firstOp; op; op = op->next) {
        op->ui.sel.selected = false;
    }
}

static bool tl_timelineAnySelected(tl_Timeline* tl) {
    for (tl_Op* o = tl->firstOp; o; o = o->next) {
        if (o->ui.sel.selected) {
            return true;
        }
    }
    return false;
}

void tl_timelineCullOpsMarkedForDelete(tl_Timeline* t) {
    // cull dependencies to deleted ops
    for (tl_Op* op = t->firstOp; op; op = op->next) {
        if (t->activeOp == op && op->markedForDeletion) {
            t->activeOp = NULL;
        }

        for (int i = 0; i < TL_OP_ARG_MAX_COUNT; i++) {
            if (op->args[i].kind == TL_OPAK_NONE) {
                continue;
            }
            // if the op has no source (i.e. is a number, source id will be zero, which will never match)
            tl_Op* other = tl_timelineGetOpByUID(t, op->args[i].geoId.opUniqueId);
            if (other && other->markedForDeletion) {
                op->args[i] = (tl_OpArg){ 0 };
            }
        }
    }

    // cull elts marked for delete
    tl_Op* next = NULL;
    tl_Op** lastNextPtr = &t->firstOp;
    for (tl_Op* op = t->firstOp; op; op = next) {
        next = op->next;
        if (op->markedForDeletion) {
            *lastNextPtr = op->next;
            memset(op, 0, sizeof(*op));
            continue;
        }
        lastNextPtr = &op->next;
    }
    // FIXME: free list
}

// FIXME: bubbles please
void tl_solve(tl_Timeline* t, ren3d_Mesh* renderMesh, snz_Arena* scratch) {
    // FIXME: topo sort please :)
    // FIXME: current geoID hashmap please

    snz_arenaClear(t->generatedArena);
    poolAllocClear(t->generatedPool);

    for (int i = dependencies.count - 1; i >= 0; i--) {
        tl_Op* op = dependencies.elems[i];
        SNZ_ASSERT(!op->markedForDeletion, "tl op marked for delete made it to the solving stage");
        op->solvedFaces = (mesh_FaceSlice){ 0 };

        if (op->kind == TL_OPK_SKETCH) {
            sk_sketchSolve(&op->val.sketch);
            mesh_Mesh* m = SNZ_ARENA_PUSH(t->generatedArena, mesh_Mesh);
            *m = skt_sketchTriangulate(op->uniqueId, &op->val.sketch, t->generatedArena, scratch);
            op->scene.mesh = m;
        } else if (op->kind == TL_OPK_BASE_GEOMETRY) {
            op->solvedFaces = op->val.baseGeometry;
        } else if (op->kind == TL_OPK_EXTRUDE) {
            SNZ_ASSERTF(op->args[0].kind == TL_OPAK_GEOID_FACE, "Extrude requires first arg to be a face. Actual kind: %d", op->args[0].kind);
            tl_Op* dep = tl_timelineGetOpByUID(t, op->args[0].geoId.opUniqueId);
            SNZ_ASSERTF(op->args[1].kind == TL_OPAK_NUMBER, "Extrude requires second arg to be a number. Actual kind: %d", op->args[1].kind);

            // duplicate previous mesh
            mesh_Mesh* m = SNZ_ARENA_PUSH(t->generatedArena, mesh_Mesh);
            *m = mesh_meshDuplicateTrisAndFaces(dep->scene.mesh, scratch, t->generatedArena);
            op->scene.mesh = m;
            // FIXME: could copy old ones if they exist instead of regenerating
            mesh_BSPTriListToFaceTris(t->generatedPool, m);
            mesh_meshGenerateEdges(m, t->generatedArena, scratch);
            mesh_meshGenerateCorners(m, t->generatedArena, scratch);

            mesh_GeoPtr geoPtr = mesh_geoIdFind(op->scene.mesh, op->args[0].geoId);
            SNZ_ASSERT(geoPtr.kind == MESH_GK_FACE, "Extrude geoid find failed.");
            mesh_Face* ogFace = geoPtr.face;

            mesh_Face* newFace = SNZ_ARENA_PUSH(t->generatedArena, mesh_Face);
            *newFace = (mesh_Face){
                .next = m->firstFace,
                .id = (mesh_GeoID) {
                    .geoKind = MESH_GK_FACE,
                    .opUniqueId = op->uniqueId,
                    .differentiationInt = 1,
                    .diffGeo1 = mesh_geoIdDuplicate(&op->args[0].geoId, t->generatedArena),
                },
            };
            m->firstFace = newFace;

            mesh_BSPTriList newTris = mesh_BSPTriListInit();

            HMM_Vec3 translation = HMM_V3(0, 0, 0);
            { // duplicate face
                // FIXME: this is reallllly slow, if facetris are already generated, should use those instead
                // FIXME: or make a more compact representation of the tris in the mesh for iteration like this
                for (mesh_BSPTri* tri = m->bspTris.first; tri; tri = tri->next) {
                    if (tri->sourceFace == ogFace) {
                        mesh_BSPTriListPushNew(t->generatedArena, &newTris, tri->tri.a, tri->tri.b, tri->tri.c, newFace);
                    }
                }
                // FIXME: round faces tho??? -> requires refactor to use facetris
                translation = HMM_Mul(geo_triNormal(newTris.first->tri), op->args[1].number);
                mesh_BSPTriListTransform(&newTris, HMM_Translate(translation)); // FIXME: can do this faster with just a translate list fn
            }

            { // stitch siding
                mesh_VertLoop* loops = mesh_faceToVertLoops(m, ogFace, scratch, scratch);
                for (mesh_VertLoop* loop = loops; loop; loop = loop->next) {
                    int64_t count = loop->points.count;
                    for (int64_t i = 0; i < count; i++) {
                        HMM_Vec3 pt = loop->points.elems[i];
                        HMM_Vec3 nextPt = loop->points.elems[(i + 1) % count];

                        HMM_Vec3 upperPt = HMM_Add(pt, translation);
                        HMM_Vec3 upperNextPt = HMM_Add(nextPt, translation);

                        mesh_BSPTriListPushNew(t->generatedArena, &newTris, pt, upperPt, upperNextPt, newFace);
                        mesh_BSPTriListPushNew(t->generatedArena, &newTris, upperNextPt, nextPt, pt, newFace);
                    }
                }
            }
            mesh_BSPTriListJoin(&m->bspTris, &newTris);
        } else {
            SNZ_ASSERTF(false, "unreachable. kind: %lld", op->kind);
        }
    } // end loop solving

    // FIXME: rendermesh update :)
}
