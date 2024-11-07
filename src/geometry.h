#pragma once

#include "csg.h"
#include "sketches.h"

/*
lines+curves -> verts + edges
then intersections
verts+edges -> vertloops
vertloops -> tris
*/

typedef struct geo_Vert geo_Vert;
typedef struct geo_Edge geo_Edge;

struct geo_Edge {
    geo_Vert* vertA;
    geo_Vert* vertB;
    geo_Edge* nextOnVertA;
    geo_Edge* nextAllocated;
};

struct geo_Vert {
    HMM_Vec3 pos;
    geo_Edge* firstEdge;
};

void geo_sketchToTris(BumpAlloc* scratch, BumpAlloc* outArena, sk_Sketch* sketch) {
    assert(sk_sketchSolve(sketch) == SKE_OK);

    geo_Vert* firstVert = NULL;
    geo_Edge* firstEdge = NULL;

    // unpack sketch data into verts and edges that are better for this operation
    {
        geo_Vert* verts = BUMP_PUSH_ARR(scratch, sketch->pointCount, geo_Vert);
        for (int64_t i = 0; i < sketch->pointCount; i++) {
            if (!sketch->points[i].inUse) {
                continue;
            }

            verts[i].pos.XY = sketch->points[i].pt;
            if (firstVert == NULL) {
                firstVert = &verts[i];
            }
        }

        for (int64_t lineIdx = 0; lineIdx < sketch->lineCount; lineIdx++) {
            sk_Line* l = &sketch->lines[lineIdx];
            if (!l->inUse) {
                continue;
            }
            assert(l->kind == SK_LK_STRAIGHT); // FIXME: this
            geo_Edge* edge = BUMP_PUSH_NEW(scratch, geo_Edge);
            edge->vertA = &verts[l->p1.index];
            edge->vertB = &verts[l->p2.index];
            edge->nextOnVertA = edge->vertA->firstEdge;
            edge->vertA->firstEdge = edge;

            edge->nextAllocated = firstEdge;
            firstEdge = edge;
        }
    }  // end sketch data unpacking

    // FIXME: are these asserts what we want?
    assert(firstVert != NULL);
    assert(firstEdge != NULL);

    // split every edge with every other to generate verts at the intersections
    // could probably be optimized, but good enough for now
    for (geo_Edge* edge = firstEdge; edge; edge = edge->nextAllocated) {
        for (geo_Edge* other = edge->nextAllocated; other; other = other->nextAllocated) {
            // stolen: https://en.wikipedia.org/wiki/Line%E2%80%93line_intersection
            float x1 = edge->vertA->pos.X;
            float x2 = edge->vertB->pos.X;
            float y1 = edge->vertA->pos.Y;
            float y2 = edge->vertB->pos.Y;
            float x3 = other->vertA->pos.X;
            float x4 = other->vertB->pos.X;
            float y3 = other->vertA->pos.Y;
            float y4 = other->vertB->pos.Y;

            // FIXME: coincident lines???
            float t = (x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4);
            t /= (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
            if (!isfinite(t)) {
                continue;
            }

            // FIXME: use doubles in the future
            // FIXME: precision of the new vert when it is 'on' another line?? Could drift over time and become a problem??
            bool under1 = t < 1 || csg_floatEqual(t, 1);
            bool over0 = t > 0 || csg_floatEqual(t, 0);
            if (!under1 || !over0) {
                continue;
            }

            HMM_Vec3 intersection = HMM_LerpV3(edge->vertA->pos, t, edge->vertB->pos);

            // 1 remove both lines from their LL around each vert
            // remove both from the allocd LL
            // fix verts first edge
        }
    }
}  // end geo_sketchToTris
