#include <inttypes.h>
#include <memory.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "base/allocators.h"

#define SK_MAX_PT_COUNT 10000
#define SK_MAX_CONSTRAINT_COUNT 10000

#define SK_MAX_SOLVE_ITERATIONS 100
#define SK_SOLVE_FAILED_THRESHOLD 0.001f

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
    sk_HandlePoint point3;
    sk_ConstraintKind kind;
    float value;
    int32_t generation;
    bool inUse;
} sk_Constraint;

typedef struct {
    sk_Point points[SK_MAX_PT_COUNT];
    sk_Constraint constraints[SK_MAX_CONSTRAINT_COUNT];
} sk_Sketch;

sk_HandlePoint sk_pushPoint(sk_Sketch* sketch, HMM_Vec2 pt) {
    for (int64_t i = 0; i < SK_MAX_PT_COUNT; i++) {
        sk_Point* p = &sketch->points[i];
        if (p->inUse == false) {
            int64_t gen = p->generation;
            memset(p, 0, sizeof(sk_Point));
            p->pt = pt;
            p->inUse = true;
            p->generation = gen + 1;
            return (sk_HandlePoint){.index = i, .generation = p->generation};
        }
    }
    assert(false);  // no remaining open points in the array, panic
}

sk_Point* sk_getPoint(sk_Sketch* sketch, sk_HandlePoint pointHandle) {
    sk_Point* p = &sketch->points[pointHandle.index];
    assert(p->inUse);
    assert(p->generation == pointHandle.generation);
    return p;
}

// void sk_removePoint(sk_Sketch* sketch, sk_HandlePoint pointHandle) {
//     sk_Point* p = sk_getPoint(sketch, pointHandle);
//     p->inUse = false;
//     memset(p, 0, sizeof(p));
// }

void sk_pushDistanceConstraint(sk_Sketch* sketch, float length, sk_HandlePoint p1, sk_HandlePoint p2) {
    for (int64_t i = 0; i < SK_MAX_CONSTRAINT_COUNT; i++) {
        sk_Constraint* c = &sketch->constraints[i];
        if (c->inUse == false) {
            int64_t gen = c->generation;
            memset(c, 0, sizeof(sk_Constraint));
            c->kind = SK_CK_DISTANCE;
            c->point1 = p1;
            c->point2 = p2;
            c->value = length;
            c->inUse = true;
            c->generation = gen + 1;
            return;
        }
    }
    assert(false);  // no remaining open points in the array, panic
}

float _sk_solveIteration(sk_Sketch* sketch) {
    float maxError = 0;
    for (int constraintIndex = 0; constraintIndex < SK_MAX_CONSTRAINT_COUNT; constraintIndex++) {
        sk_Constraint* c = &sketch->constraints[constraintIndex];
        if (!c->inUse) {
            continue;
        }

        if (c->kind == SK_CK_DISTANCE) {
            sk_Point* p1 = sk_getPoint(sketch, c->point1);
            sk_Point* p2 = sk_getPoint(sketch, c->point2);
            HMM_Vec2 diff = HMM_SubV2(p2->pt, p1->pt);  // p2 relative to p1

            float error = c->value - HMM_LenV2(diff);  // positive error = points too close
            printf("initial error for constraint %d: %f\n", constraintIndex, error);
            if (fabsf(error) > maxError) {
                maxError = fabsf(error);
            }

            HMM_Vec2 pointDelta = HMM_MulV2F(HMM_NormV2(diff), -error / 2);
            p1->pt = HMM_AddV2(p1->pt, pointDelta);
            p2->pt = HMM_SubV2(p2->pt, pointDelta);
            float postDist = HMM_LenV2(HMM_SubV2(p1->pt, p2->pt));
            printf("post solve error for constraint %d: %f\n", constraintIndex, fabsf(postDist - c->value));
            printf("\n");
        } else if (c->kind == SK_CK_ANGLE) {
            assert(false);
        }
    }
    return maxError;
}

void sk_solveSketch(sk_Sketch* sketch) {
    for (int i = 0; i < SK_MAX_SOLVE_ITERATIONS; i++) {
        float maxError = _sk_solveIteration(sketch);
        printf("Error for iteration %d: %f\n", i, maxError);
        if (maxError < SK_SOLVE_FAILED_THRESHOLD) {
            return;
        }
    }
    assert(false);
}

void sk_generateMeshPoints(sk_Sketch* sketch) {
}

void sk_tests() {
    sk_Sketch s;
    memset(&s, 0, sizeof(s));

    sk_HandlePoint p1 = sk_pushPoint(&s, HMM_V2(0, 0));
    sk_HandlePoint p2 = sk_pushPoint(&s, HMM_V2(0.001, -0.001));
    sk_HandlePoint p3 = sk_pushPoint(&s, HMM_V2(-0.1, 0.1));

    sk_pushDistanceConstraint(&s, 1, p1, p2);
    sk_pushDistanceConstraint(&s, 2, p2, p3);

    sk_solveSketch(&s);
}