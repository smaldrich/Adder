#include <stdbool.h>

#include "HMM/HandmadeMath.h"

typedef struct sk_Point sk_Point;
struct sk_Point {
    HMM_Vec2 pos;
    sk_Point* next;
};

typedef struct {
    sk_Point* firstPoint;
} sk_Sketch;

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

bool sk_solve(sk_Sketch* sketch) {
    for (int i = 0; i < SK_SOLVE_ITERATIONS; i++) {
        float iterationError = 0;
        for (sk_Constraint* c = sketch->firstConstraint; c; c = c->next) {
            if (c->kind == SK_CK_DISTANCE) {
                HMM_Vec2* p1 = &c->line1->p1->pos;
                HMM_Vec2* p2 = &c->line1->p2->pos;

                HMM_Vec2 midPt = HMM_DivV2F(HMM_AddV2(*p1, *p2), 2);
                HMM_Vec2 settled = HMM_SubV2(*p2, *p1);
                settled = HMM_NormV2(settled);
                settled = HMM_MulV2F(settled, c->value);
                *p1 = HMM_AddV2(midPt, settled);
                *p2 = HMM_SubV2(midPt, settled);
            } else if (c->kind == SK_CK_ANGLE_LINES) {
            }
        }  // end constraint loop

        if (iterationError < SK_SOLVE_MAX_ERROR) {
            return true;
        }
    }  // end iteration loop
    return false;
}

bool sk_sketchSolvable() {
}

bool sk_sketchValid() {
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
which *should* always be solvable and never contain duplicate geometry
also no colinear faces (0 area) or points in the same place

a failed add should highlight every other constraint in the chain to the origin that is preventing the add?
favor angles?
cause otherwise it's just every constrain in the sketch
Fusions 'no response' aesthetic is really fuckin annoying

also a better way of visualizing constraints

*/
