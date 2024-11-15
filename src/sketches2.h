#include <stdbool.h>

#include "HMM/HandmadeMath.h"
#include "csg.h"
#include "snooze.h"

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
// FIXME: one big geometry file, not csg sketches and geo
sk_Manifold sk_manifoldJoin(sk_Manifold a, sk_Manifold b) {
    int lineCount = (a.kind == SK_MK_LINE) + (b.kind == SK_MK_LINE);
    int circleCount = (a.kind == SK_MK_CIRCLE) + (b.kind == SK_MK_CIRCLE);
    int anyCount = (a.kind == SK_MK_ANY) + (b.kind == SK_MK_ANY);

    if (anyCount >= 1) {
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
            return (sk_Manifold){.kind = SK_MK_NONE};
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
            return (sk_Manifold){.kind = SK_MK_NONE};
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
            return (sk_Manifold){.kind = SK_MK_POINT, .point = pt};
        } else if (d > (a.circle.radius + b.circle.radius)) {
            // Circles are outside eachother, no points
            return (sk_Manifold){.kind = SK_MK_NONE};
        } else if (d < fabsf(a.circle.radius - b.circle.radius)) {
            // Circles are within eachother, no points
            return (sk_Manifold){.kind = SK_MK_NONE};
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
        return (sk_Manifold){.kind = SK_MK_NONE};
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
    return (sk_Sketch){
        .firstPoint = NULL,
        .firstLine = NULL,
        .firstConstraint = NULL,
    };
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
    sketch->firstConstraint = c;
}

// FIXME: this
// void sk_sketchRemovePointAndDeps(sk_Sketch* sketch, sk_Point* point) {
// }

// void sk_sketchRemoveLineAndDeps(sk_Sketch* sketch, sk_Line* line) {
// }

// void sk_sketchRemoveConstraint(sk_Sketch* sketch, sk_Constraint* constraint) {
// }

/*

sketches:

a collection of points, lines and constraints

a failed add should highlight every other constraint in the chain to the origin that is preventing the add?
favor angles?
cause otherwise it's just every constrain in the sketch
Fusions 'no response' aesthetic is really fuckin annoying

also a better way of visualizing constraints



*/

bool _sk_manifoldEq(sk_Manifold a, sk_Manifold b) {
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

void sk_tests() {
    snz_testPrintSection("sketch");

    sk_Manifold out = sk_manifoldJoin(
        (sk_Manifold){
            .kind = SK_MK_LINE,
            .line.origin = HMM_V2(0, 0),
            .line.direction = HMM_V2(1, 0),
        },
        (sk_Manifold){
            .kind = SK_MK_LINE,
            .line.origin = HMM_V2(2, 2),
            .line.direction = HMM_V2(0, -1),
        });
    sk_Manifold comp = (sk_Manifold){
        .kind = SK_MK_POINT,
        .point = HMM_V2(2, 0),
    };
    snz_testPrint(_sk_manifoldEq(out, comp), "line/line manifold join");

    out = sk_manifoldJoin(
        (sk_Manifold){
            .kind = SK_MK_LINE,
            .line.origin = HMM_V2(10, 10),
            .line.direction = HMM_V2(0, 1),
        },
        (sk_Manifold){
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
    out = sk_manifoldJoin((sk_Manifold){.kind = SK_MK_ANY}, comp);
    snz_testPrint(_sk_manifoldEq(comp, out), "line/any manifold join");
    out = sk_manifoldJoin(comp, (sk_Manifold){.kind = SK_MK_ANY});
    snz_testPrint(_sk_manifoldEq(comp, out), "line/any manifold join 2");

    out = sk_manifoldJoin(
        (sk_Manifold){
            .kind = SK_MK_CIRCLE,
            .circle.origin = HMM_V2(0, 0),
            .circle.radius = 3,
        },
        (sk_Manifold){
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

    out = sk_manifoldJoin(
        (sk_Manifold){
            .kind = SK_MK_CIRCLE,
            .circle.origin = HMM_V2(0, 0),
            .circle.radius = 1,
        },
        (sk_Manifold){
            .kind = SK_MK_CIRCLE,
            .circle.origin = HMM_V2(1, 0),
            .circle.radius = 100,
        });
    snz_testPrint(_sk_manifoldEq(out, (sk_Manifold){.kind = SK_MK_NONE}), "circle/inner circle manifold join");

    out = sk_manifoldJoin(
        (sk_Manifold){
            .kind = SK_MK_LINE,
            .line.origin = HMM_V2(-1, 1),
            .line.direction = HMM_V2(-3, 2),
        },
        (sk_Manifold){
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

    out = sk_manifoldJoin(
        (sk_Manifold){
            .kind = SK_MK_LINE,
            .line.origin = HMM_V2(2, 10),
            .line.direction = HMM_V2(0, -1),
        },
        (sk_Manifold){
            .kind = SK_MK_CIRCLE,
            .circle.origin = HMM_V2(0, 0),
            .circle.radius = 2,
        });
    comp = (sk_Manifold){
        .kind = SK_MK_POINT,
        .point = HMM_V2(2, 0),
    };
    snz_testPrint(_sk_manifoldEq(out, comp), "circle/tangent line manifold join");
}