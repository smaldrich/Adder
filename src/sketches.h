#include <inttypes.h>
#include <memory.h>
#include <stdio.h>

#include "HMM/HandmadeMath.h"
#include "base/allocators.h"
#include "base/options.h"
#include "base/testing.h"

#define SK_MAX_PT_COUNT 10000
#define SK_MAX_CONSTRAINT_COUNT 10000
#define SK_MAX_LINE_COUNT 10000

#define SK_MAX_SOLVE_ITERATIONS 100
#define SK_SOLVE_FAILED_THRESHOLD 0.001f

// does not double eval expr :)
// returns the result of expr if expr evaluates to something other than SKE_OK
#define _SK_VALID_OR_RETURN(expr) \
    do {                          \
        sk_Error e = expr;        \
        if (e != SKE_OK) {        \
            return e;             \
        }                         \
    } while (0)

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
// internally, only valid after calling sk_pointHandleValidate()

typedef enum {
    SK_LK_STRAIGHT,
    SK_LK_ARC,
    // SK_CIRCLE,
    // SK_BEZIER,
} sk_LineKind;

typedef struct {
    sk_PointHandle p1;
    sk_PointHandle p2;
} sk_LineStraight;

typedef struct {
    sk_PointHandle p1;
    sk_PointHandle p2;
    sk_PointHandle center;
} sk_LineArc;
// TODO:/NOTE: this arrangement of an arc being purely geometry means that there are cases where the arc is not circular
// Arcs go counterclockwise from p1 to p2

typedef struct {
    union {
        sk_LineStraight straight;
        sk_LineArc arc;
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
    SK_CK_ANGLE_ARC,
    SK_CK_ARC_UNIFORM,  // makes sure both ends are the same distance from the center // kind of a hack over having two distance constraints // TODO: should it be two distances or not?
    SK_CK_AXIS_ALIGNED,
} sk_ConstraintKind;

typedef struct {
    float length;
    sk_LineHandle line;  // required to be straight
} _sk_ConstraintDistance;

typedef struct {
    sk_LineHandle line1;  // both required to be straight
    sk_LineHandle line2;
    float angle;
} _sk_ConstraintAngleLines;

typedef struct {
    sk_LineHandle arc;
    float angle;
} _sk_ConstraintAngleArc;

typedef struct {
    sk_LineHandle arc;
    float radius;
} _sk_ConstraintArcUniform;

typedef struct {
    sk_LineHandle line;  // required to be straight
} _sk_ConstraintAxisAligned;

typedef struct {
    union {
        _sk_ConstraintDistance dist;
        _sk_ConstraintAngleLines angleLines;
        _sk_ConstraintAngleArc angleArc;
        _sk_ConstraintArcUniform arcUniform;
        _sk_ConstraintAxisAligned axisAligned;
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

OPTION(sk_PointHandle, sk_Error)
OPTION(sk_LineHandle, sk_Error)
OPTION_NAME(sk_Line*, sk_Error, sk_LinePtrOpt)
OPTION_NAME(sk_Point*, sk_Error, sk_PointPtrOpt)
OPTION_NAME(sk_Constraint*, sk_Error, sk_ConstraintPtrOpt)

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

// if successful, fills in the ptr prop of the handle to point directly at the point struct // otherwise sets it to null
sk_Error sk_pointHandleValidate(sk_Sketch* sketch, sk_PointHandle* pointHandle) {
    sk_Point* p = &sketch->points[pointHandle->index];
    sk_Error e = SKE_OK;
    if (!p->inUse) {
        e = SKE_RESOURCE_FREED;
    } else if (p->generation != pointHandle->generation) {
        e = SKE_RESOURCE_FREED;
    }

    if (e == SKE_OK) {
        pointHandle->ptr = p;
    } else {
        pointHandle->ptr = NULL;
    }
    return e;
}

bool sk_pointHandleEq(sk_PointHandle a, sk_PointHandle b) {
    return (a.generation == b.generation) && (a.index == b.index);
}

sk_Error sk_pointRemove(sk_Sketch* sketch, sk_PointHandle pointHandle) {
    _SK_VALID_OR_RETURN(sk_pointHandleValidate(sketch, &pointHandle));
    sk_Point* p = pointHandle.ptr;
    p->inUse = false;
    int32_t gen = p->generation;
    memset(p, 0, sizeof(sk_Point));
    p->generation = gen;
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

sk_LineHandleOpt sk_lineArcPush(sk_Sketch* sketch, sk_PointHandle pt1, sk_PointHandle pt2, sk_PointHandle center) {
    sk_LineHandleOpt l = _sk_linePush(sketch);
    if (l.error != SKE_OK) {
        return (sk_LineHandleOpt){.error = l.error};
    }
    sk_Line* line = &sketch->lines[l.ok.index];
    line->kind = SK_LK_ARC;
    line->variants.arc.p1 = pt1;
    line->variants.arc.p2 = pt2;
    line->variants.arc.center = center;
    return l;
}

// NOTE: only validates the handle to the line, not any of the points inside of the line
// if successful, fills in the ptr prop of the handle to point directly at the point struct // otherwise sets it to null
sk_Error sk_lineHandleValidate(sk_Sketch* sketch, sk_LineHandle* handle) {
    sk_Line* l = &sketch->lines[handle->index];
    sk_Error e = SKE_OK;
    if (!l->inUse) {
        e = SKE_RESOURCE_FREED;
    } else if (l->generation != handle->generation) {
        e = SKE_RESOURCE_FREED;
    }

    if (e == SKE_OK) {
        handle->ptr = l;
    } else {
        handle->ptr = NULL;
    }
    return e;
}

bool sk_lineHandleEq(sk_LineHandle a, sk_LineHandle b) {
    return (a.generation == b.generation) && (a.index == b.index);
}

sk_Error sk_lineRemove(sk_Sketch* sketch, sk_LineHandle handle) {
    _SK_VALID_OR_RETURN(sk_lineHandleValidate(sketch, &handle));
    sk_Line* l = handle.ptr;
    int32_t generation = l->generation;
    memset(l, 0, sizeof(sk_Line));
    l->generation = generation;
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

sk_Error sk_constraintAngleArcPush(sk_Sketch* sketch, float angle, sk_LineHandle arc) {
    sk_ConstraintPtrOpt c = _sk_constraintPush(sketch);
    if (c.error != SKE_OK) {
        return c.error;
    }
    c.ok->kind = SK_CK_ANGLE_ARC;
    _sk_ConstraintAngleArc* a = &c.ok->variants.angleArc;
    a->arc = arc;
    a->angle = angle;
    return SKE_OK;
}

sk_Error sk_constraintArcUniformPush(sk_Sketch* sketch, float radius, sk_LineHandle arc) {
    sk_ConstraintPtrOpt c = _sk_constraintPush(sketch);
    if (c.error != SKE_OK) {
        return c.error;
    }
    c.ok->kind = SK_CK_ARC_UNIFORM;
    _sk_ConstraintArcUniform* a = &c.ok->variants.arcUniform;
    a->arc = arc;
    a->radius = radius;
    return SKE_OK;
}

sk_Error sk_constraintAxisAlignedPush(sk_Sketch* sketch, sk_LineHandle straight) {
    sk_ConstraintPtrOpt c = _sk_constraintPush(sketch);
    if (c.error != SKE_OK) {
        return c.error;
    }
    c.ok->kind = SK_CK_AXIS_ALIGNED;
    _sk_ConstraintAxisAligned* a = &c.ok->variants.axisAligned;
    a->line = straight;
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
            _SK_VALID_OR_RETURN(sk_pointHandleValidate(sketch, &straight->p1));
            _SK_VALID_OR_RETURN(sk_pointHandleValidate(sketch, &straight->p2));
        } else if (l->kind == SK_LK_ARC) {
            sk_LineArc* arc = &l->variants.arc;
            _SK_VALID_OR_RETURN(sk_pointHandleValidate(sketch, &arc->p1));
            _SK_VALID_OR_RETURN(sk_pointHandleValidate(sketch, &arc->p2));
            _SK_VALID_OR_RETURN(sk_pointHandleValidate(sketch, &arc->center));
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
        } else if (a->kind == SK_LK_ARC) {
            sk_LineArc* arc = &a->variants.arc;
            if (sk_pointHandleEq(arc->p1, arc->p2)) {
                return SKE_DUPLICATE_REFERENCES;
            } else if (sk_pointHandleEq(arc->p1, arc->center)) {
                return SKE_DUPLICATE_REFERENCES;
            } else if (sk_pointHandleEq(arc->p2, arc->center)) {
                return SKE_DUPLICATE_REFERENCES;
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
            } else if (a->kind == SK_LK_ARC) {
                sk_LineArc* aArc = &a->variants.arc;
                sk_LineArc* bArc = &b->variants.arc;
                if (sk_pointHandleEq(aArc->center, bArc->center)) {  // different centers cannot be the same arc
                    // check both directions for duplicated point refs
                    if (sk_pointHandleEq(bArc->p1, aArc->p1) && sk_pointHandleEq(bArc->p2, aArc->p2)) {
                        return SKE_DUPLICATE_REFERENCES;
                    } else if (sk_pointHandleEq(bArc->p1, aArc->p2) && sk_pointHandleEq(bArc->p2, aArc->p1)) {
                        return SKE_DUPLICATE_REFERENCES;
                    }
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
            _SK_VALID_OR_RETURN(sk_lineHandleValidate(sketch, &dist->line));
            if (dist->line.ptr->kind != SK_LK_STRAIGHT) {
                return SKE_INVALID_LINE_KIND;
            }
            if (dist->length <= 0) {
                return SKE_INVALID_CONSTRAINT_VALUE;
            }
        } else if (a->kind == SK_CK_ANGLE_LINES) {
            _sk_ConstraintAngleLines* ang = &a->variants.angleLines;
            _SK_VALID_OR_RETURN(sk_lineHandleValidate(sketch, &ang->line1));
            if (ang->line1.ptr->kind != SK_LK_STRAIGHT) {
                return SKE_INVALID_LINE_KIND;
            }
            _SK_VALID_OR_RETURN(sk_lineHandleValidate(sketch, &ang->line2));
            if (ang->line2.ptr->kind != SK_LK_STRAIGHT) {
                return SKE_INVALID_LINE_KIND;
            }
            // TODO: this is missing an error when an angle has value of 0 and lines that share a point
            // TODO: is that an error tho?
        } else if (a->kind == SK_CK_ANGLE_ARC) {
            _sk_ConstraintAngleArc* ang = &a->variants.angleArc;
            _SK_VALID_OR_RETURN(sk_lineHandleValidate(sketch, &ang->arc));
            if (ang->arc.ptr->kind != SK_LK_ARC) {
                return SKE_INVALID_LINE_KIND;
            }
            if (ang->angle <= 0) {
                return SKE_INVALID_CONSTRAINT_VALUE;
            }
        } else if (a->kind == SK_CK_ARC_UNIFORM) {
            _sk_ConstraintArcUniform* uni = &a->variants.arcUniform;
            _SK_VALID_OR_RETURN(sk_lineHandleValidate(sketch, &uni->arc));
            if (uni->arc.ptr->kind != SK_LK_ARC) {
                return SKE_INVALID_LINE_KIND;
            }
            if (uni->radius <= 0) {
                return SKE_INVALID_CONSTRAINT_VALUE;
            }
        } else if (a->kind == SK_CK_AXIS_ALIGNED) {
            _sk_ConstraintAxisAligned* aa = &a->variants.axisAligned;
            _SK_VALID_OR_RETURN(sk_lineHandleValidate(sketch, &aa->line));
            if (aa->line.ptr->kind != SK_LK_STRAIGHT) {
                return SKE_INVALID_LINE_KIND;
            }
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
            } else if (a->kind == SK_CK_ANGLE_ARC) {
                _sk_ConstraintAngleArc* aArc = &a->variants.angleArc;
                _sk_ConstraintAngleArc* bArc = &b->variants.angleArc;
                if (sk_lineHandleEq(aArc->arc, bArc->arc)) {
                    return SKE_DUPLICATE_REFERENCES;
                }
            } else if (a->kind == SK_CK_ARC_UNIFORM) {
                _sk_ConstraintArcUniform* aArc = &a->variants.arcUniform;
                _sk_ConstraintArcUniform* bArc = &b->variants.arcUniform;
                if (sk_lineHandleEq(aArc->arc, bArc->arc)) {
                    return SKE_DUPLICATE_REFERENCES;
                }
            } else if (a->kind == SK_CK_AXIS_ALIGNED) {
                _sk_ConstraintAxisAligned* aAx = &a->variants.axisAligned;
                _sk_ConstraintAxisAligned* bAx = &b->variants.axisAligned;
                if (sk_lineHandleEq(aAx->line, bAx->line)) {
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
// rotates both points around their center by the given angle
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
    HMM_Vec2 l1Rel = HMM_SubV2(*p1b, *p1a);  // p2 relative to p1
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
//  TODO: shouldn't it be calculated after every constraint has been solved for the iteration?
float _sk_solveDistance(HMM_Vec2* p1, HMM_Vec2* p2, float length) {
    HMM_Vec2 diff = HMM_SubV2(*p2, *p1);     // p2 relative to p1
    float error = length - HMM_LenV2(diff);  // positive error = points too close
    // printf("initial error for constraint %d: %f\n", constraintIndex, error);
    HMM_Vec2 pointDelta = HMM_MulV2F(HMM_NormV2(diff), -error / 2);
    *p1 = HMM_AddV2(*p1, pointDelta);
    *p2 = HMM_SubV2(*p2, pointDelta);
    // float postDist = HMM_LenV2(HMM_SubV2(p1->pt, p2->pt));
    // printf("post solve error for constraint %d: %f\n", constraintIndex, fabsf(postDist - c->value));
    // printf("\n");
    return fabsf(error);
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
            float error = _sk_solveDistance(&p1->pt, &p2->pt, d->length);

            if (error > maxError) {
                maxError = error;
            }
        } else if (c->kind == SK_CK_ANGLE_LINES) {
            _sk_ConstraintAngleLines* a = &c->variants.angleLines;
            sk_LineStraight* l1 = &a->line1.ptr->variants.straight;
            sk_LineStraight* l2 = &a->line2.ptr->variants.straight;
            float error = _sk_solveAngle(&l1->p1.ptr->pt,
                                         &l1->p2.ptr->pt,
                                         &l2->p1.ptr->pt,
                                         &l2->p2.ptr->pt,
                                         a->angle);
            if (error > maxError) {
                maxError = error;
            }
        } else if (c->kind == SK_CK_ANGLE_ARC) {
            _sk_ConstraintAngleArc* ang = &c->variants.angleArc;
            sk_LineArc* arc = &ang->arc.ptr->variants.arc;
            float error = _sk_solveAngle(&arc->p1.ptr->pt,
                                         &arc->center.ptr->pt,
                                         &arc->center.ptr->pt,
                                         &arc->p2.ptr->pt,
                                         ang->angle);
            if (error > maxError) {
                maxError = error;
            }
        } else if (c->kind == SK_CK_ARC_UNIFORM) {
            _sk_ConstraintArcUniform* uni = &c->variants.arcUniform;
            sk_LineArc* arc = &uni->arc.ptr->variants.arc;
            float error1 = _sk_solveDistance(&arc->p1.ptr->pt, &arc->center.ptr->pt, uni->radius);
            float error2 = _sk_solveDistance(&arc->p2.ptr->pt, &arc->center.ptr->pt, uni->radius);
            float error = fmaxf(error1, error2);
            if (error > maxError) {
                maxError = error;
            }
        } else if (c->kind == SK_CK_AXIS_ALIGNED) {
            _sk_ConstraintAxisAligned* ax = &c->variants.axisAligned;
            HMM_Vec2* p1 = &ax->line.ptr->variants.straight.p1.ptr->pt;
            HMM_Vec2* p2 = &ax->line.ptr->variants.straight.p2.ptr->pt;
            HMM_Vec2 rel = HMM_SubV2(*p1, *p2);  // P1 RELATIVE TO P2
            float angle = HMM_AngleRad(atan2f(rel.Y, rel.X));

            float target = roundf(angle / 90) * 90;  // round to the closest 90 degree angle
            float error = target - angle;
            _sk_rotateStraightLine(p1, p2, error);

            if (fabsf(error) > maxError) {
                maxError = fabsf(error);
            }
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
        // printf("Error for iteration %d: %f\n", i, maxError);
        if (maxError < SK_SOLVE_FAILED_THRESHOLD) {
            return SKE_OK;
        }
    }
    return SKE_SOLVE_FAILED;
}

void sk_sketchClear(sk_Sketch* sketch) {
    memset(sketch, 0, sizeof(sk_Sketch));
}

void sk_tests() {
    sk_Sketch s;
    printf("\nRunning sketch tests:\n");

    {
        sk_sketchClear(&s);
        sk_PointHandleOpt p1 = sk_pointPush(&s, HMM_V2(-1, -1));
        sk_PointHandleOpt p2 = sk_pointPush(&s, HMM_V2(1, 0));
        sk_PointHandleOpt p3 = sk_pointPush(&s, HMM_V2(1, 1));
        sk_PointHandleOpt p4 = sk_pointPush(&s, HMM_V2(0, 1));

        sk_LineHandleOpt l12 = sk_lineStraightPush(&s, p1.ok, p2.ok);
        sk_LineHandleOpt l23 = sk_lineStraightPush(&s, p2.ok, p3.ok);
        sk_LineHandleOpt l34 = sk_lineStraightPush(&s, p3.ok, p4.ok);
        sk_LineHandleOpt l41 = sk_lineStraightPush(&s, p4.ok, p1.ok);

        sk_constraintDistancePush(&s, 1, l12.ok);
        sk_constraintDistancePush(&s, 1, l23.ok);
        sk_constraintDistancePush(&s, 1, l34.ok);
        sk_constraintDistancePush(&s, 1, l41.ok);

        sk_constraintAngleLinesPush(&s, 90, l12.ok, l23.ok);
        sk_constraintAngleLinesPush(&s, 0, l23.ok, l41.ok);
        sk_constraintAxisAlignedPush(&s, l41.ok);
        sk_constraintAxisAlignedPush(&s, l34.ok);

        test_print(sk_sketchSolve(&s) == SKE_OK, "square");
    }

    {
        sk_sketchClear(&s);
        sk_PointHandleOpt pt = sk_pointPush(&s, HMM_V2(0, 0));
        sk_lineStraightPush(&s, pt.ok, pt.ok);

        test_print(sk_sketchSolve(&s) == SKE_DUPLICATE_REFERENCES, "line with only one point");
    }

    {
        sk_sketchClear(&s);
        sk_PointHandleOpt p1 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_PointHandleOpt p2 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_lineStraightPush(&s, p1.ok, p2.ok);
        sk_lineStraightPush(&s, p2.ok, p1.ok);

        test_print(sk_sketchSolve(&s) == SKE_DUPLICATE_REFERENCES, "two lines across same points");
    }

    {
        sk_sketchClear(&s);
        sk_PointHandleOpt p1 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_PointHandleOpt p2 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_LineHandleOpt line = sk_lineStraightPush(&s, p1.ok, p2.ok);
        sk_constraintDistancePush(&s, 1, line.ok);
        sk_constraintDistancePush(&s, 2, line.ok);

        test_print(sk_sketchSolve(&s) == SKE_DUPLICATE_REFERENCES, "duplicated distance constraint");
    }

    {
        sk_sketchClear(&s);
        sk_PointHandleOpt p1 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_PointHandleOpt p2 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_PointHandleOpt p3 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_LineHandleOpt l12 = sk_lineStraightPush(&s, p1.ok, p2.ok);
        sk_LineHandleOpt l23 = sk_lineStraightPush(&s, p2.ok, p3.ok);
        sk_constraintAngleLinesPush(&s, 90, l12.ok, l23.ok);
        sk_constraintAngleLinesPush(&s, 90, l23.ok, l12.ok);

        test_print(sk_sketchSolve(&s) == SKE_DUPLICATE_REFERENCES, "duplicated angle constraint");
    }

    {
        sk_sketchClear(&s);
        sk_PointHandleOpt p1 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_PointHandleOpt p2 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_LineHandleOpt line = sk_lineStraightPush(&s, p1.ok, p2.ok);
        sk_constraintAxisAlignedPush(&s, line.ok);
        sk_constraintAxisAlignedPush(&s, line.ok);

        test_print(sk_sketchSolve(&s) == SKE_DUPLICATE_REFERENCES, "duplicated axis aligned constraint");
    }

    {
        sk_sketchClear(&s);
        sk_PointHandleOpt p1 = sk_pointPush(&s, HMM_V2(1, 0));
        sk_PointHandleOpt p2 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_PointHandleOpt p3 = sk_pointPush(&s, HMM_V2(-1, 0));
        sk_LineHandleOpt l12 = sk_lineStraightPush(&s, p1.ok, p2.ok);
        sk_LineHandleOpt l23 = sk_lineStraightPush(&s, p2.ok, p3.ok);
        sk_constraintAngleLinesPush(&s, 90, l12.ok, l23.ok);

        test_print(sk_sketchSolve(&s) == SKE_OK, "flat joined angle constraint");
    }

    {
        sk_sketchClear(&s);
        sk_PointHandleOpt p1 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_PointHandleOpt p2 = sk_pointPush(&s, HMM_V2(1, 0));
        sk_LineHandleOpt l = sk_lineStraightPush(&s, p1.ok, p2.ok);
        sk_constraintDistancePush(&s, 2, l.ok);

        sk_pointRemove(&s, p1.ok);
        p1 = sk_pointPush(&s, HMM_V2(0, 0));

        test_print((p1.ok.index == 0 && p1.ok.generation == 2), "point reallocation");
        test_print(sk_sketchSolve(&s) == SKE_RESOURCE_FREED, "reallocated point reference breaks");
    }

    {
        sk_sketchClear(&s);
        sk_PointHandleOpt p1 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_PointHandleOpt p2 = sk_pointPush(&s, HMM_V2(1, 0));
        sk_PointHandleOpt p3 = sk_pointPush(&s, HMM_V2(0, 1));

        sk_LineHandleOpt l12 = sk_lineStraightPush(&s, p1.ok, p2.ok);
        sk_LineHandleOpt l23 = sk_lineStraightPush(&s, p2.ok, p3.ok);
        sk_constraintDistancePush(&s, 1, l12.ok);
        sk_constraintDistancePush(&s, 2, l23.ok);

        sk_lineRemove(&s, l12.ok);
        l12 = sk_lineStraightPush(&s, p1.ok, p2.ok);

        test_print((l12.ok.index == 0 && l12.ok.generation == 2), "line reallocation");
        test_print(sk_sketchSolve(&s) == SKE_RESOURCE_FREED, "reallocated line reference breaks");
    }

    {
        sk_sketchClear(&s);
        sk_PointHandleOpt p1 = sk_pointPush(&s, HMM_V2(0.1, 0));
        sk_PointHandleOpt p2 = sk_pointPush(&s, HMM_V2(0, 0));
        sk_PointHandleOpt p3 = sk_pointPush(&s, HMM_V2(0, -1));
        sk_LineHandleOpt arc = sk_lineArcPush(&s, p1.ok, p3.ok, p2.ok);

        sk_constraintAngleArcPush(&s, 60, arc.ok);
        sk_constraintArcUniformPush(&s, 1, arc.ok);
        test_print(sk_sketchSolve(&s) == SKE_OK, "uniform angled arc");
    }
}