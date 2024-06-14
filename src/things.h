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
    SK_CK_ANGLE3,
} sk_ConstraintKind;

typedef struct {
    sk_HandlePoint point1;
    sk_HandlePoint point2;
    float length;
} _sk_ConstraintDistance;
typedef struct {
    sk_HandlePoint point1;
    sk_HandlePoint point2;
    sk_HandlePoint centerPoint;
    float angle;
} _sk_ConstraintAngle3;

typedef struct {
    union {
        _sk_ConstraintDistance dist;
        _sk_ConstraintAngle3 angle3;
    } variants;
    sk_ConstraintKind kind;
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

sk_Error sk_removePoint(sk_Sketch* sketch, sk_HandlePoint pointHandle) {
    sk_PointPtrOpt p = sk_getPoint(sketch, pointHandle);
    if (p.error != SKE_OK) {
        return p.error;
    }
    p.ok->inUse = false;
    memset(p.ok, 0, sizeof(sk_Point));
    return SKE_OK;
}

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
    _sk_ConstraintDistance* d = &c.ok->variants.dist;
    d->point1 = p1;
    d->point2 = p2;
    d->length = length;
    return SKE_OK;
}

sk_Error sk_pushAngle3Constraint(sk_Sketch* sketch, float angle, sk_HandlePoint p1, sk_HandlePoint p2, sk_HandlePoint center) {
    sk_ConstraintPtrOpt c = _sk_pushConstraint(sketch);
    if (c.error != SKE_OK) {
        return c.error;
    }
    c.ok->kind = SK_CK_ANGLE3;
    _sk_ConstraintAngle3* a = &c.ok->variants.angle3;
    a->point1 = p1;
    a->point2 = p2;
    a->centerPoint = center;
    a->angle = angle;
    return SKE_OK;
}

// verifies that all constraints in use have valid values and valid point handles
sk_Error _sk_validateConstraints(sk_Sketch* sketch) {
    for (int i = 0; i < SK_MAX_CONSTRAINT_COUNT; i++) {
        sk_Constraint* c = &sketch->constraints[i];
        if (!c->inUse) {
            continue;
        }

        if (c->kind == SK_CK_DISTANCE) {
            _sk_ConstraintDistance* dist = &c->variants.dist;
            sk_PointPtrOpt p1 = sk_getPoint(sketch, dist->point1);
            if (p1.error != SKE_OK) {
                return p1.error;
            }
            sk_PointPtrOpt p2 = sk_getPoint(sketch, dist->point2);
            if (p2.error != SKE_OK) {
                return p2.error;
            }
            if (dist->length <= 0) {
                return SKE_INVALID_CONSTRAINT_VALUE;
            }
        } else if (c->kind == SK_CK_ANGLE3) {
            _sk_ConstraintAngle3* ang = &c->variants.angle3;
            sk_PointPtrOpt p1 = sk_getPoint(sketch, ang->point1);
            if (p1.error != SKE_OK) {
                return p1.error;
            }
            sk_PointPtrOpt p2 = sk_getPoint(sketch, ang->point2);
            if (p2.error != SKE_OK) {
                return p2.error;
            }
            sk_PointPtrOpt p3 = sk_getPoint(sketch, ang->centerPoint);
            if (p3.error != SKE_OK) {
                return p3.error;
            }
            if (ang->angle == 0) {
                return SKE_INVALID_CONSTRAINT_VALUE;
            }
        } else {
            assert(false);
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

// assumes that _sk_validateConstraints has been called (and was OK) and that no constraints or points have been added/removed since
float _sk_solveIteration(sk_Sketch* sketch) {
    float maxError = 0;
    for (int constraintIndex = 0; constraintIndex < SK_MAX_CONSTRAINT_COUNT; constraintIndex++) {
        sk_Constraint* c = &sketch->constraints[constraintIndex];
        if (!c->inUse) {
            continue;
        }

        if (c->kind == SK_CK_DISTANCE) {
            _sk_ConstraintDistance* d = &c->variants.dist;
            sk_Point* p1 = &sketch->points[d->point1.index];
            sk_Point* p2 = &sketch->points[d->point2.index];
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
        } else if (c->kind == SK_CK_ANGLE3) {
            _sk_ConstraintAngle3* a = &c->variants.angle3;
            sk_Point* p1 = &sketch->points[a->point1.index];
            sk_Point* p2 = &sketch->points[a->point2.index];
            sk_Point* center = &sketch->points[a->centerPoint.index];

            HMM_Vec2 p1Rel = HMM_SubV2(p1->pt, center->pt);
            HMM_Vec2 p2Rel = HMM_SubV2(p2->pt, center->pt);
            float p1Angle = HMM_AngleRad(atan2f(p1Rel.Y, p1Rel.X));
            float p2Angle = HMM_AngleRad(atan2f(p2Rel.Y, p2Rel.X));
            float relAngle = _sk_normalizeAngle(p2Angle - p1Angle);
            float error = fabsf(relAngle) - a->angle;  // positive indicates points too far

            if (fabsf(error) > maxError) {  // see TODO above this
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
            p1->pt = HMM_RotateV2(HMM_V2(HMM_LenV2(p1Rel), 0), (p1Angle + relAngleNormalized * (error / 2)));
            p2->pt = HMM_RotateV2(HMM_V2(HMM_LenV2(p2Rel), 0), (p2Angle - relAngleNormalized * (error / 2)));
            p1->pt = HMM_AddV2(p1->pt, center->pt);
            p2->pt = HMM_AddV2(p2->pt, center->pt);
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

    sk_HandlePointOpt p1 = sk_pushPoint(&s, HMM_V2(-1, -1));
    assert(p1.error == SKE_OK);
    sk_HandlePointOpt p2 = sk_pushPoint(&s, HMM_V2(1, 0));
    assert(p2.error == SKE_OK);
    sk_HandlePointOpt p3 = sk_pushPoint(&s, HMM_V2(1, 1));
    assert(p3.error == SKE_OK);
    sk_HandlePointOpt p4 = sk_pushPoint(&s, HMM_V2(0, 1));
    assert(p3.error == SKE_OK);

    sk_pushDistanceConstraint(&s, 1, p1.ok, p2.ok);
    sk_pushDistanceConstraint(&s, 1, p2.ok, p3.ok);
    sk_pushDistanceConstraint(&s, 1, p3.ok, p4.ok);
    sk_pushDistanceConstraint(&s, 1, p4.ok, p1.ok);

    sk_pushAngle3Constraint(&s, 90, p3.ok, p1.ok, p2.ok);
    sk_pushAngle3Constraint(&s, 90, p2.ok, p4.ok, p3.ok);
    sk_pushAngle3Constraint(&s, 90, p1.ok, p3.ok, p4.ok);
    sk_pushAngle3Constraint(&s, 90, p2.ok, p4.ok, p1.ok);

    assert(sk_solveSketch(&s) == SKE_OK);
}