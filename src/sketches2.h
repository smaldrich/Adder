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

void sk_solve(sk_Sketch* sketch) {
    for (int i = 0; i < 1000; i++) {
        for (sk_Constraint* c = sketch->firstConstraint; c; c = c->next) {
            if (c->kind == SK_CK_DISTANCE) {
            } else if (c->kind == SK_CK_ANGLE_LINES) {
            }
        }
    }
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
