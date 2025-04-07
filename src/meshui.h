#include "snooze.h"
#include "mesh.h"

static ui_SelectionStatus* _mesh_meshGenSelStatuses(mesh_Mesh* mesh, mesh_Face* hoveredFace, mesh_Edge* hoveredEdge, mesh_Corner* hoveredCorner, snz_Arena* scratch) {
    ui_SelectionStatus* firstStatus = NULL;
    for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
        ui_SelectionStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectionStatus);
        *status = (ui_SelectionStatus){
            .withinDragZone = false,

            .hovered = f == hoveredFace,
            .next = firstStatus,
            .state = &f->sel,
        };
        firstStatus = status;
    }

    for (mesh_Edge* e = mesh->firstEdge; e; e = e->next) {
        ui_SelectionStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectionStatus);
        *status = (ui_SelectionStatus){
            .withinDragZone = false,

            .hovered = e == hoveredEdge,
            .next = firstStatus,
            .state = &e->sel,
        };
        firstStatus = status;
    }

    for (int i = 0; i < mesh->corners.count; i++) {
        mesh_Corner* c = &mesh->corners.elems[i];
        ui_SelectionStatus* status = SNZ_ARENA_PUSH(scratch, ui_SelectionStatus);
        *status = (ui_SelectionStatus){
            .withinDragZone = false,

            .hovered = c == hoveredCorner,
            .next = firstStatus,
            .state = &c->sel,
        };
        firstStatus = status;
    }
    return firstStatus;
}

// FIXME: geo_ui.h for consistency
void mesh_meshBuild(mesh_Mesh* mesh, HMM_Mat4 vp, HMM_Vec3 cameraPos, HMM_Vec3 mouseDir, const snzu_Interaction* inter, HMM_Vec2 panelSize, snz_Arena* scratch) {
    SNZ_ASSERT(cameraPos.X || !cameraPos.X, "AHH");
    SNZ_ASSERT(mouseDir.X || !mouseDir.X, "AHH");

    mesh_Face* hoveredFace = NULL;
    mesh_Edge* hoveredEdge = NULL;
    mesh_Corner* hoveredCorner = NULL;
    { // finding hovered elts.
        float minDistSquared = INFINITY;
        mesh_Face* minFace = NULL;
        for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
            for (int i = 0; i < f->tris.count; i++) {
                geo_Tri t = f->tris.elems[i];
                HMM_Vec3 pos = HMM_V3(0, 0, 0);
                // FIXME: bounding box opt. to cull tri checks
                if (geo_rayTriIntersection(cameraPos, mouseDir, t, &pos)) {
                    float distSquared = HMM_LenSqr(HMM_Sub(pos, cameraPos));
                    if (distSquared < minDistSquared) {
                        minDistSquared = distSquared;
                        minFace = f;
                        break;
                    }
                }
            }
        } // end face loop

        float clipDist = minDistSquared;
        if (!isinf(minDistSquared)) {
            clipDist = sqrtf(clipDist);
        }

        mesh_Edge* minEdge = NULL;
        for (mesh_Edge* e = mesh->firstEdge; e; e = e->next) {
            for (int i = 0; i < e->points.count - 1; i++) {
                geo_Line l = (geo_Line){
                    .a = e->points.elems[i],
                    .b = e->points.elems[i + 1],
                };
                float distFromRay = 0;
                HMM_Vec3 pos = geo_rayClosestPointOnSegment(cameraPos, HMM_Add(cameraPos, mouseDir), l.a, l.b, &distFromRay);
                float distFromCamera = HMM_Len(HMM_Sub(pos, cameraPos));

                float size = 0.01 * distFromCamera;
                if (distFromRay > size) {
                    continue;
                } else if (distFromCamera > clipDist + size) {
                    continue;
                }
                clipDist = distFromCamera;
                minFace = NULL;
                minEdge = e;
            }
        }

        mesh_Corner* minCorner = NULL;
        for (int i = 0; i < mesh->corners.count; i++) {
            mesh_Corner* c = &mesh->corners.elems[i];
            float dist = geo_rayPointDistance(cameraPos, mouseDir, c->pos);
            float distFromCamera = HMM_Len(HMM_Sub(c->pos, cameraPos));
            float size = 0.002 * distFromCamera;
            if (dist > size) {
                continue;
            } else if (distFromCamera > clipDist + size) {
                continue;
            }
            clipDist = dist;
            minEdge = NULL;
            minFace = NULL;
            minCorner = c;
        }

        hoveredCorner = minCorner;
        hoveredEdge = minEdge;
        hoveredFace = minFace;
    } // end hover checks

    { // sel region logic
        ui_SelectionRegion* const region = SNZU_USE_MEM(ui_SelectionRegion, "region");
        ui_SelectionStatus* firstStatus = _mesh_meshGenSelStatuses(mesh, hoveredFace, hoveredEdge, hoveredCorner, scratch);
        ui_selectionRegionUpdate(region, firstStatus, inter->mouseActions[SNZU_MB_LEFT], inter->mousePosLocal, inter->keyMods & KMOD_SHIFT, true, false);
        ui_selectionRegionAnimate(region, firstStatus);
    }

    ren3d_VertSlice faceMeshVerts = { 0 };
    {
        SNZ_ARENA_ARR_BEGIN(scratch, ren3d_Vert);
        for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
            float sumAnim = f->sel.hoverAnim + f->sel.selectionAnim;
            if (!geo_floatZero(sumAnim)) {
                HMM_Vec4 targetColor = ui_colorAccent;
                targetColor.A = 0.8;
                HMM_Vec4 color = HMM_Lerp(ui_colorTransparentPanel, f->sel.selectionAnim, targetColor);
                color.A = HMM_Lerp(0.0f, SNZ_MIN(sumAnim, 1), color.A);
                for (int i = 0; i < f->tris.count; i++) {
                    geo_Tri t = f->tris.elems[i];
                    HMM_Vec3 normal = geo_triNormal(t);
                    for (int j = 0; j < 3; j++) {
                        float scaleFactor = HMM_Len(HMM_Sub(cameraPos, t.elems[j])) * f->sel.hoverAnim * 0.02f;
                        HMM_Vec3 pos = HMM_Add(t.elems[j], HMM_MulV3F(normal, scaleFactor));
                        ren3d_Vert* v = SNZ_ARENA_PUSH(scratch, ren3d_Vert);
                        *v = (ren3d_Vert){
                            .normal = normal,
                            .pos = pos,
                            .color = color,
                        };
                    } // end tri pt loop
                } // end tri loop
            }
        } // end face loop
        faceMeshVerts = SNZ_ARENA_ARR_END(scratch, ren3d_Vert);

        // text to indicate index of each face
        // int i = -1;
        // for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
        //     i++;
        //     geo_Align uiAlign = (geo_Align){
        //         .pt = HMM_V3(0, 0, 0),
        //         .normal = HMM_V3(0, 0, 1),
        //         .vertical = HMM_V3(0, 1, 0),
        //     };
        //     geo_Tri t = f->tris.elems[0];
        //     geo_Align faceAlign = (geo_Align){
        //         .pt = t.a,
        //         .normal = geo_triNormal(t),
        //         .vertical = HMM_Sub(t.b, t.a),
        //     };
        //     HMM_Mat4 textVP = geo_alignToM4(uiAlign, faceAlign);
        //     textVP = HMM_Mul(vp, textVP);
        //     const char* str = snz_arenaFormatStr(scratch, "face %d", i);
        //     HMM_Vec4 color = HMM_Lerp(ui_colorText, f->sel.selectionAnim, ui_colorAccent);
        //     snzr_drawTextScaled(
        //         HMM_V2(0, 0),
        //         HMM_V2(-INFINITY, -INFINITY),
        //         HMM_V2(INFINITY, INFINITY),
        //         color,
        //         str,
        //         strlen(str),
        //         ui_labelFont,
        //         textVP,
        //         0.1,
        //         false);
        // }
    }

    { // render
        ren3d_drawMesh(
            &mesh->renderMesh,
            vp, HMM_M4D(1.0f),
            HMM_V4(1, 1, 1, 1), HMM_V3(-1, -1, -1), ui_lightAmbient);

        if (faceMeshVerts.count && faceMeshVerts.elems) {
            HMM_Mat4 model = HMM_Translate(HMM_V3(0, 0, 0));
            glDisable(GL_DEPTH_TEST);
            ren3d_Mesh renderMesh = ren3d_meshInit(faceMeshVerts.elems, faceMeshVerts.count);
            // FIXME: lighting shouldn't affect this
            ren3d_drawMesh(&renderMesh, vp, model, HMM_V4(1, 1, 1, 1), HMM_V3(-1, -1, -1), 1);
            ren3d_meshDeinit(&renderMesh); // FIXME: do a buffer data instead??
            glEnable(GL_DEPTH_TEST);
        }

        for (mesh_Edge* edge = mesh->firstEdge; edge; edge = edge->next) {
            // FIXME: ew gross
            if (edge->sel.selected) {
                glDisable(GL_DEPTH_TEST);
            }
            for (int i = 0; i < edge->points.count - 1; i++) {
                HMM_Vec3 a = edge->points.elems[i];
                HMM_Vec3 b = edge->points.elems[i + 1];
                HMM_Vec4 pts[2] = {
                    HMM_V4(a.X, a.Y, a.Z, 1),
                    HMM_V4(b.X, b.Y, b.Z, 1),
                };

                for (int i = 0; i < 2; i++) {
                    float scaleFactor = HMM_Len(HMM_Sub(cameraPos, pts[i].XYZ)) * 0.01;
                    HMM_Vec3 offset = HMM_Mul(HMM_Norm(HMM_Sub(cameraPos, pts[i].XYZ)), scaleFactor);
                    pts[i].XYZ = HMM_Add(pts[i].XYZ, offset);
                }

                HMM_Vec4 color = HMM_Lerp(ui_colorText, edge->sel.selectionAnim, ui_colorAccent);
                float thickness = HMM_Lerp(ui_lineThickness, edge->sel.hoverAnim, ui_lineHoveredThickness);
                snzr_drawLine(pts, 2, color, thickness, vp);
            }
            // FIXME: ew gross
            if (edge->sel.selected) {
                glEnable(GL_DEPTH_TEST);
            }
        }

        for (int i = 0; i < mesh->corners.count; i++) {
            mesh_Corner* corner = &mesh->corners.elems[i];
            // FIXME: ew gross
            if (corner->sel.selected) {
                glDisable(GL_DEPTH_TEST);
            }
            float size = HMM_Lerp(ui_cornerHalfSize, corner->sel.hoverAnim + corner->sel.selectionAnim, ui_cornerHoveredHalfSize);
            HMM_Vec4 col = HMM_Lerp(ui_colorText, corner->sel.selectionAnim, ui_colorAccent);
            ren3d_drawBillboard(vp, panelSize, *ui_cornerTexture, col, corner->pos, HMM_V2(size, size));
            if (corner->sel.selected) {
                glEnable(GL_DEPTH_TEST);
            }
        }

        // really dirty wireframe for tris in each face
        // glDisable(GL_DEPTH_TEST);
        // for (mesh_Face* f = mesh->firstFace; f; f = f->next) {
        //     for (int64_t i = 0; i < f->tris.count; i++) {
        //         geo_Tri t = f->tris.elems[i];
        //         HMM_Vec4 pts[4] = { 0 };
        //         pts[0].XYZ = t.a;
        //         pts[1].XYZ = t.b;
        //         pts[2].XYZ = t.c;
        //         pts[3].XYZ = t.a;
        //         snzr_drawLine(pts, 4, HMM_V4(0.5, 0.5, 0.5, 0.5), 2, vp);
        //     }
        // }
        // glEnable(GL_DEPTH_TEST);
    } // end render
}
