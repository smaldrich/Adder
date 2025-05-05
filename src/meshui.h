#include "snooze.h"
#include "mesh.h"

static ui_SelectionStatus* _meshu_sceneGenSelStatuses(const mesh_Scene* scene, mesh_SceneGeo* hoveredGeo, snz_Arena* arena) {
    ui_SelectionStatus* firstStatus = NULL;
    for (int64_t i = 0; i < scene->allGeo.count; i++) {
        mesh_SceneGeo* geo = scene->allGeo.elems[i];
        ui_SelectionStatus* status = SNZ_ARENA_PUSH(arena, ui_SelectionStatus);
        *status = (ui_SelectionStatus){
            .withinDragZone = false,

            .hovered = geo == hoveredGeo,
            .next = firstStatus,
            .state = &geo->sel,
        };
        firstStatus = status;
    }
    return firstStatus;
}

// filter may be more than one geo kind ored together
void meshu_sceneBuild(mesh_GeoKind filter, const mesh_Scene* scene, HMM_Mat4 vp, HMM_Vec3 cameraPos, HMM_Vec3 mouseDir, const snzu_Interaction* inter, HMM_Vec2 panelSize, snz_Arena* scratch) {
    SNZ_ASSERT(cameraPos.X || !cameraPos.X, "AHH");
    SNZ_ASSERT(mouseDir.X || !mouseDir.X, "AHH");

    mesh_SceneGeo* hoveredGeo = NULL;
    { // finding hovered elts.
        float minDistSquared = INFINITY;
        for (int64_t faceIdx = 0; faceIdx < scene->faces.count; faceIdx++) {
            mesh_SceneGeo* f = &scene->faces.elems[faceIdx];
            for (int64_t triIdx = 0; triIdx < f->faceTris.count; triIdx++) {
                geo_Tri t = f->faceTris.elems[triIdx];
                HMM_Vec3 pos = HMM_V3(0, 0, 0);
                // FIXME: bounding box opt. to cull tri checks
                if (geo_rayTriIntersection(cameraPos, mouseDir, t, &pos)) {
                    float distSquared = HMM_LenSqr(HMM_Sub(pos, cameraPos));
                    if (distSquared < minDistSquared) {
                        minDistSquared = distSquared;
                        // only register the actual geometry if the filter allows
                        if (filter & MESH_GK_FACE) {
                            hoveredGeo = f;
                        }
                        break;
                    }
                }
            }
        } // end face loop

        float clipDist = minDistSquared;
        if (!isinf(minDistSquared)) {
            clipDist = sqrtf(clipDist);
        }

        if (filter & MESH_GK_EDGE) {
            for (int64_t edgeIdx = 0; edgeIdx < scene->edges.count; edgeIdx++) {
                mesh_SceneGeo* e = &scene->edges.elems[edgeIdx];
                for (int64_t pointIdx = 0; pointIdx < e->edgePoints.count - 1; pointIdx++) {
                    geo_Line l = (geo_Line){
                        .a = e->edgePoints.elems[pointIdx],
                        .b = e->edgePoints.elems[pointIdx + 1],
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
                    hoveredGeo = e;
                }
            }
        }

        if (filter & MESH_GK_CORNER) {
            for (int64_t i = 0; i < scene->corners.count; i++) {
                mesh_SceneGeo* c = &scene->corners.elems[i];
                float dist = geo_rayPointDistance(cameraPos, mouseDir, c->cornerPos);
                float distFromCamera = HMM_Len(HMM_Sub(c->cornerPos, cameraPos));
                float size = 0.002 * distFromCamera;
                if (dist > size) {
                    continue;
                } else if (distFromCamera > clipDist + size) {
                    continue;
                }
                clipDist = dist;
                hoveredGeo = c;
            }
        }
    } // end hover checks

    { // sel region logic
        ui_SelectionRegion* const region = SNZU_USE_MEM(ui_SelectionRegion, "region");
        ui_SelectionStatus* firstStatus = _meshu_sceneGenSelStatuses(scene, hoveredGeo, scratch);
        ui_selectionRegionUpdate(region, firstStatus, inter->mouseActions[SNZU_MB_LEFT], inter->mousePosLocal, inter->keyMods & KMOD_SHIFT, true, false);
        ui_selectionRegionAnimate(region, firstStatus);
    }

    ren3d_VertSlice faceMeshVerts = { 0 };
    {
        SNZ_ARENA_ARR_BEGIN(scratch, ren3d_Vert);
        for (int64_t faceIdx = 0; faceIdx < scene->faces.count; faceIdx++) {
            mesh_SceneGeo* f = &scene->faces.elems[faceIdx];
            float sumAnim = f->sel.hoverAnim + f->sel.selectionAnim;
            if (!geo_floatZero(sumAnim)) {
                HMM_Vec4 targetColor = ui_colorAccent;
                targetColor.A = 0.8;
                HMM_Vec4 color = HMM_Lerp(ui_colorTransparentPanel, f->sel.selectionAnim, targetColor);
                color.A = HMM_Lerp(0.0f, SNZ_MIN(sumAnim, 1), color.A);
                for (int64_t triIdx = 0; triIdx < f->faceTris.count; triIdx++) {
                    geo_Tri t = f->faceTris.elems[triIdx];
                    HMM_Vec3 normal = geo_triNormal(t);
                    for (int ptIdx = 0; ptIdx < 3; ptIdx++) {
                        float scaleFactor = HMM_Len(HMM_Sub(cameraPos, t.elems[ptIdx])) * f->sel.hoverAnim * 0.02f;
                        HMM_Vec3 pos = HMM_Add(t.elems[ptIdx], HMM_MulV3F(normal, scaleFactor));
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
    }

    { // render
        ren3d_drawMesh(
            &scene->renderMesh,
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

        for (int64_t edgeIndex = 0; edgeIndex < scene->edges.count; edgeIndex++) {
            mesh_SceneGeo* edge = &scene->edges.elems[edgeIndex];
            // FIXME: ew gross
            if (edge->sel.selected) {
                glDisable(GL_DEPTH_TEST);
            }
            for (int64_t segmentIndex = 0; segmentIndex < edge->edgePoints.count - 1; segmentIndex++) {
                HMM_Vec3 a = edge->edgePoints.elems[segmentIndex];
                HMM_Vec3 b = edge->edgePoints.elems[segmentIndex + 1];
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

        for (int64_t i = 0; i < scene->corners.count; i++) {
            mesh_SceneGeo* corner = &scene->corners.elems[i];
            // FIXME: ew gross
            if (corner->sel.selected) {
                glDisable(GL_DEPTH_TEST);
            }
            float size = HMM_Lerp(ui_cornerHalfSize, corner->sel.hoverAnim + corner->sel.selectionAnim, ui_cornerHoveredHalfSize);
            HMM_Vec4 col = HMM_Lerp(ui_colorText, corner->sel.selectionAnim, ui_colorAccent);
            ren3d_drawBillboard(vp, panelSize, *ui_cornerTexture, col, corner->cornerPos, HMM_V2(size, size));
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
