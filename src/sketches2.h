#pragma once

#include <stdbool.h>

#include "HMM/HandmadeMath.h"
#include "geometry.h"
#include "snooze.h"
#include "ui.h"

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
    bool markedForDelete;

    bool inDragZone;
    float scaleFactor;
    ui_SelectionState sel;
};

typedef struct sk_Line sk_Line;
struct sk_Line {
    union {
        struct {
            sk_Point* p1;
            sk_Point* p2;
        };
        sk_Point* pts[2];
    };
    sk_Line* next;
    bool angleApplied;
    bool angleSolved;
    float expectedAngle;  // from p1 to p2, may not be normalized
    bool markedForDelete;

    ui_SelectionState sel;
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
    // By default, the angle of a line is from p1 to p2. (in rads CCW)
    // if an angle constraint wants to use two lines, but have the angle between them sit on the lines p2s,
    // these values indicate that
    bool flipLine1;
    bool flipLine2;
    float value;

    bool violated;
    bool markedForDelete;

    sk_Constraint* nextAllocated;
    sk_Constraint* nextUnapplied;

    // ui info things
    struct {
        ui_TextArea textArea;
        ui_SelectionState sel;

        HMM_Vec4 drawnColor;
        HMM_Vec2 visualCenter;
        float scaleFactor;
        bool shouldStartFocus;  // used by the shortcut system to signal the constraints text box, FIXME: it's bad but works
    } uiInfo;
};
// FIXME: opaque types for all of these

// FIXME: this should not be here, but has to bc. import problems
const char* sk_constraintLabelStr(sk_Constraint* c, snz_Arena* scratch) {
    // FIXME: cull trailing zeros
    if (c->kind == SK_CK_ANGLE) {
        return snz_arenaFormatStr(scratch, "%.1fdeg", HMM_ToDeg(c->value));
    } else if (c->kind == SK_CK_DISTANCE) {
        return snz_arenaFormatStr(scratch, "%.2fm", c->value);
    } else {
        SNZ_ASSERTF(false, "unreachable. kind: %d", c);
        return NULL;
    }
}

typedef struct {
    sk_Point* firstPoint;
    sk_Line* firstLine;
    sk_Constraint* firstConstraint;

    sk_Constraint* firstUnappliedConstraint;

    sk_Point* originPt;
    sk_Line* originLine;
    float originAngle;

    snz_Arena* arena;
} sk_Sketch;

void sk_sketchSetOrigin(sk_Sketch* sketch, sk_Line* line, bool originOnP1, float angle) {
    sketch->originLine = line;
    sketch->originPt = (originOnP1 ? line->p1 : line->p2);
    sketch->originAngle = angle;
}

sk_Point* sk_sketchAddPoint(sk_Sketch* sketch, HMM_Vec2 pos) {
    sk_Point* p = SNZ_ARENA_PUSH(sketch->arena, sk_Point);
    *p = (sk_Point){
        .pos = pos,
        .next = sketch->firstPoint,
    };
    sketch->firstPoint = p;
    return p;
}

// only creates a new one if one between p1 & 2 doesn't exist. Order may not end up as intended.
sk_Line* sk_sketchAddLine(sk_Sketch* sketch, sk_Point* p1, sk_Point* p2) {
    SNZ_ASSERT(p1 != NULL, "attemped to create a line with a null pt");
    SNZ_ASSERT(p2 != NULL, "attemped to create a line with a null pt");

    sk_Line* line = false;
    for (sk_Line* l = sketch->firstLine; l; l = l->next) {
        if (l->p1 == p1 && l->p2 == p2) {
            line = l;
            break;
        } else if (l->p2 == p1 && l->p1 == p2) {
            line = l;
            break;
        }
    }

    if (line == NULL) {
        line = SNZ_ARENA_PUSH(sketch->arena, sk_Line);
        *line = (sk_Line){
            .p1 = p1,
            .p2 = p2,
            .next = sketch->firstLine,
        };
        sketch->firstLine = line;
    }
    return line;
}

// arena is retained
sk_Sketch sk_sketchInit(snz_Arena* arena) {
    sk_Sketch out = (sk_Sketch){ .arena = arena };

    // initial origin, because sketches shouldn't be empty FIXME: kinda clunky way to do it
    sk_Point* p1 = sk_sketchAddPoint(&out, HMM_V2(0, 0));
    sk_Point* p2 = sk_sketchAddPoint(&out, HMM_V2(1, 0));
    sk_Line* l = sk_sketchAddLine(&out, p1, p2);
    out.originPt = p1;
    out.originLine = l;
    return out;
}

// FIXME: deduplicate
sk_Constraint* sk_sketchAddConstraintDistance(sk_Sketch* sketch, sk_Line* l, float length) {
    SNZ_ASSERT(l != NULL, "attemped to create a distance constraint with null line");

    sk_Constraint* c = SNZ_ARENA_PUSH(sketch->arena, sk_Constraint);
    *c = (sk_Constraint){
        .kind = SK_CK_DISTANCE,
        .line1 = l,
        .value = length,
        .nextAllocated = sketch->firstConstraint,
    };
    sketch->firstConstraint = c;
    return c;
}

// FIXME: deduplicate
sk_Constraint* sk_sketchAddConstraintAngle(sk_Sketch* sketch, sk_Line* line1, bool flipLine1, sk_Line* line2, bool flipLine2, float angle) {
    SNZ_ASSERT(line1 != NULL, "attemped to create a angle constraint with null line");
    SNZ_ASSERT(line2 != NULL, "attemped to create a angle constraint with null line");

    sk_Constraint* c = SNZ_ARENA_PUSH(sketch->arena, sk_Constraint);
    *c = (sk_Constraint){
        .kind = SK_CK_ANGLE,
        .line1 = line1,
        .line2 = line2,
        .flipLine1 = flipLine1,
        .flipLine2 = flipLine2,
        .value = angle,
        .nextAllocated = sketch->firstConstraint,
    };
    sketch->firstConstraint = c;
    return c;
}

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
        if (geo_floatZero(disriminant)) {
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
        int positiveCount = geo_floatGreaterEqual(u1, 0) + geo_floatGreaterEqual(u2, 0);

        if (positiveCount == 0) {
            return (sk_Manifold) { .kind = SK_MK_NONE };
        } else if (positiveCount == 1) {
            sk_Manifold out = (sk_Manifold){
                .kind = SK_MK_POINT,
                .point = geo_floatGreaterEqual(u1, 0) ? p1 : p2,
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
        if (geo_v2Equal(a.line.origin, b.line.origin) && geo_v2Equal(a.line.direction, b.line.direction)) {
            return a;  // coincident, return the starting manifold
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
        if (geo_floatZero(denom)) {
            return (sk_Manifold) { .kind = SK_MK_NONE };
        }

        float u = num / denom;
        if (u < 0) {
            return (sk_Manifold) { .kind = SK_MK_NONE };
        }
        // FIXME: check for out of bounds on the other line
        sk_Manifold out = (sk_Manifold){
            .kind = SK_MK_POINT,
            .point = HMM_Lerp(a.line.origin, u, aOther),
        };
        return out;
    } else if (circleCount == 2) {
        // Circle circle intersection alg, stolen from here: https://stackoverflow.com/questions/3349125/circle-circle-intersection-points
        HMM_Vec2 diff = HMM_Sub(b.circle.origin, a.circle.origin);
        float d = HMM_Len(diff);
        if (geo_floatZero(d) && geo_floatEqual(a.circle.radius, b.circle.radius)) {
            return a;  // circles are coincedent, it's ok to return either
        } else if (geo_floatEqual(d, a.circle.radius + b.circle.radius)) {
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
        out &= geo_v2Equal(a.point, b.point);
    } else if (a.kind == SK_MK_TWO_POINTS) {
        bool eq1 = geo_v2Equal(a.twoPoints.a, b.twoPoints.a);
        eq1 &= geo_v2Equal(a.twoPoints.b, b.twoPoints.b);
        bool eq2 = geo_v2Equal(a.twoPoints.a, b.twoPoints.b);
        eq2 &= geo_v2Equal(a.twoPoints.b, b.twoPoints.a);

        out &= (eq1 || eq2);
    } else if (a.kind == SK_MK_LINE) {
        out &= geo_v2Equal(a.line.origin, b.line.origin);
        out &= geo_v2Equal(a.line.direction, b.line.direction);
    } else if (a.kind == SK_MK_CIRCLE) {
        out &= geo_v2Equal(a.circle.origin, b.circle.origin);
        out &= geo_floatEqual(a.circle.radius, b.circle.radius);
    } else {
        SNZ_ASSERTF(false, "Unreachable case, kind was: %d", a.kind);
    }
    return out;
}

static void _sk_rotatePtsWhenUnsolved(sk_Point* const p1, sk_Point* const p2, float angle) {
    float t = 0.5;
    if (p1->solved && !p2->solved) {
        t = 0;
    } else if (p2->solved && !p1->solved) {
        t = 1;
    }
    HMM_Vec2 midpt = HMM_Lerp(p1->pos, t, p2->pos);
    HMM_Vec2 p1New = HMM_Add(midpt, HMM_RotateV2(HMM_SubV2(p1->pos, midpt), angle));
    HMM_Vec2 p2New = HMM_Add(midpt, HMM_RotateV2(HMM_SubV2(p2->pos, midpt), angle));

    if (!p1->solved) {
        p1->pos = p1New;
    }
    if (!p2->solved) {
        p2->pos = p2New;
    }
}

float sk_angleOfLine(HMM_Vec2 p1, HMM_Vec2 p2, bool flip) {
    HMM_Vec2 diff = HMM_Sub(p2, p1);
    float angle = atan2f(diff.Y, diff.X);
    if (flip) {
        angle += HMM_AngleDeg(180);
    }
    return angle;
}

static float _sk_angleNormalized(float a) {
    while (a > HMM_AngleDeg(180)) {
        a -= HMM_AngleDeg(360);
    }
    while (a < HMM_AngleDeg(-180)) {
        a += HMM_AngleDeg(360);
    }
    return a;
}

void sk_sketchSolve(sk_Sketch* sketch) {
    int64_t sketchPointCount = 0;
    int64_t solvedPointCount = 1;

    {  // RESET SOLVE DEPENDENT VARIABLES IN THE SKETCH
        for (sk_Point* point = sketch->firstPoint; point; point = point->next) {
            point->manifold = (sk_Manifold){ .kind = SK_MK_ANY };
            sketchPointCount++;
            point->solved = false;
        }

        for (sk_Line* line = sketch->firstLine; line; line = line->next) {
            line->expectedAngle = 0;
            line->angleSolved = false;
            line->angleApplied = false;
        }

        sketch->firstUnappliedConstraint = NULL;
        for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
            c->violated = false;
            c->nextUnapplied = sketch->firstUnappliedConstraint;
            sketch->firstUnappliedConstraint = c;
        }

        SNZ_ASSERT(sketch->originPt != NULL, "non-empty sketch w/ no origin pt.");
        SNZ_ASSERT(sketch->originLine != NULL, "non-empty sketch w/ no origin line.");
        SNZ_ASSERT(sketch->originPt == sketch->originLine->p1 || sketch->originPt == sketch->originLine->p2, "origin pt wasn't a part of origin line.");

        sketch->originLine->angleSolved = true;
        sketch->originLine->expectedAngle = sketch->originAngle;

        sketch->originPt->solved = true;
        sketch->originPt->manifold = (sk_Manifold){
            .kind = SK_MK_POINT,
            .point = sketch->originPt->pos,
        };
    }

    while (true) {  // FIXME: cutoff
        bool anySolved = false;

        sk_Constraint* prevConstraint = NULL;
        for (sk_Constraint* c = sketch->firstUnappliedConstraint; c; c = c->nextUnapplied) {
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
                    sk_Manifold newManifold = _sk_manifoldJoin(variable->manifold, m);
                    if (newManifold.kind != SK_MK_NONE) {
                        variable->manifold = newManifold;
                    }
                    // If the constraint is violated, we just mark it as applied and move on.
                    // the implicit solved will mark it officially afterwards.
                    applied = true;
                }
            } else if (c->kind == SK_CK_ANGLE) {
                int solvedCount = c->line1->angleSolved + c->line2->angleSolved;
                if (solvedCount == 1) {
                    bool fixedIsLine1 = c->line1->angleSolved;
                    sk_Line* fixed = c->line1->angleSolved ? c->line1 : c->line2;
                    sk_Line* variable = c->line1->angleSolved ? c->line2 : c->line1;
                    float fixedAngle = fixed->expectedAngle;

                    if (fixedIsLine1 && c->flipLine1) {
                        fixedAngle += HMM_AngleDeg(180);
                    } else if (!fixedIsLine1 && c->flipLine2) {
                        fixedAngle += HMM_AngleDeg(180);
                    }

                    float variableAngle = fixedAngle + c->value * (fixedIsLine1 ? 1 : -1);
                    // if fixed is l1, and the angle indicated is from l1 to l2, then variables angle is l1 + val,
                    // otherwise, variable is line1, and l1s angle is fixed - val
                    if (!fixedIsLine1 && c->flipLine1) {
                        variableAngle += HMM_AngleDeg(180);
                    } else if (fixedIsLine1 && c->flipLine2) {
                        variableAngle += HMM_AngleDeg(180);
                    }

                    variable->angleSolved = true;
                    variable->expectedAngle = variableAngle;
                    applied = true;
                }
            }

            if (applied) {
                anySolved = true;
                if (!prevConstraint) {
                    sketch->firstUnappliedConstraint = c->nextUnapplied;
                } else {
                    prevConstraint->nextUnapplied = c->nextUnapplied;
                }
            } else {
                prevConstraint = c;
            }
        }  // end manifold join/angle propagation loop

        for (sk_Line* line = sketch->firstLine; line; line = line->next) {
            int ptSolvedCount = line->p1->solved + line->p2->solved;
            if (!line->angleSolved && ptSolvedCount == 2) {
                HMM_Vec2 diff = HMM_Sub(line->p2->pos, line->p1->pos);
                line->expectedAngle = atan2f(diff.Y, diff.X);
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
                    // invert direction of the variable manifold if it is p1, because the angle is indicating p1 -> p2
                    .line.direction = HMM_RotateV2(HMM_V2(fixed == line->p1 ? 1 : -1, 0), line->expectedAngle),
                };
                sk_Manifold startManifold = variable->manifold;
                sk_Manifold newManifold = _sk_manifoldJoin(startManifold, m);
                if (newManifold.kind != SK_MK_NONE) {
                    variable->manifold = newManifold;
                }
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
            }
        }

        if (solvedPointCount == sketchPointCount) {
            return;  // skip implicit solving when everything is good
        } else if (!anySolved) {
            break;
        }
    }  // end solve loop

    for (int i = 0; i < 1000; i++) {
        float maxError = 0;
        for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
            float error = 0;
            if (c->kind == SK_CK_ANGLE) {
                // FIXME: Origin angle should be a constraint but it doesnt get applied here??
                if (c->line1->p1->solved && c->line1->p2->solved && c->line2->p1->solved && c->line2->p2->solved) {
                    continue;
                }

                // Can't use the angle prop because that is the expected angle, not the actual
                float l1Angle = sk_angleOfLine(c->line1->p1->pos, c->line1->p2->pos, c->flipLine1);
                float l2Angle = sk_angleOfLine(c->line2->p1->pos, c->line2->p2->pos, c->flipLine2);

                float angleDiff = _sk_angleNormalized(c->value - _sk_angleNormalized(l2Angle - l1Angle)) / 2;
                error = angleDiff * 2;
                _sk_rotatePtsWhenUnsolved(c->line1->p1, c->line1->p2, -angleDiff);
                _sk_rotatePtsWhenUnsolved(c->line2->p1, c->line2->p2, angleDiff);
            } else if (c->kind == SK_CK_DISTANCE) {
                HMM_Vec2* const p1 = &c->line1->p1->pos;
                HMM_Vec2* const p2 = &c->line1->p2->pos;
                HMM_Vec2 diff = HMM_Norm(HMM_Sub(*p2, *p1));
                HMM_Vec2 midpt = HMM_DivV2F(HMM_Add(*p2, *p1), 2);

                if (!c->line1->p1->solved) {
                    *p1 = HMM_Sub(midpt, HMM_Mul(diff, c->value / 2));
                }
                if (!c->line1->p2->solved) {
                    *p2 = HMM_Add(midpt, HMM_Mul(diff, c->value / 2));
                }
                error = HMM_Len(HMM_Sub(*p2, *p1)) - c->value;
            }  // end constraint kind switch

            error = fabsf(error);
            if (error > maxError) {
                maxError = error;
            }
        }  // end inplicit solve loop

        if (fabsf(maxError) < geo_EPSILON) {
            return;
        }
    }  // end iteration loop

    // mark constraints as violated
    for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
        if (c->kind == SK_CK_DISTANCE) {
            float error = HMM_Len(HMM_Sub(c->line1->p2->pos, c->line1->p1->pos)) - c->value;
            if (!geo_floatZero(error)) {
                c->violated = true;
            }
        } else if (c->kind == SK_CK_ANGLE) {
            float l1Angle = sk_angleOfLine(c->line1->p2->pos, c->line1->p2->pos, c->flipLine1);
            float l2Angle = sk_angleOfLine(c->line2->p2->pos, c->line2->p2->pos, c->flipLine2);

            float error = c->value - (l2Angle - l1Angle);
            if (!geo_floatZero(error)) {
                c->violated = true;
            }
        }
    }
}

// picks a new origin if necessary
void sk_sketchClearElementsMarkedForDelete(sk_Sketch* sketch) {
    // propagate marking for deletion to anything dependent
    {
        for (sk_Line* l = sketch->firstLine; l; l = l->next) {
            if (l->p1->markedForDelete || l->p2->markedForDelete) {
                l->markedForDelete = true;
            }
        }

        for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
            p->markedForDelete = true;
        }

        // keep any points connected to any lines that still exist
        for (sk_Line* l = sketch->firstLine; l; l = l->next) {
            if (!l->markedForDelete) {
                l->p1->markedForDelete = false;
                l->p2->markedForDelete = false;
            }
        }

        for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
            if (c->kind == SK_CK_ANGLE) {
                if (c->line1->markedForDelete || c->line2->markedForDelete) {
                    c->markedForDelete = true;
                }
            } else if (c->kind == SK_CK_DISTANCE) {
                if (c->line1->markedForDelete) {
                    c->markedForDelete = true;
                }
            } else {
                SNZ_ASSERTF(false, "unreachable. kind: %d", c->kind);
            }
        }
    }

    // FIXME: pool deleted elts so they can be reused

    {  // remake point list
        sk_Point* newList = NULL;
        sk_Point* next = NULL;
        for (sk_Point* p = sketch->firstPoint; p; p = next) {
            next = p->next;
            bool isOrigin = p == sketch->originLine->p1 || p == sketch->originLine->p2;
            if (!p->markedForDelete || isOrigin) {
                p->markedForDelete = false;
                p->next = newList;
                newList = p;
            } else {
                memset(p, 0, sizeof(*p));
            }
        }
        sketch->firstPoint = newList;
    }

    {  // remake line list
        sk_Line* newList = NULL;
        sk_Line* next = NULL;
        for (sk_Line* l = sketch->firstLine; l; l = next) {
            next = l->next;
            if (!l->markedForDelete || l == sketch->originLine) {
                l->markedForDelete = false;
                l->next = newList;
                newList = l;
            } else {
                memset(l, 0, sizeof(*l));
            }
        }
        sketch->firstLine = newList;
    }

    {  // remake constraint list
        sk_Constraint* newList = NULL;
        sk_Constraint* next = NULL;
        for (sk_Constraint* c = sketch->firstConstraint; c; c = next) {
            next = c->nextAllocated;
            if (!c->markedForDelete) {
                c->nextAllocated = newList;
                newList = c;
            } else {
                memset(c, 0, sizeof(*c));
            }
        }
        sketch->firstConstraint = newList;
    }
}

void sk_sketchDeselectAll(sk_Sketch* sketch) {
    for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
        p->sel.selected = false;
    }

    for (sk_Line* l = sketch->firstLine; l; l = l->next) {
        l->sel.selected = false;
    }

    for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
        c->uiInfo.sel.selected = false;
    }
}

typedef struct _sk_TriangulationPoint _sk_TriangulationPoint;
typedef struct {
    _sk_TriangulationPoint* pt;
    bool traversed;
} _sk_TriangulationEdge;
SNZ_SLICE(_sk_TriangulationEdge);

struct _sk_TriangulationPoint {
    _sk_TriangulationEdgeSlice adjacent;
    HMM_Vec2 pos;
};

SNZ_SLICE(_sk_TriangulationPoint);

typedef struct _sk_TriangulationVertLoop _sk_TriangulationVertLoop;
struct _sk_TriangulationVertLoop {
    _sk_TriangulationVertLoop* next;
    HMM_Vec2Slice pts;
};

// FIXME: does this function gauranteed crash on a malformed sketch??
geo_Mesh sk_sketchTriangulate(const sk_Sketch* sketch, snz_Arena* arena, snz_Arena* scratch) {
    assert(arena || !arena);
    // // arcs -> lines
    // lines -> intersections
    // sketch -> graph
    // graph -> vert loops (don't forget holes!!)
    // vert loops -> tris
    // done!!

    _sk_TriangulationPointSlice points = { 0 };
    { // imports
        SNZ_ARENA_ARR_BEGIN(scratch, _sk_TriangulationPoint);
        for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
            _sk_TriangulationPoint* new = SNZ_ARENA_PUSH(scratch, _sk_TriangulationPoint);
            new->pos = p->pos;
        }
        points = SNZ_ARENA_ARR_END(scratch, _sk_TriangulationPoint);
    }

    if (!points.count) {
        return (geo_Mesh) { 0 };
    }

    { // intersections
    }

    { // adj. lists
        sk_Point* ptInSourceSketch = sketch->firstPoint;
        for (int ptIdx = 0; ptIdx < points.count; (ptIdx++, ptInSourceSketch = ptInSourceSketch->next)) {
            _sk_TriangulationPoint* pt = &points.elems[ptIdx];

            SNZ_ARENA_ARR_BEGIN(scratch, _sk_TriangulationPoint*);
            for (sk_Line* l = sketch->firstLine; l; l = l->next) {
                bool pt1Matches = l->p1 == ptInSourceSketch;
                if (!pt1Matches && l->p2 != ptInSourceSketch) {
                    continue;
                }

                sk_Point* targetPt = (pt1Matches ? l->p2 : l->p1);
                _sk_TriangulationPoint* finalPt = NULL;
                // FIXME: cache an index in the OG pts or no?
                int i = 0;
                for (sk_Point* other = sketch->firstPoint; other; other = other->next) {
                    if (other == targetPt) {
                        finalPt = &points.elems[i];
                        break;
                    }
                    i++;
                }
                SNZ_ASSERT(finalPt != NULL, "point on line wasn't found in the triangulation point arr.");
                *SNZ_ARENA_PUSH(scratch, _sk_TriangulationEdge) = (_sk_TriangulationEdge){ .pt = finalPt };
            }
            pt->adjacent = SNZ_ARENA_ARR_END(scratch, _sk_TriangulationEdge);
        }
    } // end generating adj. lists

    { // iteratively cull pts with one or none adj.
    }

    _sk_TriangulationVertLoop* firstLoop = 0;
    { // vert loop gen
        SNZ_ASSERTF(points.count > 0, "points count was: %lld", points.count);

        while (true) { // FIXME: emergency cutoff
            _sk_TriangulationPoint* prevPt = NULL;
            _sk_TriangulationPoint* currentPt = NULL;
            { // find the seed for the next loop
                for (int i = 0; i < points.count; i++) {
                    _sk_TriangulationPoint* p = &points.elems[i];
                    SNZ_ASSERTF(p->adjacent.count >= 2, "point had only %lld adjacents.", p->adjacent.count);
                    for (int j = 0; j < p->adjacent.count; j++) {
                        _sk_TriangulationEdge* e = &p->adjacent.elems[j];
                        if (e->traversed) {
                            continue;
                        }
                        e->traversed = true;
                        prevPt = p;
                        currentPt = e->pt;

                        i = points.count; // break outer
                        break;
                    }
                }
            }
            if (prevPt == NULL) {
                break; // break out of finding more loops, because every edge has been traversed both ways
            }
            _sk_TriangulationPoint* startPt = prevPt;

            float dir = 0;
            {
                HMM_Vec2 diff = HMM_Sub(currentPt->pos, startPt->pos);
                dir = atan2f(diff.Y, diff.X);
                if (dir < 0) {
                    dir += HMM_AngleDeg(360);
                }
            }
            while (currentPt != startPt) { // FIXME: emergency cutoff
                _sk_TriangulationEdge* selected = NULL;
                float maxAngle = -INFINITY;
                for (int i = 0; i < currentPt->adjacent.count; i++) {
                    _sk_TriangulationEdge* adj = &currentPt->adjacent.elems[i];
                    if (adj->pt == prevPt) {
                        continue;
                    }
                    HMM_Vec2 diff = HMM_Sub(adj->pt->pos, currentPt->pos);
                    float angle = dir - atan2f(diff.Y, diff.X);
                    while (angle < 0) {
                        angle += HMM_AngleDeg(360);
                    }
                    while (angle > 360) {
                        angle -= HMM_AngleDeg(360);
                    }

                    // select the most CCW next point to go to
                    if (angle > maxAngle) {
                        selected = adj;
                        maxAngle = angle;
                    }
                }
                prevPt = currentPt;
                selected->traversed = true;
                currentPt = selected->pt;
                *SNZ_ARENA_PUSH(scratch, HMM_Vec2) = currentPt->pos;
            } // end finding points for a loop

            HMM_Vec2Slice loopPoints = SNZ_ARENA_ARR_END(scratch, HMM_Vec2);
            _sk_TriangulationVertLoop* loop = SNZ_ARENA_PUSH(scratch, _sk_TriangulationVertLoop);
            loop->pts = loopPoints;
            loop->next = firstLoop;
            firstLoop = loop;
        } // end loop loop
    } // end all loop gen

    { // hole gen
    }

    { // vert loop minimization
    }

    { // triangulation
    }

    // geo_Mesh out = { 0 };
    // out.bspTris = ;
    // out.firstFace = ;
    return (geo_Mesh) { 0 };
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
                .line.origin = HMM_V2(1, 0),
                .line.direction = HMM_V2(-3, 0),
        },
            (sk_Manifold) {
            .kind = SK_MK_CIRCLE,
                .circle.origin = HMM_V2(1, 0),
                .circle.radius = 2,
        });
        comp = (sk_Manifold){
            .kind = SK_MK_POINT,
            .point = HMM_V2(-1, 0),
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

    {
        sk_Sketch s = sk_sketchInit(&a);

        sk_Point* p1 = sk_sketchAddPoint(&s, HMM_V2(0, 0));
        sk_Point* p2 = sk_sketchAddPoint(&s, HMM_V2(2, 0));
        sk_Point* p3 = sk_sketchAddPoint(&s, HMM_V2(0, 2));

        sk_Line* l1 = sk_sketchAddLine(&s, p1, p2);
        sk_Line* l2 = sk_sketchAddLine(&s, p2, p3);
        sk_Line* l3 = sk_sketchAddLine(&s, p3, p1);

        sk_sketchAddConstraintDistance(&s, l1, 1);
        sk_sketchAddConstraintDistance(&s, l2, 1);
        sk_sketchAddConstraintDistance(&s, l3, 1);

        sk_sketchSetOrigin(&s, l1, true, 0);
        sk_sketchSolve(&s);  // FIXME: message?
    }

    {
        sk_Sketch s = sk_sketchInit(&a);

        sk_Point* p1 = sk_sketchAddPoint(&s, HMM_V2(0, 0));
        sk_Point* p2 = sk_sketchAddPoint(&s, HMM_V2(0, 1));
        sk_Point* p3 = sk_sketchAddPoint(&s, HMM_V2(0, 0));

        sk_Line* l1 = sk_sketchAddLine(&s, p1, p2);
        sk_Line* l2 = sk_sketchAddLine(&s, p2, p3);
        sk_Line* l3 = sk_sketchAddLine(&s, p3, p1);

        sk_sketchAddConstraintDistance(&s, l1, 1);
        sk_sketchAddConstraintAngle(&s, l1, true, l2, false, HMM_AngleDeg(30));
        sk_sketchAddConstraintAngle(&s, l1, false, l3, true, HMM_AngleDeg(-30));

        sk_sketchSetOrigin(&s, l1, true, 0);
        sk_sketchSolve(&s);  // FIXME: message?
    }

    snz_arenaDeinit(&a);
}
