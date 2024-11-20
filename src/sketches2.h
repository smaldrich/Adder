#pragma once

#include <stdbool.h>

#include "HMM/HandmadeMath.h"
#include "csg.h"
#include "snooze.h"

typedef enum {
    SK_MK_ANY,
    SK_MK_NONE,
    SK_MK_CIRCLE,
    SK_MK_LINE,
    SK_MK_TWO_POINTS,
    SK_MK_POINT,
} sk_ManifoldKind;

typedef struct {
    union {
        struct {
            HMM_Vec2 origin;
            float radius;
        } circle;
        struct {
            HMM_Vec2 origin;
            HMM_Vec2 direction;
        } line;
        struct {
            HMM_Vec2 a;
            HMM_Vec2 b;
        } twoPoints;
        HMM_Vec2 point;
    };
    sk_ManifoldKind kind;
} sk_Manifold;

typedef struct sk_Point sk_Point;
struct sk_Point {
    HMM_Vec2 pos;
    sk_Point* next;
    sk_Manifold manifold;
    bool solved;
};

typedef struct sk_Line sk_Line;
struct sk_Line {
    sk_Point* p1;
    sk_Point* p2;
    sk_Line* next;
    bool angleApplied;
    bool angleSolved;
    float angle;
};

typedef enum {
    SK_CK_DISTANCE,
    SK_CK_ANGLE,
    // FIXME: equals constraint
} sk_ConstraintKind;

typedef struct sk_Constraint sk_Constraint;
struct sk_Constraint {
    sk_ConstraintKind kind;
    sk_Line* line1;
    sk_Line* line2;
    float value;
    sk_Constraint* nextAllocated;
    sk_Constraint* nextUnsolved;
};
// FIXME: opaque types for all of these

typedef struct {
    sk_Point* firstPoint;
    sk_Line* firstLine;
    sk_Constraint* firstConstraint;

    sk_Constraint* firstUnsolvedConstraint;
} sk_Sketch;

/*

sketches:
a collection of points, lines and constraints

a failed add should highlight every other constraint in the chain to the origin that is preventing the add?
favor angles?
cause otherwise it's just every constrain in the sketch
Fusions 'no response' aesthetic is really fuckin annoying

also a better way of visualizing constraints
*/
sk_Sketch sk_sketchInit() {
    return (sk_Sketch) {
        .firstPoint = NULL,
            .firstLine = NULL,
            .firstConstraint = NULL,
    };
}

sk_Point* sk_sketchAddPoint(sk_Sketch* sketch, snz_Arena* arena, HMM_Vec2 pos) {
    sk_Point* p = SNZ_ARENA_PUSH(arena, sk_Point);
    *p = (sk_Point){
        .pos = pos,
        .next = sketch->firstPoint,
    };
    sketch->firstPoint = p;
    return p;
}

sk_Line* sk_sketchAddLine(sk_Sketch* sketch, snz_Arena* arena, sk_Point* p1, sk_Point* p2) {
    sk_Line* l = SNZ_ARENA_PUSH(arena, sk_Line);
    *l = (sk_Line){
        .p1 = p1,
        .p2 = p2,
        .next = sketch->firstLine,
    };
    sketch->firstLine = l;
    return l;
}

sk_Constraint* sk_sketchAddConstraintDistance(sk_Sketch* sketch, snz_Arena* arena, sk_Line* l, float length) {
    sk_Constraint* c = SNZ_ARENA_PUSH(arena, sk_Constraint);
    *c = (sk_Constraint){
        .kind = SK_CK_DISTANCE,
        .line1 = l,
        .value = length,
        .nextAllocated = sketch->firstConstraint,
    };
    sketch->firstConstraint = c;
    return c;
}

sk_Constraint* sk_sketchAddConstraintAngle(sk_Sketch* sketch, snz_Arena* arena, sk_Line* line1, sk_Line* line2, float angle) {
    sk_Constraint* c = SNZ_ARENA_PUSH(arena, sk_Constraint);
    *c = (sk_Constraint){
        .kind = SK_CK_ANGLE,
        .line1 = line1,
        .line2 = line2,
        .value = angle,
        .nextAllocated = sketch->firstConstraint,
    };
    sketch->firstConstraint = c;
    return c;
}

// FIXME: this
// FIXME: where is sketch validation happening
// void sk_sketchRemovePointAndDeps(sk_Sketch* sketch, sk_Point* point) {
// }

// void sk_sketchRemoveLineAndDeps(sk_Sketch* sketch, sk_Line* line) {
// }

// void sk_sketchRemoveConstraint(sk_Sketch* sketch, sk_Constraint* constraint) {
// }

// FIXME: one big geometry file, not csg sketches and geo
// FIXME: point/twopoint vs X cases :)))
static sk_Manifold _sk_manifoldJoin(sk_Manifold a, sk_Manifold b) {
    int lineCount = (a.kind == SK_MK_LINE) + (b.kind == SK_MK_LINE);
    int circleCount = (a.kind == SK_MK_CIRCLE) + (b.kind == SK_MK_CIRCLE);
    int anyCount = (a.kind == SK_MK_ANY) + (b.kind == SK_MK_ANY);

    if (a.kind == SK_MK_NONE || b.kind == SK_MK_NONE) {
        return (sk_Manifold) { .kind = SK_MK_NONE };
    } else if (anyCount >= 1) {
        return a.kind == SK_MK_ANY ? b : a;
    } else if (lineCount == 1 && circleCount == 1) {
        // stolen from: https://paulbourke.net/geometry/circlesphere/
        sk_Manifold line = a.kind == SK_MK_LINE ? a : b;
        sk_Manifold circle = a.kind == SK_MK_LINE ? b : a;

        HMM_Vec2 lineOther = HMM_Add(line.line.origin, line.line.direction);
        float a = HMM_LenSqr(HMM_Sub(lineOther, line.line.origin));

        float x1 = line.line.origin.X;
        float x2 = lineOther.X;
        float x3 = circle.circle.origin.X;
        float y1 = line.line.origin.Y;
        float y2 = lineOther.Y;
        float y3 = circle.circle.origin.Y;

        float b = 2 * ((x2 - x1) * (x1 - x3) + (y2 - y1) * (y1 - y3));
        float c = (x3 * x3) + (y3 * y3) + (x1 * x1) + (y1 * y1) - 2 * (x3 * x1 + y3 * y1) - HMM_SQUARE(circle.circle.radius);

        float disriminant = (b * b) - (4 * a * c);
        if (csg_floatZero(disriminant)) {
            float u = -b / 2 * a;
            sk_Manifold out = (sk_Manifold){
                .kind = SK_MK_POINT,
                .point = HMM_Lerp(line.line.origin, u, lineOther),
            };
            return out;
        } else if (disriminant < 0) {
            return (sk_Manifold) { .kind = SK_MK_NONE };
        }

        disriminant = HMM_SqrtF(disriminant);
        float u1 = (-b + disriminant) / (2 * a);
        float u2 = (-b - disriminant) / (2 * a);
        HMM_Vec2 p1 = HMM_Lerp(line.line.origin, u1, lineOther);
        HMM_Vec2 p2 = HMM_Lerp(line.line.origin, u2, lineOther);
        int positiveCount = csg_floatGreaterEqual(u1, 0) + csg_floatGreaterEqual(u2, 0);

        if (positiveCount == 0) {
            return (sk_Manifold) { .kind = SK_MK_NONE };
        } else if (positiveCount == 1) {
            sk_Manifold out = (sk_Manifold){
                .kind = SK_MK_POINT,
                .point = csg_floatGreaterEqual(u1, 0) ? p1 : p2,
            };
            return out;
        } else if (positiveCount == 2) {
            sk_Manifold out = (sk_Manifold){
                .kind = SK_MK_TWO_POINTS,
                .twoPoints.a = p1,
                .twoPoints.b = p2,
            };
            return out;
        }
        SNZ_ASSERTF(false, "unreachable case: %d", positiveCount);
        return (sk_Manifold) { .kind = SK_MK_NONE };
    } else if (lineCount == 2) {
        if (csg_v2Equal(a.line.origin, b.line.origin) && csg_v2Equal(a.line.direction, b.line.direction)) {
            return a; // coincident, return the starting manifold
        }

        // stolen: https://en.wikipedia.org/wiki/Line%E2%80%93line_intersection
        HMM_Vec2 aOther = HMM_Add(a.line.origin, a.line.direction);
        HMM_Vec2 bOther = HMM_Add(b.line.origin, b.line.direction);
        float x1 = a.line.origin.X;
        float x2 = aOther.X;
        float y1 = a.line.origin.Y;
        float y2 = aOther.Y;
        float x3 = b.line.origin.X;
        float x4 = bOther.X;
        float y3 = b.line.origin.Y;
        float y4 = bOther.Y;

        float num = (x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4);
        float denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
        if (csg_floatZero(denom)) {
            return (sk_Manifold) { .kind = SK_MK_NONE };
        }

        sk_Manifold out = (sk_Manifold){
            .kind = SK_MK_POINT,
            .point = HMM_Lerp(a.line.origin, num / denom, aOther),
        };
        return out;
    } else if (circleCount == 2) {
        // Circle circle intersection alg, stolen from here: https://stackoverflow.com/questions/3349125/circle-circle-intersection-points
        HMM_Vec2 diff = HMM_Sub(b.circle.origin, a.circle.origin);
        float d = HMM_Len(diff);
        if (csg_floatZero(d) && csg_floatEqual(a.circle.radius, b.circle.radius)) {
            return a;  // circles are coincedent, it's ok to return either
        } else if (csg_floatEqual(d, a.circle.radius + b.circle.radius)) {
            // Circles are just touching, return the single point
            HMM_Vec2 pt = HMM_Mul(HMM_Norm(diff), a.circle.radius);
            return (sk_Manifold) { .kind = SK_MK_POINT, .point = pt };
        } else if (d > (a.circle.radius + b.circle.radius)) {
            // Circles are outside eachother, no points
            return (sk_Manifold) { .kind = SK_MK_NONE };
        } else if (d < fabsf(a.circle.radius - b.circle.radius)) {
            // Circles are within eachother, no points
            return (sk_Manifold) { .kind = SK_MK_NONE };
        }
        // two points of intersection

        // called a in the alg, renamed to not shadow the circle
        float k = ((a.circle.radius * a.circle.radius) - (b.circle.radius * b.circle.radius) + (d * d)) / (2 * d);
        float h = HMM_SqrtF(a.circle.radius * a.circle.radius - k * k);
        HMM_Vec2 x = HMM_Mul(HMM_Norm(diff), k);
        HMM_Vec2 y = HMM_Mul(HMM_Norm(HMM_V2(-diff.Y, diff.X)), h);
        sk_Manifold out = (sk_Manifold){
            .kind = SK_MK_TWO_POINTS,
            .twoPoints.a = HMM_Add(a.circle.origin, HMM_Add(x, y)),
            .twoPoints.b = HMM_Add(a.circle.origin, HMM_Sub(x, y)),
        };
        return out;
    } else {
        return (sk_Manifold) { .kind = SK_MK_NONE };
    }
    SNZ_ASSERTF(false, "Unreachable manifold join: Line count: %d, Any count: %d, Circle count: %d", lineCount, anyCount, circleCount);
}

static bool _sk_manifoldEq(sk_Manifold a, sk_Manifold b) {
    if (a.kind != b.kind) {
        return false;
    }

    bool out = true;
    if (a.kind == SK_MK_NONE) {
        return true;
    } else if (a.kind == SK_MK_ANY) {
        return true;
    } else if (a.kind == SK_MK_POINT) {
        out &= csg_v2Equal(a.point, b.point);
    } else if (a.kind == SK_MK_TWO_POINTS) {
        bool eq1 = csg_v2Equal(a.twoPoints.a, b.twoPoints.a);
        eq1 &= csg_v2Equal(a.twoPoints.b, b.twoPoints.b);
        bool eq2 = csg_v2Equal(a.twoPoints.a, b.twoPoints.b);
        eq2 &= csg_v2Equal(a.twoPoints.b, b.twoPoints.a);

        out &= (eq1 || eq2);
    } else if (a.kind == SK_MK_LINE) {
        out &= csg_v2Equal(a.line.origin, b.line.origin);
        out &= csg_v2Equal(a.line.direction, b.line.direction);
    } else if (a.kind == SK_MK_CIRCLE) {
        out &= csg_v2Equal(a.circle.origin, b.circle.origin);
        out &= csg_floatEqual(a.circle.radius, b.circle.radius);
    } else {
        SNZ_ASSERTF(false, "Unreachable case, kind was: %d", a.kind);
    }
    return out;
}

// return indicates whether sketch was solved completely
void sk_sketchSolve(sk_Sketch* sketch, sk_Point* originPt, sk_Line* originLine, float originLineAngle) {
    int64_t sketchPointCount = 0;
    int64_t solvedPointCount = 1;

    {  // RESET SOLVE DEPENDENT VARIABLES IN THE SKETCH
        for (sk_Point* point = sketch->firstPoint; point; point = point->next) {
            point->manifold = (sk_Manifold){ .kind = SK_MK_ANY };
            sketchPointCount++;
        }

        for (sk_Line* line = sketch->firstLine; line; line = line->next) {
            line->angle = 0;
            line->angleSolved = false;
        }

        sketch->firstUnsolvedConstraint = NULL;
        for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
            c->nextUnsolved = sketch->firstUnsolvedConstraint;
            sketch->firstUnsolvedConstraint = c;
        }

        originLine->angle = originLineAngle;
        originLine->angleSolved = true;
        originPt->solved = true;
        originPt->manifold = (sk_Manifold){
            .kind = SK_MK_POINT,
            .point = originPt->pos,
        };
    }

    while (true) {  // FIXME: cutoff
        bool anySolved = false;

        sk_Constraint* prevConstraint = NULL;
        for (sk_Constraint* c = sketch->firstUnsolvedConstraint; c; c = c->nextUnsolved) {
            bool applied = false;
            if (c->kind == SK_CK_DISTANCE) {
                sk_Point* p1 = c->line1->p1;
                sk_Point* p2 = c->line1->p2;
                int solvedCount = c->line1->p1->solved + c->line1->p2->solved;
                if (solvedCount == 1) {
                    sk_Point* fixed = p1->solved ? p1 : p2;
                    sk_Point* variable = p1->solved ? p2 : p1;
                    sk_Manifold m = (sk_Manifold){
                        .kind = SK_MK_CIRCLE,
                        .circle.origin = fixed->pos,
                        .circle.radius = c->value,
                    };
                    variable->manifold = _sk_manifoldJoin(variable->manifold, m);
                    SNZ_ASSERT(variable->manifold.kind != SK_MK_NONE, "OVERCONSTRAINED!!");  // FIXME: remove
                    applied = true;
                }
            } else if (c->kind == SK_CK_ANGLE) {
                if (c->line1->angleSolved && !c->line2->angleSolved) {
                    // c's value meaning a CW rotation from 1 to 2
                    // FIXME: this is bad? document/express better in code?
                    c->line2->angle = c->line1->angle + c->value;
                    c->line2->angleSolved = true;
                    applied = true;
                } else if (c->line2->angleSolved && !c->line1->angleSolved) {
                    c->line1->angle = c->line2->angle - c->value;
                    c->line1->angleSolved = true;
                    applied = true;
                }
            }

            if (applied) {
                anySolved = true;
                if (!prevConstraint) {
                    sketch->firstUnsolvedConstraint = c->nextUnsolved;
                } else {
                    prevConstraint->nextUnsolved = c->nextUnsolved;
                }
            } else {
                prevConstraint = c;
            }
        }  // end manifold join/angle propagation loop

        for (sk_Line* line = sketch->firstLine; line; line = line->next) {
            int ptSolvedCount = line->p1->solved + line->p2->solved;
            if (!line->angleSolved && ptSolvedCount == 2) {
                HMM_Vec2 diff = HMM_Sub(line->p2->pos, line->p1->pos);
                line->angle = atan2f(diff.Y, diff.X);  // FIXME: orientation of lines can be wrong and then this breaks things?
                line->angleSolved = true;
                anySolved = true;
            } else if (line->angleSolved && ptSolvedCount == 1) {
                if (line->angleApplied) {
                    continue;
                }

                sk_Point* fixed = line->p1->solved ? line->p1 : line->p2;
                sk_Point* variable = line->p1->solved ? line->p2 : line->p1;
                sk_Manifold m = (sk_Manifold){
                    .kind = SK_MK_LINE,
                    .line.origin = fixed->pos,
                    .line.direction = HMM_RotateV2(HMM_V2(1, 0), line->angle),
                };
                sk_Manifold startManifold = variable->manifold;
                variable->manifold = _sk_manifoldJoin(startManifold, m);
                SNZ_ASSERT(variable->manifold.kind != SK_MK_NONE, "OVERCONSTRAINED!!");  // FIXME: remove
                line->angleApplied = true;
                anySolved = true;
            }
        }

        for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
            if (p->solved) {
                continue;
            } else if (p->manifold.kind == SK_MK_POINT) {
                p->pos = p->manifold.point;
                p->solved = true;
                solvedPointCount++;
                anySolved = true;
            } else if (p->manifold.kind == SK_MK_TWO_POINTS) {
                HMM_Vec2 p1 = p->manifold.twoPoints.a;
                float d1 = HMM_Len(HMM_Sub(p1, p->pos));
                HMM_Vec2 p2 = p->manifold.twoPoints.b;
                float d2 = HMM_Len(HMM_Sub(p2, p->pos));
                p->pos = (d1 < d2) ? p1 : p2;
                p->solved = true;
                solvedPointCount++;
                anySolved = true;
                // FIXME: weird things happen often here with >90 degree angles solving the wrong way. Find a solution.
            }
        }

        if (solvedPointCount == sketchPointCount) {
            return;  // skip implicit solving when everything is good
        } else if (!anySolved) {
            break;
        }
    }  // end solve loop

    // for (int i = 0; i < 1000; i++) {
    //     for (sk_Constraint* c = sketch->firstUnsolvedConstraint; c; c = c->nextUnsolved) {
    //         if (c->kind == SK_CK_ANGLE) {
    //             float diff = ;
    //         } else if (c->kind == SK_CK_DISTANCE) {
    //             HMM_Vec2 diff = HMM_Sub(c->line1->p2->pos, c->line1->p1->pos);
    //             float length = HMM_Len(diff);
    //             HMM_Vec2 midpt = HMM_DivV2F(HMM_Add(c->line1->p2->pos, c->line1->p1->pos), 2);

    //             if (!c->line1->p1->solved) {
    //                 c->line1->p1->pos = HMM_Sub(midpt, HMM_Mul(diff, c->value));
    //             }
    //             if (!c->line1->p2->solved) {
    //                 c->line1->p2->pos = HMM_Add(midpt, HMM_Mul(diff, c->value));
    //             }
    //         }  // end constraint kind switch
    //     }  // end inplicit solve loop
    // }  // end iteration loop
}

void sk_tests() {
    snz_testPrintSection("sketch");

    {  // MANIFOLD JOIN CASES
        sk_Manifold out = _sk_manifoldJoin(
            (sk_Manifold) {
            .kind = SK_MK_LINE,
                .line.origin = HMM_V2(0, 0),
                .line.direction = HMM_V2(1, 0),
        },
            (sk_Manifold) {
            .kind = SK_MK_LINE,
                .line.origin = HMM_V2(2, 2),
                .line.direction = HMM_V2(0, -1),
        });
        sk_Manifold comp = (sk_Manifold){
            .kind = SK_MK_POINT,
            .point = HMM_V2(2, 0),
        };
        snz_testPrint(_sk_manifoldEq(out, comp), "line/line manifold join");

        out = _sk_manifoldJoin(
            (sk_Manifold) {
            .kind = SK_MK_LINE,
                .line.origin = HMM_V2(10, 10),
                .line.direction = HMM_V2(0, 1),
        },
            (sk_Manifold) {
            .kind = SK_MK_LINE,
                .line.origin = HMM_V2(11, 1),
                .line.direction = HMM_V2(0, 1),
        });
        snz_testPrint(out.kind == SK_MK_NONE, "parallel line manifold join");

        comp = (sk_Manifold){
            .kind = SK_MK_LINE,
            .line.origin = HMM_V2(0, 0),
            .line.direction = HMM_V2(1, 1),
        };
        out = _sk_manifoldJoin((sk_Manifold) { .kind = SK_MK_ANY }, comp);
        snz_testPrint(_sk_manifoldEq(comp, out), "line/any manifold join");
        out = _sk_manifoldJoin(comp, (sk_Manifold) { .kind = SK_MK_ANY });
        snz_testPrint(_sk_manifoldEq(comp, out), "line/any manifold join 2");

        out = _sk_manifoldJoin(
            (sk_Manifold) {
            .kind = SK_MK_CIRCLE,
                .circle.origin = HMM_V2(0, 0),
                .circle.radius = 3,
        },
            (sk_Manifold) {
            .kind = SK_MK_CIRCLE,
                .circle.origin = HMM_V2(1, 1),
                .circle.radius = 2,
        });
        comp = (sk_Manifold){
            .kind = SK_MK_TWO_POINTS,
            .twoPoints.a = HMM_V2(0.55104, 2.94896),
            .twoPoints.b = HMM_V2(2.94896, 0.55104),
        };
        snz_testPrint(_sk_manifoldEq(out, comp), "circle/circle two pt manifold join");

        out = _sk_manifoldJoin(
            (sk_Manifold) {
            .kind = SK_MK_CIRCLE,
                .circle.origin = HMM_V2(0, 0),
                .circle.radius = 1,
        },
            (sk_Manifold) {
            .kind = SK_MK_CIRCLE,
                .circle.origin = HMM_V2(1, 0),
                .circle.radius = 100,
        });
        snz_testPrint(_sk_manifoldEq(out, (sk_Manifold) { .kind = SK_MK_NONE }), "circle/inner circle manifold join");

        out = _sk_manifoldJoin(
            (sk_Manifold) {
            .kind = SK_MK_LINE,
                .line.origin = HMM_V2(-1, 1),
                .line.direction = HMM_V2(-3, 2),
        },
            (sk_Manifold) {
            .kind = SK_MK_CIRCLE,
                .circle.origin = HMM_V2(1, 0),
                .circle.radius = 2,
        });
        comp = (sk_Manifold){
            .kind = SK_MK_TWO_POINTS,
            .twoPoints.a = HMM_V2(-0.80187, 0.86791),
            .twoPoints.b = HMM_V2(2.49418, -1.32945),
        };
        snz_testPrint(_sk_manifoldEq(out, comp), "circle/line manifold join");

        out = _sk_manifoldJoin(
            (sk_Manifold) {
            .kind = SK_MK_LINE,
                .line.origin = HMM_V2(2, 10),
                .line.direction = HMM_V2(0, -1),
        },
            (sk_Manifold) {
            .kind = SK_MK_CIRCLE,
                .circle.origin = HMM_V2(0, 0),
                .circle.radius = 2,
        });
        comp = (sk_Manifold){
            .kind = SK_MK_POINT,
            .point = HMM_V2(2, 0),
        };
        snz_testPrint(_sk_manifoldEq(out, comp), "circle/tangent line manifold join");
    }  // END MANIFOLD JOIN CASES

    snz_Arena a = snz_arenaInit(1000000, "sk testing arena");

    sk_Sketch s = sk_sketchInit();

    sk_Point* p1 = sk_sketchAddPoint(&s, &a, HMM_V2(0, 0));
    sk_Point* p2 = sk_sketchAddPoint(&s, &a, HMM_V2(2, 0));
    sk_Point* p3 = sk_sketchAddPoint(&s, &a, HMM_V2(0, 2));

    sk_Line* l1 = sk_sketchAddLine(&s, &a, p1, p2);
    sk_Line* l2 = sk_sketchAddLine(&s, &a, p2, p3);
    sk_Line* l3 = sk_sketchAddLine(&s, &a, p3, p1);

    sk_sketchAddConstraintDistance(&s, &a, l1, 1);
    sk_sketchAddConstraintDistance(&s, &a, l2, 1);
    sk_sketchAddConstraintDistance(&s, &a, l3, 1);

    sk_sketchSolve(&s, p1, l1, 30); // FIXME: test res?

    snz_arenaDeinit(&a);
}