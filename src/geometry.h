#pragma once

typedef struct {
    union {
        struct {
            HMM_Vec3 a;
            HMM_Vec3 b;
            HMM_Vec3 c;
        };
        HMM_Vec3 elems[3];
    };
} geo_Tri;

SNZ_SLICE(geo_Tri);

typedef struct {
    HMM_Vec3 a;
    HMM_Vec3 b;
} geo_Line;

SNZ_SLICE(geo_Line);

#define geo_EPSILON 0.0001

bool geo_floatZero(float a) {
    return fabsf(a) < geo_EPSILON;
}

bool geo_floatEqual(float a, float b) {
    return geo_floatZero(a - b);
}

bool geo_floatGreater(float a, float b) {
    return !geo_floatEqual(a, b) && a > b;
}

bool geo_floatLess(float a, float b) {
    return !geo_floatEqual(a, b) && a < b;
}

bool geo_floatLessEqual(float a, float b) {
    return geo_floatEqual(a, b) || a < b;
}

bool geo_floatGreaterEqual(float a, float b) {
    return geo_floatEqual(a, b) || a > b;
}

bool geo_v2Equal(HMM_Vec2 a, HMM_Vec2 b) {
    return geo_floatEqual(a.X, b.X) && geo_floatEqual(a.Y, b.Y);
}

bool geo_v3Equal(HMM_Vec3 a, HMM_Vec3 b) {
    return geo_floatEqual(a.X, b.X) && geo_floatEqual(a.Y, b.Y) && geo_floatEqual(a.Z, b.Z);
}

typedef struct {
    HMM_Vec3 pt;
    HMM_Vec3 normal;
    HMM_Vec3 vertical;
} geo_Align;

geo_Align geo_alignZero() {
    return (geo_Align) { .pt = HMM_V3(0, 0, 0), .normal = HMM_V3(0, 0, 1), .vertical = HMM_V3(0, 1, 0) };
}

void geo_alignAssertValid(const geo_Align* a) {
    SNZ_ASSERT(!isnan(a->pt.X), "align point x was NaN.");
    SNZ_ASSERT(!isnan(a->normal.X), "align normal x was NaN.");
    SNZ_ASSERT(!isnan(a->vertical.X), "align vertical x was NaN.");
    float dot = HMM_Dot(a->normal, a->vertical);
    SNZ_ASSERTF(geo_floatZero(dot), "align dot prod between normal and vertical non-zero. was: %f", dot);

    float normalLen = HMM_Len(a->normal);
    SNZ_ASSERTF(geo_floatEqual(normalLen, 1), "align normal length invalid. was: %f, expected: 1.0", normalLen);
    float verticalLen = HMM_Len(a->vertical);
    SNZ_ASSERTF(geo_floatEqual(verticalLen, 1), "align vertical length invalid. was: %f, expected: 1.0", verticalLen);
}

static float _geo_angleBetweenV3(HMM_Vec3 a, HMM_Vec3 b) {
    return acosf(HMM_Dot(a, b) / (HMM_Len(a) * HMM_Len(b)));
}

HMM_Quat geo_alignToQuat(geo_Align a, geo_Align b) {
    HMM_Vec3 normalCross = HMM_Cross(a.normal, b.normal);
    if (geo_v3Equal(normalCross, HMM_V3(0, 0, 0))) { // normals are equal or opposite, angle between is either 0 or 180
        normalCross = HMM_Cross(a.normal, HMM_Add(a.normal, HMM_V3(0.1, 0, 0))); // vector perpendicular to both
    }
    float normalAngle = _geo_angleBetweenV3(a.normal, b.normal);
    HMM_Quat planeRotate = HMM_QFromAxisAngle_RH(normalCross, normalAngle);
    if (geo_floatEqual(normalAngle, 0)) {
        planeRotate = HMM_QFromAxisAngle_RH(HMM_V3(0, 0, 1), 0);
    }

    HMM_Vec3 postRotateVertical = HMM_MulM4V4(HMM_QToM4(planeRotate), HMM_V4(a.vertical.X, a.vertical.Y, a.vertical.Z, 1)).XYZ;
    // stolen: https://stackoverflow.com/questions/5188561/signed-angle-between-two-3d-vectors-with-same-origin-within-the-same-plane
    // tysm internet
    float y = HMM_Dot(HMM_Cross(postRotateVertical, b.vertical), b.normal);
    float x = HMM_Dot(postRotateVertical, b.vertical);
    float postRotateAngle = atan2(y, x);
    HMM_Quat postRotate = HMM_QFromAxisAngle_RH(b.normal, postRotateAngle);

    return HMM_MulQ(postRotate, planeRotate);
}

HMM_Mat4 geo_alignToM4(geo_Align a, geo_Align b) {
    HMM_Quat q = geo_alignToQuat(a, b);
    HMM_Mat4 translate = HMM_Translate(HMM_Sub(b.pt, a.pt));
    return HMM_Mul(translate, HMM_QToM4(q));
}

// puts it in the range of -180 to +180, in rads
float geo_normalizeAngle(float a) {
    while (a > HMM_AngleDeg(180)) {
        a -= HMM_AngleDeg(360);
    }
    while (a < HMM_AngleDeg(-180)) {
        a += HMM_AngleDeg(360);
    }
    return a;
}

HMM_Vec3 geo_triNormal(geo_Tri t) {
    HMM_Vec3 n = HMM_Cross(HMM_SubV3(t.b, t.a), HMM_SubV3(t.c, t.a));  // isn't this backwards?
    return HMM_NormV3(n);
}

// returns a T value along the line such that ((t*rayDir) + rayOrigin) = the point of intersection
// done this way so that bounds checking can be done after the return
// false retur nvalue indicates no intersection between the plane and line
// t may be negative
// outT assumed non-null, written for output
bool geo_rayPlaneIntersection(HMM_Vec3 planeOrigin, HMM_Vec3 planeNormal, HMM_Vec3 rayOrigin, HMM_Vec3 rayDir, float* outT) {
    float t = HMM_Dot(HMM_SubV3(planeOrigin, rayOrigin), planeNormal);
    t /= HMM_DotV3(rayDir, planeNormal);
    *outT = t;
    if (!isfinite(t)) {
        t = 0;
        return false;
    }
    return true;
}

// https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
bool geo_rayTriIntersection(HMM_Vec3 rayOrigin, HMM_Vec3 rayDir, geo_Tri tri, HMM_Vec3* outPos) {
    *outPos = HMM_V3(0, 0, 0);

    HMM_Vec3 edge1 = HMM_Sub(tri.b, tri.a);
    HMM_Vec3 edge2 = HMM_Sub(tri.c, tri.a);
    HMM_Vec3 ray_cross_e2 = HMM_Cross(rayDir, edge2);
    float det = HMM_Dot(edge1, ray_cross_e2);

    if (geo_floatZero(det)) {
        return false;
    }

    float inv_det = 1.0 / det;
    HMM_Vec3 s = HMM_Sub(rayOrigin, tri.a);
    float u = inv_det * HMM_Dot(s, ray_cross_e2);

    if ((u < 0 && fabsf(u) > geo_EPSILON) || (u > 1 && fabsf(u - 1) > geo_EPSILON)) {
        return false;
    }

    HMM_Vec3 s_cross_e1 = HMM_Cross(s, edge1);
    float v = inv_det * HMM_Dot(rayDir, s_cross_e1);

    if ((v < 0 && fabsf(v) > geo_EPSILON) || (u + v > 1 && fabsf(u + v - 1) > geo_EPSILON)) {
        return false;
    }

    // At this stage we can compute t to find out where the intersection point is on the line.
    float t = inv_det * HMM_Dot(edge2, s_cross_e1);

    // ray intersection
    if (t > geo_EPSILON) {
        *outPos = HMM_Mul(rayDir, t);
        *outPos = HMM_Add(rayOrigin, *outPos);
        return true;
    } else {
        // This means that there is a line intersection but not a ray intersection.
        return false;
    }
}

// thank u internet: https://math.stackexchange.com/questions/846054/closest-points-on-two-line-segments
// FIXME: probably smarter to do the collision detection in screen space instead
HMM_Vec3 geo_rayClosestPointOnSegment(HMM_Vec3 rayStart, HMM_Vec3 rayDir, HMM_Vec3 l2a, HMM_Vec3 l2b, float* outDistFromLine) {
    SNZ_ASSERTF(!geo_v3Equal(l2a, l2b), "Segment w/ zero length: A: %f,%f,%f", l2a.X, l2a.Y, l2a.Z);

    HMM_Vec3 _21 = HMM_Sub(rayDir, rayStart);
    HMM_Vec3 _43 = HMM_Sub(l2b, l2a);
    HMM_Vec3 _31 = HMM_Sub(l2a, rayStart);

    float r1 = HMM_LenSqr(_21);
    float r2 = HMM_LenSqr(_43);

    float d4321 = HMM_Dot(_21, _43);
    float d3121 = HMM_Dot(_31, _21);
    float d4331 = HMM_Dot(_43, _31);

    float denominator = powf(d4321, 2.0f) - (r1 * r2);
    float s = (d4321 * d4331 - r2 * d3121) / denominator;
    float t = (r1 * d4331 - d4321 * d3121) / denominator;

    // FIXME: handle case with inf denom correctly

    t = SNZ_MIN(SNZ_MAX(0, t), 1);
    s = SNZ_MAX(0, s);

    HMM_Vec3 p1 = HMM_Add(rayStart, HMM_MulV3F(_21, s)); // is norming here correct?
    HMM_Vec3 p2 = HMM_Add(l2a, HMM_MulV3F(_43, t)); // is norming here correct?

    float x = HMM_Len(HMM_Sub(p2, p1));
    *outDistFromLine = x;
    return p2;
}

float geo_rayPointDistance(HMM_Vec3 rayStart, HMM_Vec3 rayDir, HMM_Vec3 pt) {
    HMM_Vec3 diff = HMM_Sub(pt, rayStart);
    HMM_Vec3 dir = HMM_Cross(diff, rayDir);
    dir = HMM_Cross(rayDir, dir);
    return HMM_Dot(diff, dir);
}
