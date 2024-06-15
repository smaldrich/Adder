#include <inttypes.h>
#include <memory.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "base/allocators.h"
#include "base/options.h"

#define SK_MAX_PT_COUNT 10000
#define SK_MAX_CONSTRAINT_COUNT 10000
#define SK_MAX_LINE_COUNT 10000

#define SK_MAX_SOLVE_ITERATIONS 100
#define SK_SOLVE_FAILED_THRESHOLD 0.001f

typedef enum {
    SKE_OK,
    SKE_RESOURCE_FREED,
    SKE_OUT_OF_SPACE,
    SKE_SOLVE_FAILED,
    SKE_INVALID_CONSTRAINT_VALUE,
    SKE_INVALID_LINE_KIND,
    SKE_DUPLICATE_REFERENCES,
} sk_Error;

typedef struct {
    HMM_Vec2 pt;
    bool inUse;
    int32_t generation;
} sk_Point;
// inUse and genetration should only be edited by sk_pointXXX functions

typedef struct {
    int32_t index;
    int32_t generation;
    sk_Point* ptr;
} sk_PointHandle;
// UI should should not change this
// ptr is only valid after an SKE_OK result from _sk_sketchValidate, UI code should not touch

typedef enum {
    SK_LK_STRAIGHT,
    // SK_ARC,
    // SK_CIRCLE,
    // SK_BEZIER,
} sk_LineKind;

typedef struct {
    sk_PointHandle p1;
    sk_PointHandle p2;
} sk_LineStraight;

typedef struct {
    union {
        sk_LineStraight straight;
    } variants;
    sk_LineKind kind;
    int32_t generation;
    bool inUse;
} sk_Line;
// Kind should be considered immutable after construction

typedef struct {
    int32_t index;
    int32_t generation;
    sk_Line* ptr;
} sk_LineHandle;

typedef enum {
    SK_CK_DISTANCE,
    SK_CK_ANGLE_LINES,
} sk_ConstraintKind;

typedef struct {
    float length;
    sk_LineHandle line;
} _sk_ConstraintDistance;

typedef struct {
    sk_LineHandle line1;
    sk_LineHandle line2;
    float angle;
} _sk_ConstraintAngleLines;

typedef struct {
    union {
        _sk_ConstraintDistance dist;
        _sk_ConstraintAngleLines angleLines;
    } variants;
    sk_ConstraintKind kind;
    int32_t generation;
    bool inUse;
} sk_Constraint;
// Kind should be considered immutable after construction

typedef struct {
    sk_Point points[SK_MAX_PT_COUNT];
    sk_Constraint constraints[SK_MAX_CONSTRAINT_COUNT];
    sk_Line lines[SK_MAX_LINE_COUNT];
} sk_Sketch;

OPTION(sk_PointHandle, sk_Error);
OPTION(sk_LineHandle, sk_Error);
OPTION_NAME(sk_Line*, sk_Error, sk_LinePtrOpt);
OPTION_NAME(sk_Point*, sk_Error, sk_PointPtrOpt);
OPTION_NAME(sk_Constraint*, sk_Error, sk_ConstraintPtrOpt);

/////////////////////
// POINTS
/////////////////////

sk_PointHandleOpt sk_pointPush(sk_Sketch* sketch, HMM_Vec2 pt) {
    for (int64_t i = 0; i < SK_MAX_PT_COUNT; i++) {
        sk_Point* p = &sketch->points[i];
        if (p->inUse == false) {
            int64_t gen = p->generation;
            memset(p, 0, sizeof(sk_Point));
            p->pt = pt;
            p->inUse = true;
            p->generation = gen + 1;
            return (sk_PointHandleOpt){
                .error = SKE_OK,
                .ok = {.index = i, .generation = p->generation},
            };
        }
    }
    return (sk_PointHandleOpt){.error = SKE_OUT_OF_SPACE};
}

sk_PointPtrOpt sk_pointGet(sk_Sketch* sketch, sk_PointHandle pointHandle) {
    sk_Point* p = &sketch->points[pointHandle.index];
    sk_Error e = SKE_OK;
    if (!p->inUse) {
        e = SKE_RESOURCE_FREED;
    } else if (p->generation != pointHandle.generation) {
        e = SKE_RESOURCE_FREED;
    }
    return (sk_PointPtrOpt){.ok = (e != SKE_OK) ? NULL : p, .error = e};
}

bool sk_pointHandleEq(sk_PointHandle a, sk_PointHandle b) {
    return (a.generation == b.generation) && (a.index == b.index);
}

sk_Error sk_pointRemove(sk_Sketch* sketch, sk_PointHandle pointHandle) {
    sk_PointPtrOpt p = sk_pointGet(sketch, pointHandle);
    if (p.error != SKE_OK) {
        return p.error;
    }
    p.ok->inUse = false;
    memset(p.ok, 0, sizeof(sk_Point));
    return SKE_OK;
}

/////////////////////
// LINES
/////////////////////

sk_LineHandleOpt _sk_linePush(sk_Sketch* sketch) {
    for (int64_t i = 0; i < SK_MAX_LINE_COUNT; i++) {
        sk_Line* l = &sketch->lines[i];
        if (l->inUse == false) {
            int64_t gen = l->generation;
            memset(l, 0, sizeof(sk_Line));
            l->inUse = true;
            l->generation = gen + 1;
            return (sk_LineHandleOpt){
                .error = SKE_OK,
                .ok = (sk_LineHandle){.generation = l->generation, .index = i},
            };
        }
    }
    return (sk_LineHandleOpt){.error = SKE_OUT_OF_SPACE};
}

sk_LineHandleOpt sk_lineStraightPush(sk_Sketch* sketch, sk_PointHandle pt1, sk_PointHandle pt2) {
    sk_LineHandleOpt l = _sk_linePush(sketch);
    if (l.error != SKE_OK) {
        return (sk_LineHandleOpt){.error = l.error};
    }
    sk_Line* line = &sketch->lines[l.ok.index];
    line->kind = SK_LK_STRAIGHT;
    line->variants.straight.p1 = pt1;
    line->variants.straight.p2 = pt2;

    return l;
}

// NOTE: only validates the handle to the line, not any points inside of the line
sk_LinePtrOpt sk_lineGet(sk_Sketch* sketch, sk_LineHandle handle) {
    sk_Line* l = &sketch->lines[handle.index];
    sk_Error e = SKE_OK;
    if (!l->inUse) {
        e = SKE_RESOURCE_FREED;
    } else if (l->generation != handle.generation) {
        e = SKE_RESOURCE_FREED;
    }
    return (sk_LinePtrOpt){.ok = (e != SKE_OK) ? NULL : l, .error = e};
}

bool sk_lineHandleEq(sk_LineHandle a, sk_LineHandle b) {
    return (a.generation == b.generation) && (a.index == b.index);
}

sk_Error sk_lineRemove(sk_Sketch* sketch, sk_LineHandle handle) {
    sk_LinePtrOpt l = sk_lineGet(sketch, handle);
    if (l.error != SKE_OK) {
        return l.error;
    }
    l.ok->inUse = false;
    memset(l.ok, 0, sizeof(sk_Line));
    return SKE_OK;
}

/////////////////////
// CONSTRAINTS
/////////////////////

sk_ConstraintPtrOpt _sk_constraintPush(sk_Sketch* sketch) {
    for (int64_t i = 0; i < SK_MAX_CONSTRAINT_COUNT; i++) {
        sk_Constraint* c = &sketch->constraints[i];
        if (c->inUse == false) {
            int64_t gen = c->generation;
            memset(c, 0, sizeof(sk_Constraint));
            c->inUse = true;
            c->generation = gen + 1;
            return (sk_ConstraintPtrOpt){.error = SKE_OK, .ok = c};
        }
    }
    return (sk_ConstraintPtrOpt){.error = SKE_OUT_OF_SPACE};
}

sk_Error sk_constraintDistancePush(sk_Sketch* sketch, float length, sk_LineHandle line) {
    sk_ConstraintPtrOpt c = _sk_constraintPush(sketch);
    if (c.error != SKE_OK) {
        return c.error;
    }
    c.ok->kind = SK_CK_DISTANCE;
    _sk_ConstraintDistance* d = &c.ok->variants.dist;
    d->line = line;
    d->length = length;
    return SKE_OK;
}

sk_Error sk_constraintAngleLinesPush(sk_Sketch* sketch, float angle, sk_LineHandle line1, sk_LineHandle line2) {
    sk_ConstraintPtrOpt c = _sk_constraintPush(sketch);
    if (c.error != SKE_OK) {
        return c.error;
    }
    c.ok->kind = SK_CK_ANGLE_LINES;
    _sk_ConstraintAngleLines* a = &c.ok->variants.angleLines;
    a->line1 = line1;
    a->line2 = line2;
    a->angle = angle;
    return SKE_OK;
}

// verifies that all constraints in use have valid values and valid point handles
// does line/constraint duplication checks - which aren't handled at push time
// editing behaviour of this will likely change the correctness of _sk_solveIteration, because it directly relies on this
// things like the allowed kinds of lines for a constraint are notable, because they aren't checked in _sk_solveIteration
sk_Error _sk_sketchValidate(sk_Sketch* sketch) {
    // check all line refs, fill in direct pointers
    for (int i = 0; i < SK_MAX_LINE_COUNT; i++) {
        sk_Line* l = &sketch->lines[i];
        if (!l->inUse) {
            continue;
        }

        if (l->kind == SK_LK_STRAIGHT) {
            sk_LineStraight* straight = &l->variants.straight;
            {
                sk_PointPtrOpt p1 = sk_pointGet(sketch, straight->p1);
                if (p1.error != SKE_OK) {
                    return p1.error;
                }
                straight->p1.ptr = p1.ok;
            }
            {
                sk_PointPtrOpt p2 = sk_pointGet(sketch, straight->p2);
                if (p2.error != SKE_OK) {
                    return p2.error;
                }
                straight->p2.ptr = p2.ok;
            }
        } else {
            assert(false);
        }
    }

    // check for line duplicates and lines that go to and from the same point
    for (int aIdx = 0; aIdx < SK_MAX_LINE_COUNT; aIdx++) {
        sk_Line* a = &sketch->lines[aIdx];
        if (!a->inUse) {
            continue;
        }

        if (a->kind == SK_LK_STRAIGHT) {
            // if same start and end, error
            if (sk_pointHandleEq(a->variants.straight.p1, a->variants.straight.p2)) {
                return SKE_DUPLICATE_REFERENCES;  // TODO: is this a good error to report?
            }
        } else {
            assert(false);
        }

        // check for duplicates
        for (int bIdx = 0; bIdx < SK_MAX_LINE_COUNT; bIdx++) {
            sk_Line* b = &sketch->lines[bIdx];
            if (!b->inUse) {  // skip unused
                continue;
            } else if (b->kind != a->kind) {  // check A and B are the same kind
                continue;
            } else if (bIdx == aIdx) {  // dont check against self
                continue;
            }

            // A and B are the same kind at this point, so just check A's
            if (a->kind == SK_LK_STRAIGHT) {
                sk_LineStraight* as = &a->variants.straight;
                sk_LineStraight* bs = &b->variants.straight;
                if (sk_pointHandleEq(bs->p1, as->p1) && sk_pointHandleEq(bs->p2, as->p2)) {
                    return SKE_DUPLICATE_REFERENCES;
                } else if (sk_pointHandleEq(bs->p1, as->p2) && sk_pointHandleEq(bs->p2, as->p1)) {
                    return SKE_DUPLICATE_REFERENCES;
                }
            } else {
                assert(false);
            }
        }
    }

    // check all constraint refs - for correct kind as well, fill in direct ptrs
    for (int aIdx = 0; aIdx < SK_MAX_CONSTRAINT_COUNT; aIdx++) {
        sk_Constraint* a = &sketch->constraints[aIdx];
        if (!a->inUse) {
            continue;
        }

        if (a->kind == SK_CK_DISTANCE) {
            _sk_ConstraintDistance* dist = &a->variants.dist;
            sk_LinePtrOpt e = sk_lineGet(sketch, dist->line);
            if (e.error != SKE_OK) {
                return e.error;
            }
            if (e.ok->kind != SK_LK_STRAIGHT) {
                return SKE_INVALID_LINE_KIND;
            }
            if (dist->length <= 0) {
                return SKE_INVALID_CONSTRAINT_VALUE;
            }
            dist->line.ptr = e.ok;
        } else if (a->kind == SK_CK_ANGLE_LINES) {
            _sk_ConstraintAngleLines* ang = &a->variants.angleLines;
            {
                sk_LinePtrOpt l1 = sk_lineGet(sketch, ang->line1);
                if (l1.error != SKE_OK) {
                    return l1.error;
                }
                if (l1.ok->kind != SK_LK_STRAIGHT) {  // NOTE: making sure child lines are straight is important and relied on later
                    return SKE_INVALID_LINE_KIND;
                }
                ang->line1.ptr = l1.ok;
            }
            {
                sk_LinePtrOpt l2 = sk_lineGet(sketch, ang->line2);
                if (l2.error != SKE_OK) {
                    return l2.error;
                }
                if (l2.ok->kind != SK_LK_STRAIGHT) {
                    return SKE_INVALID_LINE_KIND;
                }
                ang->line2.ptr = l2.ok;
            }

            // TODO: this is missing an error when an angle has value of 0 and lines that share a point
            // TODO: is that an error tho?
        } else {
            assert(false);
        }

        // check for duplicates of this constraint
        for (int bIdx = 0; bIdx < SK_MAX_CONSTRAINT_COUNT; bIdx++) {
            sk_Constraint* b = &sketch->constraints[bIdx];
            if (!b->inUse) {
                continue;
            } else if (b->kind != a->kind) {
                continue;
            } else if (bIdx == aIdx) {
                continue;
            }

            // A and B at this point have the same kind, so only one needs to be checked
            if (a->kind == SK_CK_DISTANCE) {
                // lines have already been deduplicated, so checking for the same line is correct
                if (sk_lineHandleEq(a->variants.dist.line, b->variants.dist.line)) {
                    return SKE_DUPLICATE_REFERENCES;
                }
            } else if (a->kind == SK_CK_ANGLE_LINES) {
                _sk_ConstraintAngleLines* al = &a->variants.angleLines;
                _sk_ConstraintAngleLines* bl = &b->variants.angleLines;
                // check if angle constraints have the same lines as each other
                if (sk_lineHandleEq(al->line1, bl->line1) && sk_lineHandleEq(al->line2, bl->line2)) {
                    return SKE_DUPLICATE_REFERENCES;
                } else if (sk_lineHandleEq(al->line1, bl->line2) && sk_lineHandleEq(al->line2, bl->line1)) {
                    return SKE_DUPLICATE_REFERENCES;
                }
            } else {
                assert(false);
            }
        }
    }

    return SKE_OK;
}

float _sk_normalizeAngle(float a) {
    while (a > 180) {
        a -= 360;
    }
    while (a < -180) {
        a += 360;
    }
    return a;
}

// p1 and p2 are both read from and written to
// rotates both points around their center by the given angle
void _sk_rotateStraightLine(HMM_Vec2* p1, HMM_Vec2* p2, float angle) {
    HMM_Vec2 center = HMM_DivV2F(HMM_AddV2(*p1, *p2), 2.0f);
    HMM_Vec2 rel = HMM_SubV2(*p1, center);  // p1 relative to center
    HMM_Vec2 rotated = HMM_RotateV2(rel, angle);
    *p1 = HMM_AddV2(center, rotated);
    *p2 = HMM_AddV2(center, HMM_SubV2(HMM_V2(0, 0), rotated));
}

// assumes that _sk_sketchValidate has been called (and was OK) and that no constraints or points or lines have been added/removed since (and that no kinds have changed)
// relies on the kinds of lines for each constraint being checked and correct
float _sk_solveIteration(sk_Sketch* sketch) {
    float maxError = 0;
    for (int constraintIndex = 0; constraintIndex < SK_MAX_CONSTRAINT_COUNT; constraintIndex++) {
        sk_Constraint* c = &sketch->constraints[constraintIndex];
        if (!c->inUse) {
            continue;
        }

        if (c->kind == SK_CK_DISTANCE) {
            _sk_ConstraintDistance* d = &c->variants.dist;
            sk_Point* p1 = d->line.ptr->variants.straight.p1.ptr;
            sk_Point* p2 = d->line.ptr->variants.straight.p2.ptr;
            HMM_Vec2 diff = HMM_SubV2(p2->pt, p1->pt);  // p2 relative to p1

            float error = d->length - HMM_LenV2(diff);  // positive error = points too close
            // printf("initial error for constraint %d: %f\n", constraintIndex, error);
            if (fabsf(error) > maxError) {
                maxError = fabsf(error);  // TODO: I believe that calculating this now is wrong, and it should be done after every constraint has been solved, but it's good enough for now
            }

            HMM_Vec2 pointDelta = HMM_MulV2F(HMM_NormV2(diff), -error / 2);
            p1->pt = HMM_AddV2(p1->pt, pointDelta);
            p2->pt = HMM_SubV2(p2->pt, pointDelta);
            // float postDist = HMM_LenV2(HMM_SubV2(p1->pt, p2->pt));
            // printf("post solve error for constraint %d: %f\n", constraintIndex, fabsf(postDist - c->value));
            // printf("\n");
        } else if (c->kind == SK_CK_ANGLE_LINES) {
            _sk_ConstraintAngleLines* a = &c->variants.angleLines;
            sk_LineStraight* l1 = &a->line1.ptr->variants.straight;
            sk_LineStraight* l2 = &a->line2.ptr->variants.straight;
            HMM_Vec2* l1a = &l1->p1.ptr->pt;
            HMM_Vec2* l1b = &l1->p2.ptr->pt;
            HMM_Vec2* l2a = &l2->p1.ptr->pt;
            HMM_Vec2* l2b = &l2->p2.ptr->pt;

            HMM_Vec2 l1Rel = HMM_SubV2(*l1b, *l1a);  // p2 relative to p1
            HMM_Vec2 l2Rel = HMM_SubV2(*l2b, *l2a);
            float l1Angle = HMM_AngleRad(atan2f(l1Rel.Y, l1Rel.X));
            float l2Angle = HMM_AngleRad(atan2f(l2Rel.Y, l2Rel.X));
            float relAngle = _sk_normalizeAngle(l2Angle - l1Angle);  // l2 relative to l1
            float error = fabsf(relAngle) - a->angle;                // positive indicates points too far

            if (fabsf(error) > maxError) {  // TODO: see above todo in the distance constraint solve
                maxError = fabsf(error);
            }

            float relAngleNormalized = relAngle;
            if (relAngleNormalized > 0) {
                relAngleNormalized = 1;
            } else if (relAngleNormalized < 0) {
                relAngleNormalized = -1;
            } else {
                assert(false);
            }

            // apply calculated rotations to the points
            float rot = relAngleNormalized * (error / 2);
            _sk_rotateStraightLine(l1a, l1b, rot);
            _sk_rotateStraightLine(l2a, l2b, -rot);
        } else {
            assert(false);
        }
    }
    return maxError;
}

sk_Error sk_sketchSolve(sk_Sketch* sketch) {
    // TODO: cull bad dependencies instead of stopping
    sk_Error e = _sk_sketchValidate(sketch);
    if (e != SKE_OK) {
        return e;
    }

    for (int i = 0; i < SK_MAX_SOLVE_ITERATIONS; i++) {
        float maxError = _sk_solveIteration(sketch);
        printf("Error for iteration %d: %f\n", i, maxError);
        if (maxError < SK_SOLVE_FAILED_THRESHOLD) {
            return SKE_OK;
        }
    }
    return SKE_SOLVE_FAILED;
}

void sk_tests() {
    sk_Sketch s;
    memset(&s, 0, sizeof(s));

    sk_PointHandleOpt p1 = sk_pointPush(&s, HMM_V2(-1, -1));
    sk_PointHandleOpt p2 = sk_pointPush(&s, HMM_V2(1, 0));
    sk_PointHandleOpt p3 = sk_pointPush(&s, HMM_V2(1, 1));
    sk_PointHandleOpt p4 = sk_pointPush(&s, HMM_V2(0, 1));
    assert(p1.error == SKE_OK);
    assert(p2.error == SKE_OK);
    assert(p3.error == SKE_OK);
    assert(p4.error == SKE_OK);

    sk_LineHandleOpt l12 = sk_lineStraightPush(&s, p1.ok, p2.ok);
    sk_LineHandleOpt l23 = sk_lineStraightPush(&s, p2.ok, p3.ok);
    sk_LineHandleOpt l34 = sk_lineStraightPush(&s, p3.ok, p4.ok);
    sk_LineHandleOpt l41 = sk_lineStraightPush(&s, p4.ok, p1.ok);
    assert(l12.error == SKE_OK);
    assert(l23.error == SKE_OK);
    assert(l34.error == SKE_OK);
    assert(l41.error == SKE_OK);

    sk_constraintDistancePush(&s, 1, l12.ok);
    sk_constraintDistancePush(&s, 1, l23.ok);
    sk_constraintDistancePush(&s, 1, l34.ok);
    sk_constraintDistancePush(&s, 1, l41.ok);
    sk_constraintAngleLinesPush(&s, 90, l12.ok, l23.ok);
    sk_constraintAngleLinesPush(&s, 90, l23.ok, l34.ok);
    sk_constraintAngleLinesPush(&s, 90, l34.ok, l41.ok);
    sk_constraintAngleLinesPush(&s, 90, l41.ok, l12.ok);

    assert(sk_sketchSolve(&s) == SKE_OK);
}