#pragma once

#include "csg.h"
#include "render3d.h"
#include "snooze.h"
#include "ui.h"

// https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
bool geo_intersectRayAndTri(HMM_Vec3 rayOrigin, HMM_Vec3 rayDir,
                            HMM_Vec3 pts[3], HMM_Vec3* outPos) {
    *outPos = HMM_V3(0, 0, 0);

    HMM_Vec3 edge1 = HMM_Sub(pts[1], pts[0]);
    HMM_Vec3 edge2 = HMM_Sub(pts[2], pts[0]);
    HMM_Vec3 ray_cross_e2 = HMM_Cross(rayDir, edge2);
    float det = HMM_Dot(edge1, ray_cross_e2);

    if (csg_floatZero(det)) {
        return false;
    }

    float inv_det = 1.0 / det;
    HMM_Vec3 s = HMM_Sub(rayOrigin, pts[0]);
    float u = inv_det * HMM_Dot(s, ray_cross_e2);

    if ((u < 0 && fabsf(u) > CSG_EPSILON) || (u > 1 && fabsf(u - 1) > CSG_EPSILON)) {
        return false;
    }

    HMM_Vec3 s_cross_e1 = HMM_Cross(s, edge1);
    float v = inv_det * HMM_Dot(rayDir, s_cross_e1);

    if ((v < 0 && fabsf(v) > CSG_EPSILON) || (u + v > 1 && fabsf(u + v - 1) > CSG_EPSILON)) {
        return false;
    }

    // At this stage we can compute t to find out where the intersection point is on the line.
    float t = inv_det * HMM_Dot(edge2, s_cross_e1);

    // ray intersection
    if (t > CSG_EPSILON) {
        *outPos = HMM_Mul(rayDir, t);
        *outPos = HMM_Add(rayOrigin, *outPos);
        return true;
    } else {
        // This means that there is a line intersection but not a ray intersection.
        return false;
    }
}

typedef struct geo_MeshFace geo_MeshFace;
struct geo_MeshFace {
    csg_TriListNode* tri;  // FIXME: this should be >1 but thats a project.
    geo_MeshFace* next;
};

typedef struct {
    ren3d_Mesh renderMesh;
    csg_TriList triList;
    csg_BSPNode* bspTree;
    geo_MeshFace* firstFace;
} geo_Mesh;

ren3d_Mesh _geo_selectedMesh;

void geo_buildSelectedMesh(geo_Mesh* mesh, HMM_Mat4 vp, HMM_Vec3 cameraPos, HMM_Vec3 mouseRayDir) {
    geo_MeshFace* closestHovered = NULL;
    float closestHoveredDist = INFINITY;

    assert(cameraPos.X || !cameraPos.X);
    assert(mouseRayDir.X || !mouseRayDir.X);

    for (geo_MeshFace* face = mesh->firstFace; face; face = face->next) {
        HMM_Vec3 intersection = HMM_V3(0, 0, 0);
        // NOTE: any model transform will have to change this to adapt
        bool hovered = geo_intersectRayAndTri(cameraPos, mouseRayDir, face->tri->elems, &intersection);
        if (hovered) {
            float dist = HMM_LenSqr(HMM_Sub(intersection, cameraPos));
            if (dist < closestHoveredDist) {
                closestHoveredDist = dist;
                closestHovered = face;
            }
        }  // end hover check
    }  // end face loop

    if (!closestHovered) {
        return;
    }

    float* const selectionAnim = SNZU_USE_MEM(float, "geo selection anim");
    geo_MeshFace** const prevFace = SNZU_USE_MEM(geo_MeshFace*, "geo prevFace");
    if (snzu_useMemIsPrevNew() || (*prevFace) != closestHovered) {
        *selectionAnim = 0;
    }
    *prevFace = closestHovered;

    snzu_easeExp(selectionAnim, true, 15);

    ren3d_meshDeinit(&_geo_selectedMesh);
    csg_TriListNode* t = closestHovered->tri;
    HMM_Vec3 normal = csg_triNormal(t->a, t->b, t->c);
    float scaleFactor = HMM_SqrtF(closestHoveredDist);
    HMM_Vec3 offset = HMM_Mul(normal, scaleFactor * 0.03f * *selectionAnim);
    ren3d_Vert verts[] = {
        (ren3d_Vert){
            .pos = HMM_Add(t->a, offset),
            .normal = normal,
        },
        (ren3d_Vert){
            .pos = HMM_Add(t->b, offset),
            .normal = normal,
        },
        (ren3d_Vert){
            .pos = HMM_Add(t->c, offset),
            .normal = normal,
        },
    };
    _geo_selectedMesh = ren3d_meshInit(verts, 3);
    HMM_Mat4 model = HMM_Translate(HMM_V3(0, 0, 0));
    HMM_Vec4 color = ui_colorText;  // FIXME: not text colored
    color.A = 0.1;                  // FIXME: ui val for this alpha
    ren3d_drawMesh(&_geo_selectedMesh, vp, model, color, HMM_V3(-1, -1, -1), 1);
}

void geo_tests() {
    snz_testPrintSection("geo");
}

/*
so we are making a way of identifying geometry for shit

doing it LL of opp style is miserable in so many ways
    each operation needs a method of location a piece of geometry
    i.e. it's own struct/enum tag/values/handling code etc.
    each op also needs a way of storing what made it

a backup would be lovely -> 'closest fuckin position + normal?'


so sketches are 'primatives' (stored ptr style to identify components, edge+dir to ident faces)
the planned operations we have are:

union
difference
intersection
extrusion
revolve
pattern
shell
loft
fillet
chamfer


and the outputted geometry can be distinguished via:
and that gives you the exact fucking thing you need to find a source sketch by golly ive done it again

union:
    f1+f2->line
    or f->f
    or l->l
    or l+f->p
    or p->p
diff: same as union
intersection: same as union

extrude:
    f->f1
    f->f2
    l->f
    p->l
    p->p1
    p->p2
    l->l1
    l->l2

revolve:
    l->f
    l->l
    f->f1
    f->f2
    p->p1
    p->p2
    p->l

pattern:
    l|f|p -> (l|f|p)i,j,k,etc.

shell:
    l->f
    f->f
    f->f1
    f->f2
    p->p1
    p->p2
    l->l1
    l->l2

loft:
    f->f1
    f->f2
    l->f
    l->l1
    l->l2
    p->l
    p->p1
    p->p2

fillet:
    l->f
    l->l1
    l->l2
    p->f
    p->l
chamfer: same as fillet

and then the face for some geometry just stores the ID for which of these it is and your're golden
*/