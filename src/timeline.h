#pragma once

#include "geometry.h"
#include "sketches2.h"
#include "sketchTriangulation.h"
#include "snooze.h"
#include "ui.h"

typedef enum {
    TL_OPK_SKETCH,
    TL_OPK_BASE_GEOMETRY,
    TL_OPK_SKETCH_GEOMETRY,
} tl_OpKind;

typedef struct tl_Op tl_Op;

typedef struct {
    sk_Sketch sketch;
} tl_OpSketch;

typedef struct {
    geo_Mesh mesh;
} tl_OpBaseGeometry;

typedef struct {
    geo_Align orbitOrigin;
    HMM_Vec2 orbitAngle;
    float orbitDist;

    geo_Mesh* mesh;
} tl_Scene;

tl_Scene tl_sceneInit() {
    // FIXME: initialize this to not be inside of geometry
    tl_Scene out = (tl_Scene){
        .orbitDist = 5,
        .orbitOrigin = geo_alignZero(),
        .mesh = NULL,
    };
    return out;
}

typedef enum {
    TL_OPS_INVALIDATED,
    TL_OPS_SOLVED,
    TL_OPS_MARKED_FOR_DELETE,
} tl_OpStatus;

struct tl_Op {
    tl_Op* next;
    tl_OpStatus status;

    struct {
        HMM_Vec2 pos;
        ui_SelectionState sel;
        snzu_Interaction inter;
    } ui;

    tl_OpKind kind;
    union {
        tl_OpSketch sketch;
        tl_OpBaseGeometry baseGeometry;
    } val;
    tl_Op* dependencies[1];

    tl_Scene scene;
};

typedef struct {
    tl_Op* firstOp;
    tl_Op* activeOp;
    HMM_Vec2 camPos;
    float camHeight;
    snz_Arena* operationArena;
    snz_Arena* generatedArena;
    PoolAlloc* generatedPool;
} tl_Timeline;

tl_Timeline tl_timelineInit(snz_Arena* opArena, snz_Arena* generatedArena, PoolAlloc* generatedPool) {
    tl_Timeline out = {
        .operationArena = opArena,
        .generatedArena = generatedArena,
        .generatedPool = generatedPool,
        .camHeight = 1000,
        .camPos = HMM_V2(0, 0),
    };
    return out;
}

tl_Op* tl_timelinePushSketch(tl_Timeline* tl, HMM_Vec2 pos, sk_Sketch sketch) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->operationArena, tl_Op);
    *out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_SKETCH,
        .val.sketch.sketch = sketch,
        .next = tl->firstOp,
        .scene = tl_sceneInit(),
    };
    tl->firstOp = out;
    return out;
}

tl_Op* tl_timelinePushBaseGeometry(tl_Timeline* tl, HMM_Vec2 pos, geo_Mesh mesh) {
    tl_Op* out = SNZ_ARENA_PUSH(tl->operationArena, tl_Op);
    *out = (tl_Op){
        .ui.pos = pos,
        .kind = TL_OPK_BASE_GEOMETRY,
        .next = tl->firstOp,
        .val.baseGeometry.mesh = mesh,
        .scene = tl_sceneInit(),
    };
    out->scene.mesh = &out->val.baseGeometry.mesh;

    tl->firstOp = out;
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

void tl_solve(tl_Timeline* t, snz_Arena* scratch) {
    while (true) { // FIXME: emergency cutoff
        bool anyChanged = false;
        for (tl_Op* op = t->firstOp; op; op = op->next) {
            if (op->status == TL_OPS_MARKED_FOR_DELETE) {
                continue;
            }

            tl_Op* dep = op->dependencies[0];
            if (dep) {
                if (dep->status == TL_OPS_MARKED_FOR_DELETE) {
                    op->dependencies[0] = NULL;
                } else if (dep->status == TL_OPS_INVALIDATED) {
                    op->status = TL_OPS_INVALIDATED;
                }
                anyChanged = true;
            } // end dep check
        }

        if (anyChanged) {
            break;
        }
    }

    { // cull elts marked for delete
        tl_Op* newFirst = NULL;
        tl_Op* next = NULL;
        for (tl_Op* op = t->firstOp; op; op = next) {
            next = op->next;
            if (op->status == TL_OPS_MARKED_FOR_DELETE) {
                continue;
            }
            op->next = newFirst;
            newFirst = op;
        }
        t->firstOp = newFirst;
    }

    // collect render meshes
    for (tl_Op* op = t->firstOp; op; op = op->next) {
        if (op->status != TL_OPS_INVALIDATED) {
            continue;
        }

        if (op->scene.mesh) {
            ren3d_meshDeinit(&op->scene.mesh->renderMesh);
        }
        op->scene.mesh = NULL;
    }

    // FIXME: free arenas and pools with generated data

    // resolve
    while (true) { // FIXME: emergency cutoff
        bool anySolved = false;
        for (tl_Op* op = t->firstOp; op; op = op->next) {
            SNZ_ASSERT(op->status != TL_OPS_MARKED_FOR_DELETE, "tl op marked for delete made it to the solving stage");
            if (!op->status != TL_OPS_INVALIDATED) {
                continue;
            } else if (op->dependencies[0] && op->dependencies[0]->status != TL_OPS_SOLVED) {
                continue;
            }

            if (op->kind == TL_OPK_SKETCH) {
                sk_sketchSolve(&op->val.sketch.sketch);
                op->status = TL_OPS_SOLVED;
            } else if (op->kind == TL_OPK_SKETCH_GEOMETRY) {
                tl_Op* other = op->val.sketchGeometry.sketch;
                SNZ_ASSERTF(other->kind == TL_OPK_SKETCH, "dependent op wasn't expected kind. Was: %d, expected %d.", other->kind, TL_OPK_SKETCH);
                SNZ_ASSERT(op->val.sketchGeometry.sketch, "dependent was null.");

                geo_Mesh* m = SNZ_ARENA_PUSH(t->generatedArena, geo_Mesh);
                *m = skt_sketchTriangulate(&other->val.sketch.sketch, t->generatedArena, scratch);
                m->renderMesh = geo_BSPTriListToRenderMesh(m->bspTris, scratch);
                geo_BSPTriListToFaceTris(t->generatedPool, m);
                other->scene.mesh = m;
                op->status = TL_OPS_SOLVED;
            }
        }
    } // end loop to iteratvely solve
}