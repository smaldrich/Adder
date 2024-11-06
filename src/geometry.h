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
            geo_Edge* edge = BUMP_PUSH_NEW(scratch, geo_Edge);
            edge->vertA = &verts[l->p1.index];
            edge->vertB = &verts[l->p2.index];
            edge->nextOnVertA = edge->vertA->firstEdge;
            edge->vertA->firstEdge = edge;

            edge->nextAllocated = firstEdge;
            firstEdge = edge;
        }
    }  // end sketch data unpacking

    FIXME sketch validation :)
    FIXME null 1st point or 1st edge?
}  // end geo_sketchToTris
