#include "snooze.h"
#include "sketches2.h"

typedef struct _skt_Point _skt_Point;
typedef struct {
    // low and high decided based on pointer compare
    _skt_Point* lowPt;
    _skt_Point* highPt;
    int traverse; // see _skt_EdgeTraverse
} _skt_Edge;

typedef enum {
    _SKT_ET_NONE,
    _SKT_ET_LOW_FIRST,
    _SKT_ET_HIGH_FIRST,
} _skt_EdgeTraverse;

SNZ_SLICE(_skt_Edge);
SNZ_SLICE_NAMED(_skt_Edge*, _skt_EdgePtrSlice);

typedef struct _skt_Island _skt_Island;

struct _skt_Point {
    _skt_EdgePtrSlice adjacent;
    HMM_Vec2 pos;
    _skt_Island* island;
};

SNZ_SLICE(_skt_Point);
SNZ_SLICE_NAMED(_skt_Point*, _skt_PointPtrSlice);

static _skt_Point* _sk_otherInTriangulationEdge(_skt_Edge* e, _skt_Point* pt) {
    return e->lowPt == pt ? e->highPt : e->lowPt;
}

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

void _skt_MarkIsland(_skt_Point* pt, _skt_Island* island, snz_Arena* arena) {
    pt->island = island;
    for (int i = 0; i < pt->adjacent.count; i++) {
        _skt_Edge* edge = pt->adjacent.elems[i];
        _skt_Point* other = _sk_otherInTriangulationEdge(edge, pt);
        if (other->island) {
            continue;
        }
        _skt_MarkIsland(other, island, arena);
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

// FIXME: does this function gauranteed crash on a malformed sketch??
geo_Mesh sk_sketchTriangulate(const sk_Sketch* sketch, snz_Arena* arena, snz_Arena* scratch) {
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

    _skt_EdgeSlice edges = { 0 };
    { // adj. lists
        SNZ_ARENA_ARR_BEGIN(scratch, _skt_Edge);
        for (sk_Line* l = sketch->firstLine; l; l = l->next) {
            _skt_Point* p1 = &points.elems[l->p1->indexIntoSketch];
            _skt_Point* p2 = &points.elems[l->p2->indexIntoSketch];
            *SNZ_ARENA_PUSH(scratch, _skt_Edge) = (_skt_Edge){
                .highPt = SNZ_MAX(p1, p2),
                .lowPt = SNZ_MIN(p1, p2),
            };
        }
        edges = SNZ_ARENA_ARR_END(scratch, _skt_Edge);

        for (int ptIdx = 0; ptIdx < points.count; ptIdx++) {
            _skt_Point* p = &points.elems[ptIdx];
            SNZ_ARENA_ARR_BEGIN(scratch, _skt_Edge*);
            for (int edgeIdx = 0; edgeIdx < edges.count; edgeIdx++) {
                _skt_Edge* e = &edges.elems[edgeIdx];
                if (e->lowPt == p || e->highPt == p) {
                    *SNZ_ARENA_PUSH(scratch, _skt_Edge*) = e;
                }
            }
            p->adjacent = SNZ_ARENA_ARR_END_NAMED(scratch, _skt_Edge*, _skt_EdgePtrSlice);
            SNZ_ASSERTF(p->adjacent.count >= 2, "point had only %lld adjacents.", p->adjacent.count);
        }
    } // end generating adj. lists

    _skt_Island* firstIsland;
    { // island marking / list gen
        for (int i = 0; i < points.count; i++) {
            _skt_Point* pt = &points.elems[i];
            if (pt->island) {
                continue;
            }
            _skt_Island* island = SNZ_ARENA_PUSH(scratch, _skt_Island);
            island->next = firstIsland;
            firstIsland = island;
            _skt_MarkIsland(pt, island, scratch);
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
                for (int j = 0; j < p->adjacent.count; j++) {
                    _skt_Edge* e = p->adjacent.elems[j];
                    int traverseMask = (p == e->lowPt) ? _SKT_ET_LOW_FIRST : _SKT_ET_HIGH_FIRST;
                    if (e->traverse & traverseMask) {
                        continue;
                    }
                    e->traverse &= traverseMask;
                    prevPt = p;
                    currentPt = _sk_otherInTriangulationEdge(e, p);

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
            for (int i = 0; i < currentPt->adjacent.count; i++) {
                _skt_Edge* edge = currentPt->adjacent.elems[i];
                _skt_Point* other = _sk_otherInTriangulationEdge(edge, currentPt);
                if (other == prevPt) {
                    continue;
                }
                HMM_Vec2 diff = HMM_Sub(other->pos, currentPt->pos);
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
            selected->traverse &= (currentPt == selected->lowPt ? _SKT_ET_LOW_FIRST : _SKT_ET_HIGH_FIRST);
            currentPt = _sk_otherInTriangulationEdge(selected, currentPt);
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
    }

    // hole / seam gen
    for (_skt_Island* island = firstIsland; island; island = island->next) {
        for (_skt_Island* other = island->next; other; other = other->next) {
            if (!_skt_vertLoopContainsVertLoop(island->perimeterLoop, other->perimeterLoop)) {
                continue;
            }
            printf("island contains another !!!!\n");
            // _skt_Point* a = NULL;
            // _skt_Point* b = NULL;
            // findMeGoodPointsForASeam(island, other, &a, &b);
            // join(island, other);
        }
    }

    // triangulation
    {
    }

    // geo_Mesh out = { 0 };
    // out.bspTris = ;
    // out.firstFace = ;
    return (geo_Mesh) { 0 };
}

void skt_tests() {
    snz_testPrintSection("sketch triangulation");
}