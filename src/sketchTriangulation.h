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
    _skt_EdgeSlice edges;
    HMM_Vec2 pos;
    _skt_Island* island;
    int dbgIndex;
};

typedef struct _skt_VertLoop _skt_VertLoop;
struct _skt_VertLoop {
    _skt_VertLoop* next;
    HMM_Vec2Slice pts;
    float area;
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

bool _skt_lineLineIntersection(HMM_Vec2 l1a, HMM_Vec2 l1b, HMM_Vec2 l2a, HMM_Vec2 l2b) {
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
    if (!geo_floatGreaterEqual(u, 0) || !geo_floatLessEqual(u, 1)) {
        return false; // past bounds on one line, doesn't trip on ~equal to ends
    }

    float v = (x1 - x2) * (y1 - y3) - (y1 - y2) * (x1 - x3);
    v /= denom;
    if (!geo_floatGreaterEqual(v, 0) || !geo_floatLessEqual(v, 1)) {
        return false;
    }

    return true;
}

bool _skt_vertLoopContainsPoint(_skt_VertLoop* l, HMM_Vec2 pt) {
    int hitCount = 0;
    HMM_Vec2 rayOrigin = pt;
    HMM_Vec2 rayOther = HMM_Add(pt, HMM_V2(100000, 0)); // FIXME: maybe this fails in insane cases, but INF i haven't tested so this is just gonna work for now.
    for (int i = 0; i < l->pts.count; i++) {
        HMM_Vec2 l1a = l->pts.elems[i];
        HMM_Vec2 l1b = l->pts.elems[(i + 1) % l->pts.count];
        if (_skt_lineLineIntersection(rayOrigin, rayOther, l1a, l1b)) {
            hitCount++;
        }
    }
    // point must have been inside if it crosses an odd number of times
    return (hitCount % 2) == 1;
}

// checks only that B is inside of A
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
            if (!_skt_lineLineIntersection(l1a, l1b, l2a, l2b)) {
                return false;
            }
        }
    }
    return true;
}

static void _skt_vertLoopPrint(_skt_VertLoop* l) {
    for (int i = 0; i < l->pts.count; i++) {
        HMM_Vec2 p = l->pts.elems[i];
        printf("\t%.2f, %.2f\n", p.X, p.Y);
    }
}

static geo_MeshFace* _skt_vertLoopToMeshFace(_skt_VertLoop* l, geo_BSPTriList* list, snz_Arena* scratch, snz_Arena* out) {
    geo_MeshFace* f = SNZ_ARENA_PUSH(out, geo_MeshFace);

    // FIXME: move this data to be inline with the vert loop points // actually also profile if that is worth doing
    bool* culledFlags = SNZ_ARENA_PUSH_ARR(scratch, l->pts.count, bool);
    int culledCount = 0;

    for (int startIdx = 0; true; startIdx++) {
        startIdx %= l->pts.count;

        if (culledCount >= l->pts.count - 2) {
            break;
        } else if (culledFlags[startIdx]) {
            continue;
        }

        int ptIndexes[3] = { startIdx, 0, 0 };
        for (int i = 1; i < 3; i++) {
            int idx = ptIndexes[i - i];
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
            t.elems[i].XY = l->pts.elems[i];
        }

        geo_BSPTriListPushNew(out, list, t.a, t.b, t.c, f);
        culledFlags[ptIndexes[1]] = true;
        culledCount++;
    }

    return f;
}

// FIXME: does this function gauranteed crash on a malformed sketch??
geo_Mesh skt_sketchTriangulate(const sk_Sketch* sketch, snz_Arena* arena, snz_Arena* scratch, PoolAlloc* pool) {
    assert(arena || !arena);
    // // arcs -> lines
    // lines -> intersections
    // sketch -> graph
    // graph -> vert loops (don't forget holes!!)
    // vert loops -> tris
    // done!!

    _skt_PointSlice points = { 0 };
    { // imports
        SNZ_ARENA_ARR_BEGIN(scratch, _skt_Point);
        int i = 0;
        for (sk_Point* p = sketch->firstPoint; p; (p = p->next, i++)) {
            p->indexIntoSketch = i;
            _skt_Point* new = SNZ_ARENA_PUSH(scratch, _skt_Point);
            new->pos = p->pos;
            new->dbgIndex = i;
        }
        points = SNZ_ARENA_ARR_END(scratch, _skt_Point);
    }

    if (!points.count) {
        return (geo_Mesh) { 0 };
    }

    { // intersections
    }

    { // iteratively cull pts with one or none adj.
    }

    { // adj. lists
        for (int i = 0; i < points.count; i++) {
            _skt_Point* p = &points.elems[i];
            SNZ_ARENA_ARR_BEGIN(scratch, _skt_Edge);
            for (sk_Line* line = sketch->firstLine; line; line = line->next) {
                bool p1Matches = line->p1->indexIntoSketch == i;
                bool p2Matches = line->p2->indexIntoSketch == i;
                if (!p1Matches && !p2Matches) {
                    continue;
                }

                int idx = (p1Matches ? line->p2->indexIntoSketch : line->p1->indexIntoSketch);
                *SNZ_ARENA_PUSH(scratch, _skt_Edge) = (_skt_Edge){
                    .other = &points.elems[idx],
                    .traversed = false,
                };
            }
            p->edges = SNZ_ARENA_ARR_END(scratch, _skt_Edge);
            SNZ_ASSERTF(p->edges.count >= 2, "point had only %lld adjacents.", p->edges.count);
        }
    } // end generating adj. lists

    { // dbg print adj lists in a readable way
        for (int i = 0; i < points.count; i++) {
            _skt_Point* p = &points.elems[i];
            printf("Pt: %d\n", p->dbgIndex);
            for (int j = 0; j < p->edges.count; j++) {
                printf("\t%d\n", p->edges.elems[j].other->dbgIndex);
            }
        }
        printf("\n");
    }

    _skt_Island* firstIsland = NULL;
    { // island marking / list gen
        for (int i = 0; i < points.count; i++) {
            _skt_Point* pt = &points.elems[i];
            if (pt->island) {
                continue;
            }
            _skt_Island* island = SNZ_ARENA_PUSH(scratch, _skt_Island);
            island->next = firstIsland;
            firstIsland = island;
            _skt_markIsland(pt, island, scratch);
        }
    }

    SNZ_ASSERTF(points.count > 0, "points count was: %lld", points.count);

    // vert loop gen
    while (true) { // FIXME: emergency cutoff
        _skt_Point* prevPt = NULL;
        _skt_Point* currentPt = NULL;
        { // find the seed for the next loop
            for (int i = 0; i < points.count; i++) {
                _skt_Point* p = &points.elems[i];
                for (int j = 0; j < p->edges.count; j++) {
                    _skt_Edge* edge = &p->edges.elems[j];
                    if (edge->traversed) {
                        continue;
                    }
                    edge->traversed = true;
                    prevPt = p;
                    currentPt = edge->other;

                    i = points.count; // break outer
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
            float maxAngle = -INFINITY;
            for (int i = 0; i < currentPt->edges.count; i++) {
                _skt_Edge* edge = &currentPt->edges.elems[i];
                if (edge->other == prevPt) {
                    continue;
                }
                HMM_Vec2 diff = HMM_Sub(edge->other->pos, currentPt->pos);
                float angle = dir - atan2f(diff.Y, diff.X);
                while (angle < 0) {
                    angle += HMM_AngleDeg(360);
                }
                while (angle > 360) {
                    angle -= HMM_AngleDeg(360);
                }

                // select the most CCW next point to go to
                if (angle > maxAngle) {
                    selected = edge;
                    maxAngle = angle;
                }
            }
            prevPt = currentPt;
            selected->traversed = true;
            currentPt = selected->other;
            *SNZ_ARENA_PUSH(scratch, HMM_Vec2) = currentPt->pos;
        } // end finding points for a loop
        HMM_Vec2Slice loopPoints = SNZ_ARENA_ARR_END(scratch, HMM_Vec2);

        _skt_Island* island = startPt->island;
        SNZ_ASSERT(island != NULL, "null island for a vert loop.");
        _skt_VertLoop* loop = SNZ_ARENA_PUSH(scratch, _skt_VertLoop);
        *loop = (_skt_VertLoop){
            .pts = loopPoints,
            .next = island->firstLoop,
        };
        island->firstLoop = loop;

        for (int i = 0; i < loop->pts.count; i++) {
            HMM_Vec2 p0 = loop->pts.elems[i];
            HMM_Vec2 p1 = loop->pts.elems[(i + 1) % loop->pts.count];
            loop->area += p0.X * p1.Y - p1.X * p0.Y;
        }
        loop->area = fabsf(loop->area) / 2;
    } // end loop loop
    // all of vert loop gen

    // finding & removing the perimeter loop that happens to get generated per island (which has the largest area)
    for (_skt_Island* island = firstIsland; island; island = island->next) {
        _skt_VertLoop* prevLoop = NULL;
        _skt_VertLoop* maxLoop = island->firstLoop;
        for (_skt_VertLoop* loop = island->firstLoop; loop; loop = loop->next) {
            if (loop->area > maxLoop->area) {
                maxLoop = loop;
            }
            prevLoop = loop;
        }

        if (prevLoop) {
            prevLoop->next = maxLoop->next;
        } else {
            island->firstLoop = maxLoop->next;
        }
        maxLoop->next = NULL;
        island->perimeterLoop = maxLoop;


        // Debug printing islands
        printf("Island:\n");
        printf("perimeter:\n");
        _skt_vertLoopPrint(island->perimeterLoop);
        int i = 0;
        for (_skt_VertLoop* l = island->firstLoop; l; l = l->next) {
            printf("%d:\n", i);
            _skt_vertLoopPrint(l);
            i++;
        }
        printf("\n");
    }

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

    geo_Mesh out = (geo_Mesh){
        .firstFace = firstFace,
        .bspTris = tris,
        .renderMesh = geo_BSPTriListToRenderMesh(tris, scratch),
    };
    geo_BSPTriListToFaceTris(pool, &out);
    return out;
}

void skt_tests() {
    snz_testPrintSection("sketch triangulation");
}