#include <stdbool.h>

#include "HMM/HandmadeMath.h"
#include "snooze.h"
#include "csg.h"

typedef enum {
    SK_MK_NONE,
    SK_MK_CIRCLE,
    SK_MK_LINE,
    SK_MK_TWO_POINTS,
    SK_MK_POINT,
    SK_MK_ANY,
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

// FIXME: tests
sk_Manifold sk_manifoldJoin(sk_Manifold a, sk_Manifold b) {
    int lineCount = a.kind == SK_MK_LINE + b.kind == SK_MK_LINE;
    int circleCount = a.kind == SK_MK_CIRCLE + b.kind == SK_MK_CIRCLE;
    int anyCount = a.kind == SK_MK_ANY + b.kind == SK_MK_ANY;

    if (anyCount >= 1) {
        return a.kind == SK_MK_ANY ? b : a;
    } else if (lineCount == 1 && circleCount == 1) {
        // stolen from: https://paulbourke.net/geometry/circlesphere/
        sk_Manifold line = a.kind == SK_MK_LINE ? a : b;
        sk_Manifold circle = a.kind == SK_MK_LINE ? b : a;

        HMM_Vec2 lineOther = HMM_Add(line.line.origin, line.line.direction);
        float a = HMM_Len(HMM_Sub(lineOther, line.line.origin));

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

        sk_Manifold out = (sk_Manifold){
            .kind = SK_MK_TWO_POINTS,
            .twoPoints.a = HMM_Lerp(line.line.origin, u1, lineOther),
            .twoPoints.b = HMM_Lerp(line.line.origin, u2, lineOther),
        };
        return out;
    } else if (lineCount == 2) {
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
        // FIXME: what do we do with coincedent circles??
        SNZ_ASSERT(!csg_floatZero(d) || !csg_floatEqual(a.circle.radius, b.circle.radius), "Two circles were coincedent :) have fun w/ this one");
        if (csg_floatEqual(d, a.circle.radius + b.circle.radius)) {
            // Circles are just touching, return the single point
            HMM_Vec2 pt = HMM_Mul(HMM_Norm(diff), a.circle.radius);
            return (sk_Manifold) { .kind = SK_MK_POINT, .point = pt };
        } else if (d > (a.circle.radius + b.circle.radius)) {
            // Circles are outside eachother, no points
            return (sk_Manifold) { .kind = SK_MK_NONE };
        } else if (d < absf(a.circle.radius - b.circle.radius)) {
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

typedef struct sk_Point sk_Point;
struct sk_Point {
    HMM_Vec2 pos;
    sk_Point* next;
};

typedef struct sk_Line sk_Line;
struct sk_Line {
    sk_Point* p1;
    sk_Point* p2;
    sk_Line* next;
};

typedef enum {
    SK_CK_DISTANCE,
    SK_CK_ANGLE_LINES,
} sk_ConstraintKind;

typedef struct sk_Constraint sk_Constraint;
struct sk_Constraint {
    sk_ConstraintKind kind;
    sk_Line* line1;
    sk_Line* line2;
    sk_Point* point;
    float value;
    sk_Constraint* next;
};

typedef struct {
    sk_Point* firstPoint;
    sk_Line* firstLine;
    sk_Constraint* firstConstraint;
} sk_Sketch;

sk_Sketch sk_sketchInit() {
    return (sk_Sketch) {
        .firstPoint = NULL,
            .firstLine = NULL,
    };
}

#define SK_SOLVE_ITERATIONS 1000
#define SK_SOLVE_MAX_ERROR 0.0001

float _sk_normalizeAngle(float a) {
    while (a > 180) {
        a -= 360;
    }
    while (a <= -180) {
        a += 360;
    }
    return a;
}

float _sk_normalizeLineAngle(float a) {
    while (a > 90) {
        a -= 180;
    }
    while (a <= -90) {
        a += 180;
    }
    return a;
}

// p1 and p2 are both read from and written to
// rotates both points around their center by the given angle, (CW?)
void _sk_rotateStraightLine(HMM_Vec2* p1, HMM_Vec2* p2, float angle) {
    HMM_Vec2 center = HMM_DivV2F(HMM_AddV2(*p1, *p2), 2.0f);
    HMM_Vec2 rel = HMM_SubV2(*p1, center);  // p1 relative to center
    HMM_Vec2 rotated = HMM_RotateV2(rel, angle);
    *p1 = HMM_AddV2(center, rotated);
    *p2 = HMM_AddV2(center, HMM_SubV2(HMM_V2(0, 0), rotated));
}

// two of the points can be the same point and itll still limp to a solution after a couple of iterations
// returns the initial unsigned error
// points are read and written to
float _sk_solveAngle(HMM_Vec2* p1a, HMM_Vec2* p1b, HMM_Vec2* p2a, HMM_Vec2* p2b, float angle) {
    HMM_Vec2 l1Rel = HMM_SubV2(*p1b, *p1a);
    HMM_Vec2 l2Rel = HMM_SubV2(*p2b, *p2a);
    float l1Angle = _sk_normalizeLineAngle(HMM_AngleRad(atan2f(l1Rel.Y, l1Rel.X)));
    float l2Angle = _sk_normalizeLineAngle(HMM_AngleRad(atan2f(l2Rel.Y, l2Rel.X)));
    float relAngle = _sk_normalizeAngle(l2Angle - l1Angle);  // l2 relative to l1
    float error = fabsf(relAngle) - angle;                   // positive indicates points too far

    float relAngleNormalized = relAngle;
    if (relAngleNormalized > 0) {
        relAngleNormalized = 1;
    } else if (relAngleNormalized < 0) {
        relAngleNormalized = -1;
    } else {
        relAngleNormalized = 1;  // in the case of the lines being perfectly parallel, pick a direction
    }

    // apply calculated rotations to the points
    float rot = relAngleNormalized * (error / 2);
    _sk_rotateStraightLine(p1a, p1b, rot);
    _sk_rotateStraightLine(p2a, p2b, -rot);

    return fabsf(error);
}

// returns initial unsigned error
float _sk_solveDistance(HMM_Vec2* p1, HMM_Vec2* p2, float length) {
    HMM_Vec2 diff = HMM_SubV2(*p2, *p1);
    float error = length - HMM_LenV2(diff);  // positive error = points too close
    // printf("initial error for constraint %d: %f\n", constraintIndex, error);
    HMM_Vec2 pointDelta = HMM_MulV2F(HMM_NormV2(diff), -error / 2);
    *p1 = HMM_AddV2(*p1, pointDelta);
    *p2 = HMM_SubV2(*p2, pointDelta);
    return fabsf(error);
}

bool sk_solve(sk_Sketch* sketch) {
    for (int i = 0; i < SK_SOLVE_ITERATIONS; i++) {
        float iterationError = 0;
        for (sk_Constraint* c = sketch->firstConstraint; c; c = c->next) {
            if (c->kind == SK_CK_DISTANCE) {
                iterationError = _sk_solveDistance(
                    &c->line1->p1,
                    &c->line2->p2,
                    c->value);
            } else if (c->kind == SK_CK_ANGLE_LINES) {
                iterationError = _sk_solveAngle(
                    &c->line1->p1->pos,
                    &c->line1->p2->pos,
                    &c->line2->p1->pos,
                    &c->line2->p2->pos, c->value);
            }
        }  // end constraint loop

        if (iterationError < SK_SOLVE_MAX_ERROR) {
            return true;
        }
    }  // end iteration loop
    return false;
}

/* fail states:
    any null ptrs
    any wacky OOB pointers
    any use-after-free ptrs
    any duplicate geometry (in any permutation)
    any colinear vertloops
    any 0 length/value constraints
    any lines with the same p1 and p2
    any constraints with the same l1 and l2
    an unsolvable sketch

    assumes that the sketch data is not malformed. ie a circular line list or smth else.
    Sketches created using only sk_sketchAddXX or sk_sketchRemoveXX should be OK for this.
    FIXME: sketchy as hell tho, especially the circular lists
 */
bool sk_sketchValid(sk_Sketch* sketch) {
    for (sk_Line* line = sketch->firstLine; line; line = line->next) {
        sk_Point* line0 = SNZ_MIN(line->p1, line->p2);
        sk_Point* line1 = SNZ_MIN(line->p2, line->p1);
        for (sk_Line* other = line->next; other; other = other->next) {
            sk_Point* other0 = SNZ_MIN(other->p1, other->p2);
            sk_Point* other1 = SNZ_MIN(other->p2, other->p1);

            if (line0 == other0 && line1 == other1) {
                // FIXME: proper error report here :)
                printf("[sk_sketchValid]: failed because of duplicate lines.");
                return false;
            }
        }
    }
}

void sk_sketchAddPoint(sk_Sketch* sketch, sk_Point* point) {
    point->next = sketch->firstPoint;
    sketch->firstPoint = point;
}

void sk_sketchAddLine(sk_Sketch* sketch, sk_Line* line) {
    line->next = sketch->firstLine;
    sketch->firstLine = line;
}

void sk_sketchAddConstraint(sk_Sketch* sketch, sk_Constraint* c) {
    c->next = sketch->firstConstraint;
    sketch->firstLine = c;
}

void sk_sketchRemovePointAndDeps(sk_Sketch* sketch, sk_Point* point) {
}

void sk_sketchRemoveLineAndDeps(sk_Sketch* sketch, sk_Line* line) {
}

void sk_sketchRemoveConstraint(sk_Sketch* sketch, sk_Constraint* constraint) {
}

/*

sketches:

a collection of points, lines and constraints

a failed add should highlight every other constraint in the chain to the origin that is preventing the add?
favor angles?
cause otherwise it's just every constrain in the sketch
Fusions 'no response' aesthetic is really fuckin annoying

also a better way of visualizing constraints



*/
