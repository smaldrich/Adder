#include <inttypes.h>
#include <memory.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "base/allocators.h"
#include "base/options.h"

#define SK_MAX_PT_COUNT 10000
#define SK_MAX_CONSTRAINT_COUNT 10000

#define SK_MAX_SOLVE_ITERATIONS 100
#define SK_SOLVE_FAILED_THRESHOLD 0.001f

typedef enum {
    SKE_OK,
    SKE_RESOURCE_FREED,
    SKE_OUT_OF_SPACE,
    SKE_SOLVE_FAILED,
    SKE_INVALID_CONSTRAINT_VALUE,
} sk_Error;

typedef struct {
    HMM_Vec2 pt;
    bool inUse;
    int64_t generation;
} sk_Point;

typedef struct {
    int32_t index;
    int32_t generation;
} sk_HandlePoint;

typedef enum {
    SK_CK_DISTANCE,
    SK_CK_ANGLE,
} sk_ConstraintKind;

typedef struct {
    sk_HandlePoint point1;
    sk_HandlePoint point2;
    sk_ConstraintKind kind;
    float value;
    int32_t generation;
    bool inUse;
} sk_Constraint;

typedef struct {
    sk_Point points[SK_MAX_PT_COUNT];
    sk_Constraint constraints[SK_MAX_CONSTRAINT_COUNT];
} sk_Sketch;

OPTION(sk_HandlePoint, sk_Error);
OPTION_NAME(sk_Point*, sk_Error, sk_PointPtrOpt);
OPTION_NAME(sk_Constraint*, sk_Error, sk_ConstraintPtrOpt);

sk_HandlePointOpt sk_pushPoint(sk_Sketch* sketch, HMM_Vec2 pt) {
    for (int64_t i = 0; i < SK_MAX_PT_COUNT; i++) {
        sk_Point* p = &sketch->points[i];
        if (p->inUse == false) {
            int64_t gen = p->generation;
            memset(p, 0, sizeof(sk_Point));
            p->pt = pt;
            p->inUse = true;
            p->generation = gen + 1;
            return (sk_HandlePointOpt){
                .error = SKE_OK,
                .ok = {.index = i, .generation = p->generation},
            };
        }
    }
    return (sk_HandlePointOpt){.error = SKE_OUT_OF_SPACE};
}

sk_PointPtrOpt sk_getPoint(sk_Sketch* sketch, sk_HandlePoint pointHandle) {
    sk_Point* p = &sketch->points[pointHandle.index];
    sk_Error e = SKE_OK;
    if (!p->inUse) {
        e = SKE_RESOURCE_FREED;
    } else if (p->generation != pointHandle.generation) {
        e = SKE_RESOURCE_FREED;
    }
    return (sk_PointPtrOpt){.ok = (e != SKE_OK) ? NULL : p, .error = e};
}

// void sk_removePoint(sk_Sketch* sketch, sk_HandlePoint pointHandle) {
//     sk_Point* p = sk_getPoint(sketch, pointHandle);
//     p->inUse = false;
//     memset(p, 0, sizeof(p));
// }

sk_ConstraintPtrOpt _sk_pushConstraint(sk_Sketch* sketch) {
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

sk_Error sk_pushDistanceConstraint(sk_Sketch* sketch, float length, sk_HandlePoint p1, sk_HandlePoint p2) {
    sk_ConstraintPtrOpt c = _sk_pushConstraint(sketch);
    if (c.error != SKE_OK) {
        return c.error;
    }
    c.ok->kind = SK_CK_DISTANCE;
    c.ok->point1 = p1;
    c.ok->point2 = p2;
}

// verifies that all constraints in use have valid values and valid point handles
sk_Error _sk_validateConstraints(sk_Sketch* sketch) {
    for (int i = 0; i < SK_MAX_CONSTRAINT_COUNT; i++) {
        sk_Constraint* c = &sketch->constraints[i];
        if (!c->inUse) {
            continue;
        }

        if (c->kind == SK_CK_DISTANCE) {
            sk_PointPtrOpt p1 = sk_getPoint(sketch, c->point1);
            if (p1.error != SKE_OK) {
                return p1.error;
            }
            sk_PointPtrOpt p2 = sk_getPoint(sketch, c->point2);
            if (p2.error != SKE_OK) {
                return p2.error;
            }

            if (c->value <= 0) {
                return SKE_INVALID_CONSTRAINT_VALUE;
            }
        } else {
            assert(false);
        }
    }
    return SKE_OK;
}

// assumes that _sk_validateConstraints has been called (and was OK) and that no constraints or points have been added/removed since
float _sk_solveIteration(sk_Sketch* sketch) {
    float maxError = 0;
    for (int constraintIndex = 0; constraintIndex < SK_MAX_CONSTRAINT_COUNT; constraintIndex++) {
        sk_Constraint* c = &sketch->constraints[constraintIndex];
        if (!c->inUse) {
            continue;
        }

        if (c->kind == SK_CK_DISTANCE) {
            sk_Point* p1 = &sketch->points[c->point1.index];
            sk_Point* p2 = &sketch->points[c->point2.index];
            HMM_Vec2 diff = HMM_SubV2(p2->pt, p1->pt);  // p2 relative to p1

            float error = c->value - HMM_LenV2(diff);  // positive error = points too close
            // printf("initial error for constraint %d: %f\n", constraintIndex, error);
            if (fabsf(error) > maxError) {
                maxError = fabsf(error);
            }

            HMM_Vec2 pointDelta = HMM_MulV2F(HMM_NormV2(diff), -error / 2);
            p1->pt = HMM_AddV2(p1->pt, pointDelta);
            p2->pt = HMM_SubV2(p2->pt, pointDelta);
            // float postDist = HMM_LenV2(HMM_SubV2(p1->pt, p2->pt));
            // printf("post solve error for constraint %d: %f\n", constraintIndex, fabsf(postDist - c->value));
            // printf("\n");
        } else {
            assert(false);
        }
    }
    return maxError;
}

sk_Error sk_solveSketch(sk_Sketch* sketch) {
    sk_Error e = _sk_validateConstraints(sketch);
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

    sk_HandlePointOpt p1 = sk_pushPoint(&s, HMM_V2(0, 0));
    assert(p1.error == SKE_OK);
    sk_HandlePointOpt p2 = sk_pushPoint(&s, HMM_V2(0.001, -0.001));
    assert(p2.error == SKE_OK);
    sk_HandlePointOpt p3 = sk_pushPoint(&s, HMM_V2(-0.1, 0.1));
    assert(p3.error == SKE_OK);

    sk_pushDistanceConstraint(&s, 1, p1.ok, p2.ok);
    sk_pushDistanceConstraint(&s, 2, p2.ok, p3.ok);

    assert(sk_solveSketch(&s) == SKE_OK);
}