#pragma once

#include "csg.h"
#include "sketches.h"
#include "snooze.h"

typedef struct geo_Vert geo_Vert;
typedef struct geo_Edge geo_Edge;

struct geo_Edge {
    geo_Vert* vertA;
    geo_Vert* vertB;
    geo_Edge* nextAllocated;
    bool doneSplitting;
};

struct geo_Vert {
    HMM_Vec3 pos;
};

void _geo_splitEdge(snz_Arena* scratch, geo_Edge* edge, geo_Edge** firstEdge, geo_Vert* intersection) {
    geo_Vert* vertA = edge->vertA;
    geo_Vert* vertB = edge->vertB;

    geo_Edge* e0 = edge;
    geo_Edge* e1 = SNZ_ARENA_PUSH(scratch, geo_Edge);
    e1->nextAllocated = *firstEdge;
    *firstEdge = e1;

    e0->vertA = vertA;
    e0->vertB = intersection;
    e1->vertA = intersection;
    e1->vertB = vertB;
}

void geo_sketchToTris(snz_Arena* scratch, snz_Arena* outArena, sk_Sketch* sketch) {
    assert(sk_sketchSolve(sketch) == SKE_OK);

    geo_Edge* firstEdge = NULL;

    // unpack sketch data into verts and edges that are better for this operation
    {
        geo_Vert* verts = SNZ_ARENA_PUSH_ARR(scratch, sketch->pointCount, geo_Vert);
        for (int64_t i = 0; i < sketch->pointCount; i++) {
            if (!sketch->points[i].inUse) {
                continue;
            }

            verts[i].pos.XY = sketch->points[i].pt;
        }

        for (int64_t lineIdx = 0; lineIdx < sketch->lineCount; lineIdx++) {
            sk_Line* l = &sketch->lines[lineIdx];
            if (!l->inUse) {
                continue;
            }
            assert(l->kind == SK_LK_STRAIGHT); // FIXME: this
            geo_Edge* edge = SNZ_ARENA_PUSH(scratch, geo_Edge);
            edge->vertA = &verts[l->p1.index];
            edge->vertB = &verts[l->p2.index];
            edge->nextAllocated = firstEdge;
            firstEdge = edge;
        }
    }  // end sketch data unpacking

    // FIXME: are these asserts what we want?
    assert(firstEdge != NULL);

    // split every edge with every other to generate verts at the intersections
    // could probably be optimized, but good enough for now
    for (geo_Edge* edge = firstEdge; edge != NULL; edge = edge->nextAllocated) {
        if (edge->doneSplitting) {
            continue;
        }

        // FIXME: coincident lines??? // should be prevented in sk_solve but currently arent
        HMM_Vec3 intersection = HMM_V3(0, 0, 0);
        geo_Edge* intersected = NULL;
        for (geo_Edge* other = edge->nextAllocated; other; other = other->nextAllocated) {
            if (other->doneSplitting) {
                continue;
            }
            // stolen: https://en.wikipedia.org/wiki/Line%E2%80%93line_intersection
            float x1 = edge->vertA->pos.X;
            float x2 = edge->vertB->pos.X;
            float y1 = edge->vertA->pos.Y;
            float y2 = edge->vertB->pos.Y;
            float x3 = other->vertA->pos.X;
            float x4 = other->vertB->pos.X;
            float y3 = other->vertA->pos.Y;
            float y4 = other->vertB->pos.Y;

            float t = (x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4);
            t /= (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
            if (!isfinite(t)) {
                continue;
            }
            // NOTE: we are purposefully expecting colinear points to not exist, they should split the line in the
            // sketch, not just exist on top of it or be constrained on top of it
            assert(!csg_floatEqual(t, 0) && !csg_floatEqual(t, 1));

            intersection = HMM_LerpV3(edge->vertA->pos, t, edge->vertB->pos);
            intersected = other;
            break;
        }

        if (intersected == NULL) {
            edge->doneSplitting = true;
            continue;
        }

        assert(!intersected->doneSplitting);

        geo_Vert* middle = SNZ_ARENA_PUSH(scratch, geo_Vert);
        middle->pos = intersection;
        _geo_splitEdge(scratch, edge, &firstEdge, middle);
        _geo_splitEdge(scratch, intersected, &firstEdge, middle);
    } // end intersection generation
}  // end geo_sketchToTris

void geo_tests() {
    snz_testPrintSection("geo");
}