#pragma once

#include "snooze.h"
#include "sketches2.h"

typedef struct _skt_Island _skt_Island;
typedef struct _skt_Point _skt_Point;
SNZ_SLICE(_skt_Point);
SNZ_SLICE_NAMED(_skt_Point*, _skt_PointPtrSlice);

typedef struct {
    _skt_Point* other;
    bool traversed; // FIXME: the padding on this one hurts but who cares
} _skt_Edge;

SNZ_SLICE(_skt_Edge);

struct _skt_Point {
    _skt_Point* next;
    _skt_Island* island;
    _skt_EdgeSlice edges;
    HMM_Vec2 pos;
    int dbgIndex;
};

typedef struct _skt_VertLoop _skt_VertLoop;
struct _skt_VertLoop {
    _skt_VertLoop* next;
    HMM_Vec2Slice pts;
};

struct _skt_Island {
    _skt_VertLoop* perimeterLoop;
    _skt_VertLoop* firstLoop;
    _skt_Island* next;
};

void _skt_markIsland(_skt_Point* pt, _skt_Island* island, snz_Arena* arena) {
    pt->island = island;
    for (int i = 0; i < pt->edges.count; i++) {
        _skt_Point* other = pt->edges.elems[i].other;
        if (other->island) {
            continue;
        }
        _skt_markIsland(other, island, arena);
    }
}

// FIXME: tests
bool _skt_lineLineIntersection(HMM_Vec2 l1a, HMM_Vec2 l1b, HMM_Vec2 l2a, HMM_Vec2 l2b, float* outT) {
    HMM_Vec2 l1Dir = HMM_Norm(HMM_Sub(l1b, l1a));
    HMM_Vec2 l2Dir = HMM_Norm(HMM_Sub(l2b, l2a));
    if (geo_v2Equal(l1a, l2a) && geo_v2Equal(l1Dir, l2Dir)) {
        return true;  // coincident
    }

    // stolen: https://en.wikipedia.org/wiki/Line%E2%80%93line_intersection
    float x1 = l1a.X;
    float x2 = l1b.X;
    float y1 = l1a.Y;
    float y2 = l1b.Y;
    float x3 = l2a.X;
    float x4 = l2b.X;
    float y3 = l2a.Y;
    float y4 = l2b.Y;

    float denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (geo_floatZero(denom)) {
        return false; // parallel
    }

    float u = (x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4);
    u /= denom;
    if (!geo_floatGreater(u, 0) || !geo_floatLess(u, 1)) {
        return false; // past bounds on one line, will trip on ~equal to ends
    }

    float v = (x1 - x2) * (y1 - y3) - (y1 - y2) * (x1 - x3);
    v /= -denom;
    if (!geo_floatGreater(v, 0) || !geo_floatLess(v, 1)) {
        return false;
    }

    *outT = u;
    return true;
}

// FIXME: tests
bool _skt_vertLoopContainsPoint(_skt_VertLoop* l, HMM_Vec2 pt) {
    int hitCount = 0;
    HMM_Vec2 rayOrigin = pt;
    HMM_Vec2 rayOther = HMM_Add(pt, HMM_V2(100000, 0)); // FIXME: maybe this fails in insane cases, but INF i haven't tested so this is just gonna work for now.
    for (int i = 0; i < l->pts.count; i++) {
        HMM_Vec2 l1a = l->pts.elems[i];
        HMM_Vec2 l1b = l->pts.elems[(i + 1) % l->pts.count];
        float t = 0;
        if (_skt_lineLineIntersection(rayOrigin, rayOther, l1a, l1b, &t)) {
            hitCount++;
        }
    }
    // point must have been inside if it crosses an odd number of times
    return (hitCount % 2) == 1;
}

// checks only that B is inside of A
// FIXME: tests
bool _skt_vertLoopContainsVertLoop(_skt_VertLoop* a, _skt_VertLoop* b) {
    SNZ_ASSERTF(b->pts.count > 0, "Vert loop with zero or less points. Had: %lld", b->pts.count);
    bool ptInside = _skt_vertLoopContainsPoint(a, b->pts.elems[0]);
    if (!ptInside) {
        return false;
    }

    for (int i = 0; i < a->pts.count; i++) {
        HMM_Vec2 l1a = a->pts.elems[i];
        HMM_Vec2 l1b = a->pts.elems[(i + 1) % a->pts.count];
        for (int j = 0; j < b->pts.count; j++) {
            HMM_Vec2 l2a = b->pts.elems[j];
            HMM_Vec2 l2b = b->pts.elems[(j + 1) % b->pts.count];
            float t = 0;
            if (!_skt_lineLineIntersection(l1a, l1b, l2a, l2b, &t)) {
                return false;
            }
        }
    }
    return true;
}

// static void _skt_vertLoopPrint(_skt_VertLoop* l) {
//     for (int i = 0; i < l->pts.count; i++) {
//         HMM_Vec2 p = l->pts.elems[i];
//         printf("\t%.2f, %.2f\n", p.X, p.Y);
//     }
// }

static geo_MeshFace* _skt_vertLoopToMeshFace(_skt_VertLoop* l, geo_BSPTriList* list, snz_Arena* scratch, snz_Arena* out) {
    geo_MeshFace* f = SNZ_ARENA_PUSH(out, geo_MeshFace);

    // FIXME: move this data to be inline with the vert loop points ? // actually also profile if that is worth doing
    bool* culledFlags = SNZ_ARENA_PUSH_ARR(scratch, l->pts.count, bool);
    int culledCount = 0;

    int iterationsSinceTriWasAdded = 0;
    for (int startIdx = 0; true; (startIdx++, iterationsSinceTriWasAdded++)) {
        if (iterationsSinceTriWasAdded > l->pts.count) {
            // FIXME: this is too loose technically for infinite loop cases, but I don't know why they are happening so this is just gonna be a failsafe when they do
            break;
        }

        startIdx %= l->pts.count;

        if (culledCount >= l->pts.count - 2) {
            break;
        } else if (culledFlags[startIdx]) {
            continue;
        }

        int ptIndexes[3] = { startIdx, 0, 0 };
        for (int i = 1; i < 3; i++) {
            int idx = ptIndexes[i - 1] + 1;
            for (; true; idx++) { // FIXME: emergency crash here
                int modded = idx % l->pts.count;
                if (culledFlags[modded]) {
                    continue;
                }
                ptIndexes[i] = modded;
                break;
            }
        }

        geo_Tri t = { 0 };
        for (int i = 0; i < 3; i++) {
            HMM_Vec2 pt = l->pts.elems[ptIndexes[i]];
            t.elems[i].XY = pt;
        }

        HMM_Vec2 diffB = HMM_Sub(t.b.XY, t.a.XY);
        HMM_Vec2 diffC = HMM_Sub(t.c.XY, t.a.XY);
        float angle = atan2f(diffC.Y, diffC.X) - atan2f(diffB.Y, diffB.X);
        angle = geo_normalizeAngle(angle);
        // don't gap it if AC is more CW than AB (would cross a gap)
        // FIXME: skippies if this goes over another pt in the sketch
        if (!geo_floatGreaterEqual(angle, 0)) {
            continue;
        }

        // any zero area triangles we skip
        angle = fmodf(angle, HMM_AngleDeg(180));
        if (geo_floatEqual(angle, 0) || geo_floatEqual(angle, HMM_AngleDeg(180))) {
            culledFlags[ptIndexes[1]] = true; // FIXME: I haven't thought thru whether this is actually correct, but fuck it
            culledCount++;
            continue;
        } else if (geo_v3Equal(t.a, t.b) || geo_v3Equal(t.a, t.c)) {
            culledFlags[ptIndexes[1]] = true; // FIXME: I haven't thought thru whether this is actually correct, but fuck it
            culledCount++;
            continue;
        }

        geo_BSPTriListPushNew(out, list, t.a, t.b, t.c, f);
        culledFlags[ptIndexes[1]] = true;
        culledCount++;
        iterationsSinceTriWasAdded = 0;
    }
    return f;
}

typedef struct _skt_IntersectionEdge _skt_IntersectionEdge;
struct _skt_IntersectionEdge {
    _skt_Point* p1;
    _skt_Point* p2;
    _skt_IntersectionEdge* next;
    bool clean;
    bool culled;
};

// FIXME: does this function gauranteed crash on a malformed sketch??
// arena and scratch can be the same arena
// only fills out BSPTri list for a new mesh
// FIXME: exhaustive checks to make sure that there aren't any colinear & fully overlapped edges in the sketch before starting this
// FIXME: decide whether we are going to cope with T intersections here, and if not, add checks to make sure they don't exist on inputs
geo_Mesh skt_sketchTriangulate(const sk_Sketch* sketch, snz_Arena* arena, snz_Arena* scratch) {
    _skt_Point* firstPoint = NULL;
    _skt_IntersectionEdge* firstIntersectionEdge = NULL;
    { // intersections

        // import pts
        SNZ_ARENA_ARR_BEGIN(scratch, _skt_Point);
        int i = 0;
        for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
            p->indexIntoSketch = i;
            _skt_Point* pt = SNZ_ARENA_PUSH(scratch, _skt_Point);
            *pt = (_skt_Point){
                .pos = p->pos,
                .dbgIndex = i,
                .next = firstPoint, // storing as a LL as well because of appends happening down the road
            };
            firstPoint = pt;
            i++;
        }
        _skt_PointSlice initialPointsArr = SNZ_ARENA_ARR_END(scratch, _skt_Point);

        // import lines
        int edgeCount = 0;
        for (sk_Line* l = sketch->firstLine; l;l = l->next) {
            _skt_IntersectionEdge* e = SNZ_ARENA_PUSH(scratch, _skt_IntersectionEdge);
            *e = (_skt_IntersectionEdge){
                .p1 = &initialPointsArr.elems[l->p1->indexIntoSketch],
                .p2 = &initialPointsArr.elems[l->p2->indexIntoSketch],
                .next = firstIntersectionEdge,
            };
            firstIntersectionEdge = e;
            edgeCount++;
        }

        // printf("initial edges: \n");
        // for (_skt_IntersectionEdge* e = firstIntersectionEdge; e; e = e->next) {
        //     printf("Edge:\n");
        //     printf("\tA: %f, %f\n", e->p1->pos.X, e->p1->pos.Y);
        //     printf("\tB: %f, %f\n", e->p2->pos.X, e->p2->pos.Y);
        // }

        int cleanCount = 0;
        _skt_IntersectionEdge* edge = firstIntersectionEdge;
        while (cleanCount < edgeCount) {
            if (!edge->clean) {
                bool anyIntersection = false;
                for (_skt_IntersectionEdge* other = firstIntersectionEdge; other; other = other->next) {
                    if (other->clean || other == edge) {
                        continue;
                    }

                    float t = 0;
                    if (!_skt_lineLineIntersection(edge->p1->pos, edge->p2->pos, other->p1->pos, other->p2->pos, &t)) {
                        continue;
                    }

                    _skt_Point* newPt = SNZ_ARENA_PUSH(scratch, _skt_Point);
                    newPt->dbgIndex = (int)(uint64_t)newPt;
                    newPt->pos = HMM_Lerp(edge->p1->pos, t, edge->p2->pos);
                    newPt->next = firstPoint;
                    firstPoint = newPt;

                    // create new edges
                    _skt_IntersectionEdge* newEdges = SNZ_ARENA_PUSH_ARR(scratch, 2, _skt_IntersectionEdge);
                    edgeCount += 2;
                    newEdges[0].next = firstIntersectionEdge;
                    newEdges[1].next = &newEdges[0];
                    firstIntersectionEdge = &newEdges[1];

                    // swap around where things are pointing to form the new edges
                    newEdges[0].p1 = edge->p1;
                    newEdges[0].p2 = newPt;
                    newEdges[1].p1 = edge->p2;
                    newEdges[1].p2 = newPt;
                    edge->p1 = other->p1;
                    edge->p2 = newPt;
                    other->p1 = other->p2;
                    other->p2 = newPt;

                    anyIntersection = true;
                    break;
                }

                if (!anyIntersection) {
                    edge->clean = true;
                    cleanCount++;
                }
            } // end clean check

            edge = edge->next;
            if (edge == NULL) {
                edge = firstIntersectionEdge;
            }
        }
    }

    { // iteratively cull connections to pts with exactly one adj
        // FIXME: time complexity of this is pretty bad
        // profile to see if improving this via a edge 'neighbor changed flag' would be better on an avg sketch
        while (true) {
            bool anyCulled = false;
            for (_skt_Point* p = firstPoint; p; p = p->next) {
                int adjCount = 0;
                _skt_IntersectionEdge* lastEdge = NULL;
                for (_skt_IntersectionEdge* e = firstIntersectionEdge; e; e = e->next) {
                    if (e->culled) {
                        continue;
                    } else if (e->p1 == p || e->p2 == p) {
                        adjCount++;
                        lastEdge = e;
                    }
                }

                if (adjCount == 1) {
                    anyCulled = true;
                    lastEdge->culled = true;
                }
            }

            if (!anyCulled) {
                break;
            }
        }
    }

    { // adj. lists
        _skt_Point* newFirstPt = NULL;
        _skt_Point* next = NULL;
        for (_skt_Point* p = firstPoint; p; p = next) {
            next = p->next;

            SNZ_ARENA_ARR_BEGIN(scratch, _skt_Edge);
            for (_skt_IntersectionEdge* e = firstIntersectionEdge; e; e = e->next) {
                if (e->culled) {
                    continue;
                }

                int matchCount = (e->p1 == p) + (e->p2 == p);
                if (matchCount == 0) {
                    continue;
                }
                SNZ_ASSERT(matchCount == 1, "edge connecting to itself.");
                *SNZ_ARENA_PUSH(scratch, _skt_Edge) = (_skt_Edge){
                    .other = ((e->p1 == p) ? e->p2 : e->p1),
                    .traversed = false,
                };
            }
            p->edges = SNZ_ARENA_ARR_END(scratch, _skt_Edge);

            if (p->edges.count == 0) {
                continue;
            }
            SNZ_ASSERTF(p->edges.count >= 2, "Pt with only %lld adj.", p->edges.count);
            p->next = newFirstPt;
            newFirstPt = p;
        }

        firstPoint = newFirstPt;
    } // end generating adj. lists

    // { // dbg print adj lists in a readable way
    //     for (_skt_Point* p = firstPoint; p; p = p->next) {
    //         printf("Pt: %d\n", p->dbgIndex);
    //         for (int j = 0; j < p->edges.count; j++) {
    //             printf("\t%d\n", p->edges.elems[j].other->dbgIndex);
    //         }
    //     }
    //     printf("\n");
    // }

    _skt_Island* firstIsland = NULL;
    { // island marking / list gen
        for (_skt_Point* p = firstPoint; p; p = p->next) {
            if (p->island) {
                continue;
            }
            _skt_Island* island = SNZ_ARENA_PUSH(scratch, _skt_Island);
            island->next = firstIsland;
            firstIsland = island;
            _skt_markIsland(p, island, scratch);
        }
    }

    // vert loop gen
    while (true) { // FIXME: emergency cutoff
        _skt_Point* prevPt = NULL;
        _skt_Point* currentPt = NULL;
        { // find the seed for the next loop
            // FIXME: the check in the loop is probs bad for perf, but I don't feel like keeping track of the last one rn
            for (_skt_Point* p = firstPoint; p; p = p ? p->next : NULL) {
                for (int j = 0; j < p->edges.count; j++) {
                    _skt_Edge* edge = &p->edges.elems[j];
                    if (edge->traversed) {
                        continue;
                    }
                    edge->traversed = true;
                    prevPt = p;
                    currentPt = edge->other;

                    p = NULL; // break outer
                    break;
                }
            }
        }
        if (prevPt == NULL) {
            break; // break out of finding more loops, because every edge has been traversed both ways
        }
        _skt_Point* startPt = prevPt;

        float dir = 0;
        {
            HMM_Vec2 diff = HMM_Sub(currentPt->pos, startPt->pos);
            dir = atan2f(diff.Y, diff.X);
            if (dir < 0) {
                dir += HMM_AngleDeg(360);
            }
        }

        SNZ_ARENA_ARR_BEGIN(scratch, HMM_Vec2);
        *SNZ_ARENA_PUSH(scratch, HMM_Vec2) = currentPt->pos;

        while (currentPt != startPt) { // FIXME: emergency cutoff
            _skt_Edge* selected = NULL;
            float maxAngle = 0;
            float maxAngleDiff = -INFINITY;
            for (int i = 0; i < currentPt->edges.count; i++) {
                _skt_Edge* edge = &currentPt->edges.elems[i];
                if (edge->other == prevPt) {
                    continue;
                }
                HMM_Vec2 diff = HMM_Sub(edge->other->pos, currentPt->pos);
                float angle = atan2f(diff.Y, diff.X);
                float angleDiff = geo_normalizeAngle(angle - dir);

                // select the most CCW next point to go to
                if (angleDiff > maxAngleDiff) {
                    selected = edge;
                    maxAngleDiff = angleDiff;
                    maxAngle = angle;
                }
            }
            prevPt = currentPt;
            selected->traversed = true;
            currentPt = selected->other;
            dir = maxAngle;
            *SNZ_ARENA_PUSH(scratch, HMM_Vec2) = currentPt->pos;
        } // end finding points for a loop
        HMM_Vec2Slice loopPoints = SNZ_ARENA_ARR_END(scratch, HMM_Vec2);

        _skt_Island* island = startPt->island;
        SNZ_ASSERT(island != NULL, "null island for a vert loop.");
        _skt_VertLoop* loop = SNZ_ARENA_PUSH(scratch, _skt_VertLoop);
        *loop = (_skt_VertLoop){
            .pts = loopPoints,
        };

        float area = 0;
        for (int i = 0; i < loop->pts.count; i++) {
            HMM_Vec2 p0 = loop->pts.elems[i];
            HMM_Vec2 p1 = loop->pts.elems[(i + 1) % loop->pts.count];
            area += p0.X * p1.Y - p1.X * p0.Y;
        }
        area /= 2;
        SNZ_ASSERT(!geo_floatEqual(area, 0), "loop with zero area.");

        // less than zero indicates CW wrapping, which is the opposite of how loops should have wrapped
        // the only case that happens in is when it is a perimeter loop, so we skip inserting it here
        if (area < 0) {
            island->perimeterLoop = loop;
        } else {
            loop->next = island->firstLoop;
            island->firstLoop = loop;
        }
    } // end loop loop
    // all of vert loop gen

    // // Debug printing islands + vert loops
    // for (_skt_Island* island = firstIsland; island; island = island->next) {
    //     printf("Island:\n");
    //     printf("perimeter:\n");
    //     _skt_vertLoopPrint(island->perimeterLoop);
    //     int i = 0;
    //     for (_skt_VertLoop* l = island->firstLoop; l; l = l->next) {
    //         printf("%d:\n", i);
    //         _skt_vertLoopPrint(l);
    //         i++;
    //     }
    //     printf("\n");
    // }

    // // hole / seam gen // FIXME: make this work :)
    // for (_skt_Island* island = firstIsland; island; island = island->next) {
    //     for (_skt_Island* other = island->next; other; other = other->next) {
    //         if (!_skt_vertLoopContainsVertLoop(island->perimeterLoop, other->perimeterLoop)) {
    //             continue;
    //         }
    //         printf("island contains another !!!!\n");
    //         // _skt_Point* a = NULL;
    //         // _skt_Point* b = NULL;
    //         // findMeGoodPointsForASeam(island, other, &a, &b);
    //         // join(island, other);
    //     }
    // }

    // triangulation
    geo_BSPTriList tris = geo_BSPTriListInit();
    geo_MeshFace* firstFace = NULL;

    for (_skt_Island* island = firstIsland; island; island = island->next) {
        for (_skt_VertLoop* l = island->firstLoop; l; l = l->next) {
            geo_MeshFace* new = _skt_vertLoopToMeshFace(l, &tris, scratch, arena);
            new->next = firstFace;
            firstFace = new;
        }
    } // end island loop

    // move points and lines over to the mesh
    geo_MeshEdge* firstEdge = NULL;
    geo_MeshCornerSlice corners = { 0 };
    {
        for (_skt_IntersectionEdge* edge = firstIntersectionEdge; edge; edge = edge->next) {
            if (edge->culled) {
                continue;
            }
            geo_MeshEdge* e = SNZ_ARENA_PUSH(arena, geo_MeshEdge);
            geo_MeshEdgeSegment* segment = SNZ_ARENA_PUSH(arena, geo_MeshEdgeSegment);
            segment->a.XY = edge->p1->pos;
            segment->b.XY = edge->p2->pos;
            *e = (geo_MeshEdge){
                .next = firstEdge,
                .segments = (geo_MeshEdgeSegmentSlice) {
                    .count = 1,
                    .elems = segment,
                },
            };
            firstEdge = e;
        }

        SNZ_ARENA_ARR_BEGIN(arena, geo_MeshCorner);
        for (_skt_Point* p = firstPoint; p; p = p->next) {
            *SNZ_ARENA_PUSH(arena, geo_MeshCorner) = (geo_MeshCorner){
                .pos.XY = p->pos,
            };
        }
        corners = SNZ_ARENA_ARR_END(arena, geo_MeshCorner);
    }

    geo_Mesh out = (geo_Mesh){
        .firstFace = firstFace,
        .firstEdge = firstEdge,
        .corners = corners,
        .bspTris = tris,
    };
    return out;
}

void skt_tests() {
    snz_testPrintSection("sketch triangulation");

    {
        // FIXME: more tests for this
        float t = 0;
        _skt_lineLineIntersection(
            HMM_V2(-1, 0), HMM_V2(1, 0),
            HMM_V2(0, 1), HMM_V2(0, -1), &t);
        snz_testPrint(geo_floatEqual(t, .5), "line/line 0");
    }

    // FIXME: more tests for triangulating a vertloop
    // FIXME: many more tests + maybe a fuzzer for skt_sketchTriangulate
}
