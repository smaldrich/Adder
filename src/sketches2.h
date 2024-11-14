#include <stdbool.h>

#include "HMM/HandmadeMath.h"
#include "snooze.h"

typedef enum {
    SK_MK_INVALID,
    SK_MK_CIRCLE,
    SK_MK_LINE,
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
        HMM_Vec2 point;
    };
    sk_ManifoldKind kind;
} sk_Manifold;

// return indicates success
bool sk_manifoldJoin(sk_Manifold a, sk_Manifold b) {
    if (a.kind == SK_MK_INVALID || b.kind == SK_MK_INVALID) {
        return false;
    }
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
