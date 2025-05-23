#pragma once

#include "PoolAlloc.h"
#include "geometry.h"
#include "sketches2.h"
#include "snooze.h"
#include "ui.h"
#include "timeline.h"

typedef enum {
    SC_VIEW_NONE = 0,
    SC_VIEW_SCENE = 1 << 0,
    SC_VIEW_DOCS = 1 << 1,
    SC_VIEW_SETTINGS = 1 << 2,
    SC_VIEW_SHORTCUTS = 1 << 3,
    SC_VIEW_TIMELINE = 1 << 4,
    SC_VIEW_ALL = SC_VIEW_SCENE | SC_VIEW_DOCS | SC_VIEW_SETTINGS | SC_VIEW_SHORTCUTS | SC_VIEW_TIMELINE,
} sc_View;

typedef enum {
    SC_FLAGS_NONE,
    SC_FLAGS_REQUIRE_SCENE = (1 << 0),
    SC_FLAGS_REQUIRE_SKETCH = (1 << 1),
    SC_FLAGS_REQUIRE_TIMELINE = (1 << 2),
} _sc_Flags;

typedef struct {
    SDL_KeyCode key;
    SDL_Keymod mods;
} _sc_KeyPress;

typedef struct {
    snz_Arena* scratch;
    sk_Sketch* activeSketch;
    tl_Timeline* timeline;
    bool firstFrame;

    mesh_Scene* scene;

    sc_View* currentView;  // read/write
    mesh_GeoKind* outGeoFilter; // read/write
    tl_Op** argBarFocusOverride; // read/write
} _sc_CommandFuncArgs;

typedef bool (*sc_CommandFunc)(_sc_CommandFuncArgs args);

typedef struct {
    _sc_KeyPress key;
    sc_CommandFunc func;
    const char* nameLabel;
    const char* keyLabel;
    _sc_Flags flags;
} _sc_Command;

_sc_Command* _sc_commands = NULL;
int64_t _sc_commandCount = 0;
PoolAlloc* _sc_commandPool = NULL;
_sc_Command* _sc_activeCommand = NULL;

sc_CommandFunc sc_getActiveCommand() {
    if (_sc_activeCommand) {
        return _sc_activeCommand->func;
    }
    return NULL;
}

static _sc_Command* _sc_commandInit(const char* displayName, const char* keyName, SDL_KeyCode code, SDL_Keymod mod, _sc_Flags flags, sc_CommandFunc func) {
    _sc_Command* c = poolAllocPushArray(_sc_commandPool, _sc_commands, _sc_commandCount, _sc_Command);
    *c = (_sc_Command){
        .nameLabel = displayName,
        .keyLabel = keyName,
        .func = func,
        .key = (_sc_KeyPress){
            .key = code,
            .mods = mod,
        },
        .flags = flags,
    };
    return c;
}

bool _scc_goToSettings(_sc_CommandFuncArgs args) {
    *args.currentView = SC_VIEW_SETTINGS;
    return true;
}

bool _scc_goToDocs(_sc_CommandFuncArgs args) {
    *args.currentView = SC_VIEW_DOCS;
    return true;
}

bool _scc_goToShortcuts(_sc_CommandFuncArgs args) {
    *args.currentView = SC_VIEW_SHORTCUTS;
    return true;
}

bool _scc_goToMainScene(_sc_CommandFuncArgs args) {
    *args.currentView = SC_VIEW_SCENE;
    return true;
}

bool _scc_goToTimeline(_sc_CommandFuncArgs args) {
    *args.currentView = SC_VIEW_TIMELINE;
    return true;
}

bool _scc_sketchDelete(_sc_CommandFuncArgs args) {
    for (sk_Point* p = args.activeSketch->firstPoint; p; p = p->next) {
        if (p->sel.selected) {
            p->markedForDelete = true;
        }
    }
    for (sk_Line* l = args.activeSketch->firstLine; l; l = l->next) {
        if (l->sel.selected) {
            l->markedForDelete = true;
        }
    }
    for (sk_Constraint* c = args.activeSketch->firstConstraint; c; c = c->nextAllocated) {
        if (c->uiInfo.sel.selected) {
            c->markedForDelete = true;
        }
    }
    return true;
}

bool _scc_sketchAddDistanceConstraint(_sc_CommandFuncArgs args) {
    int selectedCount = 0;
    sk_Line* firstLine = NULL;

    for (sk_Line* line = args.activeSketch->firstLine; line; line = line->next) {
        if (line->sel.selected) {
            firstLine = line;
            selectedCount++;
        }
    }

    if (selectedCount != 1) {
        return true;
    }

    float currentLength = HMM_Len(HMM_Sub(firstLine->p2->pos, firstLine->p1->pos));
    sk_Constraint* c = sk_sketchAddConstraintDistance(args.activeSketch, firstLine, currentLength);
    c->uiInfo.shouldStartFocus = true;
    const char* str = sk_constraintLabelStr(c, args.scratch);
    ui_textAreaSetStr(&c->uiInfo.textArea, str, strlen(str));
    return true;
}

// FIXME: this and every sketch cmd triggers and crashes if there isn't an active sketch, fix that
bool _scc_sketchAddAngleConstraint(_sc_CommandFuncArgs args) {
    int selectedCount = 0;
    sk_Line* lines[2] = { NULL, NULL };
    for (sk_Line* line = args.activeSketch->firstLine; line; line = line->next) {
        if (line->sel.selected) {
            selectedCount++;
            if (selectedCount > 2) {
                return true;
            }
            lines[selectedCount - 1] = line;
        }
    }

    if (selectedCount != 2) {
        return true;
    }

    float minDist = INFINITY;
    bool isP1OnLine1 = true;
    bool isP1OnLine2 = true;
    for (int i = 0; i < 2; i++) {
        HMM_Vec2 a = lines[0]->pts[i]->pos;
        for (int j = 0; j < 2; j++) {
            HMM_Vec2 b = lines[1]->pts[j]->pos;
            float dist = HMM_Len(HMM_Sub(b, a));
            if (dist < minDist) {
                minDist = dist;
                isP1OnLine1 = i == 0;
                isP1OnLine2 = j == 0;
            }
        }
    }

    float angle1 = sk_angleOfLine(lines[0]->p1->pos, lines[0]->p2->pos, !isP1OnLine1);
    float angle2 = sk_angleOfLine(lines[1]->p1->pos, lines[1]->p2->pos, !isP1OnLine2);

    float diff = angle2 - angle1;
    while (diff > HMM_AngleDeg(180)) {
        diff -= HMM_AngleDeg(360);
    }
    while (diff < HMM_AngleDeg(-180)) {
        diff += HMM_AngleDeg(360);
    }

    sk_Constraint* c = NULL;
    if (diff < 0) {
        c = sk_sketchAddConstraintAngle(args.activeSketch, lines[1], !isP1OnLine2, lines[0], !isP1OnLine1, -diff);
    } else {
        c = sk_sketchAddConstraintAngle(args.activeSketch, lines[0], !isP1OnLine1, lines[1], !isP1OnLine2, diff);
    }
    c->uiInfo.shouldStartFocus = true;
    const char* str = sk_constraintLabelStr(c, args.scratch);
    ui_textAreaSetStr(&c->uiInfo.textArea, str, strlen(str));
    return true;
}

bool scc_sketchLineMode(_sc_CommandFuncArgs args) {
    if (args.firstFrame) {  // creating a line between two selected pts
        int ptCount = 0;
        sk_Point* pts[2] = { 0 };
        for (sk_Point* p = args.activeSketch->firstPoint; p; p = p->next) {
            if (p->sel.selected) {
                ptCount++;
                if (ptCount > 2) {
                    break;
                }
                pts[ptCount - 1] = p;
            }
        }
        if (ptCount == 2) {
            sk_sketchAddLine(args.activeSketch, pts[0], pts[1]);
            return true;
        }
    }

    // remainder of logic in sketchui.h
    return false;
}

// FIXME: rename this and move it to sketches2.h
bool _sc_anySelectedInSketch(sk_Sketch* sketch) {
    for (sk_Point* p = sketch->firstPoint; p; p = p->next) {
        if (p->sel.selected) {
            return true;
        }
    }

    for (sk_Line* l = sketch->firstLine; l; l = l->next) {
        if (l->sel.selected) {
            return true;
        }
    }

    for (sk_Constraint* c = sketch->firstConstraint; c; c = c->nextAllocated) {
        if (c->uiInfo.sel.selected) {
            return true;
        }
    }
    return false;
}

bool scc_sketchMove(_sc_CommandFuncArgs args) {
    if (args.firstFrame) {
        return !_sc_anySelectedInSketch(args.activeSketch);  // cancels if nothing selected
    }

    // bulk of logic in sketchui.h
    return false;
}

bool scc_sketchRotate(_sc_CommandFuncArgs args) {
    if (args.firstFrame) {
        return !_sc_anySelectedInSketch(args.activeSketch);  // cancels if nothing selected
    }

    // bulk of logic in sketchui.h
    return false;
}

// FIXME: do a deeper dive on use after frees when nodes are deleted? are there any retained refs?
bool scc_timelineDelete(_sc_CommandFuncArgs args) {
    for (tl_Op* op = args.timeline->firstOp; op; op = op->next) {
        if (op->ui.sel.selected) {
            op->markedForDeletion = true;
        }
    }
    return true;
}

bool scc_timelineMove(_sc_CommandFuncArgs args) {
    if (args.firstFrame) {
        return !tl_timelineAnySelected(args.timeline);
    }
    return false;
}

bool scc_timelineRotate(_sc_CommandFuncArgs args) {
    if (args.firstFrame) {
        return !tl_timelineAnySelected(args.timeline);
    }
    return false;
}

bool scc_timelineAddGeometry(_sc_CommandFuncArgs args) {
    *args.currentView = SC_VIEW_SCENE;
    tl_Op* newOp = tl_timelinePushBaseGeometry(args.timeline, HMM_V2(0, 0), (mesh_FaceSlice) { 0 });
    // FIXME: should be on the mouse, isn't // enter move mode?
    newOp->ui.sel.selected = true;
    newOp->ui.sel.selectionAnim = 1;
    args.timeline->activeOp = newOp;
    return true;
}

bool scc_timelineAddSketch(_sc_CommandFuncArgs args) {
    *args.currentView = SC_VIEW_SCENE;
    snz_Arena* arena = SNZ_ARENA_PUSH(args.timeline->operationArena, snz_Arena); // FIXME: freelist
    *arena = snz_arenaInit(1000000, "sketch arena");
    tl_Op* newOp = tl_timelinePushSketch(args.timeline, HMM_V2(0, 0), sk_sketchInit(arena));
    // FIXME: should be on the mouse, isn't // enter move mode?
    newOp->ui.sel.selected = true;
    newOp->ui.sel.selectionAnim = 1;
    args.timeline->activeOp = newOp;
    return true;
}

bool scc_timelineAddExtrude(_sc_CommandFuncArgs args) {
    tl_Op* op = tl_timelinePushExtrude(args.timeline, HMM_V2(0, 0)); // FIXME: smart positioning
    op->ui.sel.selected = true;
    op->ui.sel.selectionAnim = 1;
    *args.argBarFocusOverride = op;

    op->args[1].kind = TL_OPAK_NUMBER;
    op->args[1].number = 1.0f;
    return true;
}

bool scc_timelineMarkActive(_sc_CommandFuncArgs args) {
    tl_Op* selected = NULL;
    for (tl_Op* op = args.timeline->firstOp; op; op = op->next) {
        if (!op->ui.sel.selected) {
            continue;
        } else if (selected != NULL) {
            selected = NULL;
            break;
        }
        selected = op;
    }
    tl_timelineDeselectAll(args.timeline);

    if (selected == NULL) {
        return true;
    }

    args.timeline->activeOp = selected;
    *args.currentView = SC_VIEW_SCENE;
    *args.scene = tl_solveForNode(args.timeline, args.timeline->activeOp, args.scratch);
    return true;
}

void _scc_sceneDeselectAll(mesh_Scene* scene) {
    for (int64_t i = 0; i < scene->faces.count; i++) {
        mesh_SceneGeo* geo = &scene->faces.elems[i];
        geo->sel.selected = false;
    }

    for (int64_t i = 0; i < scene->edges.count; i++) {
        mesh_SceneGeo* geo = &scene->edges.elems[i];
        geo->sel.selected = false;
    }

    for (int64_t i = 0; i < scene->corners.count; i++) {
        mesh_SceneGeo* geo = &scene->corners.elems[i];
        geo->sel.selected = false;
    }
}

bool scc_sceneLookAt(_sc_CommandFuncArgs args) {
    tl_Op* op = args.timeline->activeOp;
    if (!op) {
        return true;
    }

    mesh_SceneGeo* selected = NULL;
    for (int64_t i = 0; i < args.scene->allGeo.count; i++) {
        mesh_SceneGeo* geo = args.scene->allGeo.elems[i];
        if (geo->sel.selected) {
            if (selected) {
                return true;
            }
            selected = geo;
        }
    }

    if (!selected) {
        return true;
    }

    geo_Align* origin = &args.scene->orbitOrigin;
    if (selected->id.geoKind == MESH_GK_FACE) {
        SNZ_ASSERT(selected->faceTris.count > 0, "face with no tris.");
        HMM_Vec3 newNorm = geo_triNormal(selected->faceTris.elems[0]); // FIXME: what about curved faces??
        SNZ_ASSERT(isfinite(newNorm.X), "invalid (likely zero area) tri.");

        // adjust so that vertical is 90 off the new normal
        HMM_Vec3 newVert = origin->vertical;
        newVert = HMM_Cross(newVert, origin->normal);
        newVert = HMM_Cross(newNorm, newVert);;
        if (!geo_v3Equal(newVert, HMM_V3(0, 0, 0))) {
            origin->vertical = HMM_Norm(newVert);
        }
        origin->normal = newNorm;
        args.scene->orbitAngle = HMM_V2(0, 0);

        // adjusting orbit center to be on the new plane
        float t = 0;
        HMM_Vec3 lineOrigin = HMM_Add(origin->pt, newNorm);
        bool hit = geo_rayPlaneIntersection(
            selected->faceTris.elems[0].a, newNorm,
            lineOrigin, newNorm, &t);
        SNZ_ASSERT(hit, "can't find a point to move orbit origin to.");
        origin->pt = HMM_Add(lineOrigin, HMM_Mul(newNorm, t));
    } else if (selected->id.geoKind == MESH_GK_CORNER) {
        args.scene->orbitOrigin.pt = selected->cornerPos;
    } else if (selected->id.geoKind == MESH_GK_EDGE) {
        // FIXME: what about curved edges??
        SNZ_ASSERTF(selected->edgePoints.count >= 2, "edge with only %lld point(s)", selected->edgePoints.count);
        HMM_Vec3 p1 = selected->edgePoints.elems[0];
        HMM_Vec3 p2 = selected->edgePoints.elems[1];
        HMM_Vec3 dir = HMM_Norm(HMM_Sub(p2, p1));
        dir = HMM_Norm(HMM_Cross(HMM_Cross(origin->normal, dir), origin->normal));

        HMM_Vec3 closest = dir;
        float closestDot = -INFINITY;
        for (int i = 0; i < 4; i++) {
            float dot = HMM_Dot(dir, origin->vertical);
            if (dot > closestDot) {
                closest = dir;
                closestDot = dot;
            }
            dir = HMM_Norm(HMM_Cross(dir, origin->normal)); // rotate 90deg right
        }

        // if the edge is perfectly away from the base normal, this could end up nan. Just don't apply if that's the case.
        if (!isnan(closest.X)) {
            origin->vertical = closest;
        }
        args.scene->orbitAngle = HMM_V2(0, 0);
    } else {
        SNZ_ASSERTF(false, "unreachable. kind: %d", selected->id.geoKind);
    }

    geo_alignAssertValid(origin);
    // _scc_sceneDeselectAll(&op->scene); // FIXME: build this in to cmd handling routine like for sketches and tl elts
    return true;
}

bool scc_sceneRotateCameraLeft(_sc_CommandFuncArgs args) {
    tl_Op* op = args.timeline->activeOp;
    if (!op) {
        return true;
    }
    geo_Align* origin = &args.scene->orbitOrigin;
    HMM_Vec3 newVertical = HMM_Norm(HMM_Cross(origin->normal, origin->vertical));
    origin->vertical = newVertical;
    geo_alignAssertValid(&args.scene->orbitOrigin);
    return true;
}

bool scc_sceneRotateCameraRight(_sc_CommandFuncArgs args) {
    tl_Op* op = args.timeline->activeOp;
    if (!op) {
        return true;
    }
    geo_Align* origin = &args.scene->orbitOrigin;
    HMM_Vec3 newVertical = HMM_Norm(HMM_Cross(origin->vertical, origin->normal));
    origin->vertical = newVertical;
    geo_alignAssertValid(&args.scene->orbitOrigin);
    return true;
}

bool scc_sceneTake(_sc_CommandFuncArgs args) {
    if (*args.currentView != SC_VIEW_SCENE) {
        return true;
    } else if (!args.timeline->activeOp) {
        return true;
    }

    int foundCount = 0;
    tl_OpArg outArg = { 0 };
    for (int64_t i = 0; i < args.scene->allGeo.count; i++) {
        mesh_SceneGeo* geo = args.scene->allGeo.elems[i];
        if (geo->sel.selected) {
            if (geo->id.geoKind == MESH_GK_CORNER) {
                outArg.kind = TL_OPAK_GEOID_CORNER;
            } else if (geo->id.geoKind == MESH_GK_EDGE) {
                outArg.kind = TL_OPAK_GEOID_EDGE;
            } else if (geo->id.geoKind == MESH_GK_FACE) {
                outArg.kind = TL_OPAK_GEOID_FACE;
            } else {
                SNZ_ASSERTF(false, "unreachable. kind: %d", geo->id.geoKind);
            }
            outArg.geoId = geo->id;
            foundCount++;
        }
    }

    // FIXME: multiple take
    if (foundCount == 1) {
        args.timeline->takenArgSignal = outArg;
    }
    return true;
}

bool scc_sceneSetFilterForCorners(_sc_CommandFuncArgs args) {
    *args.outGeoFilter = MESH_GK_CORNER;
    return true;
}

bool scc_sceneSetFilterForEdges(_sc_CommandFuncArgs args) {
    *args.outGeoFilter = MESH_GK_EDGE;
    return true;
}

bool scc_sceneSetFilterForFaces(_sc_CommandFuncArgs args) {
    *args.outGeoFilter = MESH_GK_FACE;
    return true;
}

void sc_init(PoolAlloc* pool) {
    _sc_commandPool = pool;

    _sc_commandInit("look at", "V", SDLK_v, KMOD_NONE, SC_FLAGS_REQUIRE_SCENE, scc_sceneLookAt);
    _sc_commandInit("rotate camera left", "Q", SDLK_q, KMOD_LSHIFT, SC_FLAGS_REQUIRE_SCENE, scc_sceneRotateCameraLeft);
    _sc_commandInit("rotate camera right", "E", SDLK_e, KMOD_LSHIFT, SC_FLAGS_REQUIRE_SCENE, scc_sceneRotateCameraRight);

    _sc_commandInit("delete", "X", SDLK_x, KMOD_NONE, SC_FLAGS_REQUIRE_SKETCH, _scc_sketchDelete);
    _sc_commandInit("line", "B", SDLK_b, KMOD_NONE, SC_FLAGS_REQUIRE_SKETCH, scc_sketchLineMode);
    _sc_commandInit("move", "G", SDLK_g, KMOD_NONE, SC_FLAGS_REQUIRE_SKETCH, scc_sketchMove);
    _sc_commandInit("rotate", "R", SDLK_r, KMOD_NONE, SC_FLAGS_REQUIRE_SKETCH, scc_sketchRotate);
    _sc_commandInit("distance", "D", SDLK_d, KMOD_NONE, SC_FLAGS_REQUIRE_SKETCH, _scc_sketchAddDistanceConstraint);
    _sc_commandInit("angle", "A", SDLK_a, KMOD_NONE, SC_FLAGS_REQUIRE_SKETCH, _scc_sketchAddAngleConstraint);

    _sc_commandInit("delete", "X", SDLK_x, KMOD_NONE, SC_FLAGS_REQUIRE_TIMELINE, scc_timelineDelete);
    _sc_commandInit("move", "G", SDLK_g, KMOD_NONE, SC_FLAGS_REQUIRE_TIMELINE, scc_timelineMove);
    _sc_commandInit("rotate", "R", SDLK_r, KMOD_NONE, SC_FLAGS_REQUIRE_TIMELINE, scc_timelineRotate);

    _sc_Flags tlSketchOrScene = SC_FLAGS_REQUIRE_TIMELINE | SC_FLAGS_REQUIRE_SKETCH | SC_FLAGS_REQUIRE_SCENE;
    // FIXME: rename this and add file dialogue and add stl parsing
    _sc_commandInit("new geomety", "I", SDLK_i, KMOD_NONE, tlSketchOrScene, scc_timelineAddGeometry);
    _sc_commandInit("new sketch", "S", SDLK_s, KMOD_NONE, tlSketchOrScene, scc_timelineAddSketch);
    _sc_commandInit("extrude", "E", SDLK_e, KMOD_NONE, tlSketchOrScene, scc_timelineAddExtrude);

    // FIXME: these shouldn't be availible if geo filter is turned off
    // but they should give a warning/err msg to let the user know that the filter is disabled
    _sc_commandInit("Filter for corners", "1", SDLK_1, KMOD_NONE, SC_FLAGS_REQUIRE_SCENE, scc_sceneSetFilterForCorners);
    _sc_commandInit("Filter for edges", "2", SDLK_2, KMOD_NONE, SC_FLAGS_REQUIRE_SCENE, scc_sceneSetFilterForEdges);
    _sc_commandInit("Filter for faces", "3", SDLK_3, KMOD_NONE, SC_FLAGS_REQUIRE_SCENE, scc_sceneSetFilterForFaces);

    _sc_commandInit("take", "T", SDLK_t, KMOD_NONE, SC_FLAGS_REQUIRE_SCENE, scc_sceneTake);
    _sc_commandInit("mark active", "W", SDLK_w, KMOD_NONE, SC_FLAGS_REQUIRE_TIMELINE, scc_timelineMarkActive);

    _sc_commandInit("goto main scene", "W", SDLK_w, KMOD_LSHIFT, SC_FLAGS_NONE, _scc_goToMainScene);
    _sc_commandInit("goto timeline", "T", SDLK_t, KMOD_LSHIFT, SC_FLAGS_NONE, _scc_goToTimeline);
    _sc_commandInit("goto settings", "S", SDLK_s, KMOD_LSHIFT, SC_FLAGS_NONE, _scc_goToSettings);
    _sc_commandInit("goto docs", "D", SDLK_d, KMOD_LSHIFT, SC_FLAGS_NONE, _scc_goToDocs);
    _sc_commandInit("goto shortcuts", "C", SDLK_c, KMOD_LSHIFT, SC_FLAGS_NONE, _scc_goToShortcuts);
}

// immediately sets the active cmd to null, so make sure you don't trample shit
// FIXME: this is bad but making a buffer system is a pain and kinda bloated for smth so simple
void sc_cancelActiveCommand() {
    _sc_activeCommand = NULL;
}

// aligns to the TL of parent, no padding, tagged with cmd label
static void _sc_buildCommandShortcutBox(_sc_Command* cmd, HMM_Vec4 textColor) {
    snzu_boxNew(cmd->nameLabel);
    snzu_boxFillParent();
    snzu_boxScope() {
        snzu_boxNew("icons container");
        snzu_boxFillParent();
        snzu_boxScope() {
            // FIXME: both :)
            if (cmd->key.mods & KMOD_LSHIFT) {
                snzu_boxNew("icon");
                float aspect = (float)ui_shiftTexture->width / (float)ui_shiftTexture->height;
                float height = ui_shortcutFont.ascent;
                snzu_boxSetStartFromParentKeepSizeRecurse(HMM_V2(0, height * 0.1));  // FIXME: this
                snzu_boxSetSizeFromStart(HMM_V2(aspect * height, height));
                snzu_boxSetTexture(*ui_shiftTexture);
                snzu_boxSetColor(textColor);
            }
        }
        snzu_boxSetSizeFitChildren();

        snzu_boxNew("char");
        snzu_boxFillParent();
        snzu_boxSetDisplayStr(&ui_shortcutFont, textColor, cmd->keyLabel);
        snzu_boxSetSizeFitText(0);
    }
    snzu_boxOrderChildrenInRowRecurse(1, SNZU_AX_X, SNZU_ALIGN_TOP);
    snzu_boxSetSizeFitChildren();
}

bool _sc_commandShouldBeAvailible(_sc_Command* cmd, sc_View view, bool activeSketch) {
    _sc_Flags flags = cmd->flags;
    if (flags == SC_FLAGS_NONE) {
        return true;
    }

    if (view == SC_VIEW_SCENE) {
        if (activeSketch) {
            if (flags & SC_FLAGS_REQUIRE_SKETCH) {
                return true;
            }
        } else {
            if (flags & SC_FLAGS_REQUIRE_SCENE) {
                return true;
            }
        }
    } else if (view == SC_VIEW_TIMELINE) {
        return (flags & SC_FLAGS_REQUIRE_TIMELINE) != 0;
    }
    return false;
}

// literally every arg here gets modified by sc functions
void sc_updateAndBuildHintWindow(
    sk_Sketch* activeSketch,
    tl_Timeline* timeline,
    sc_View* outCurrentView,
    mesh_GeoKind* outGeoFilter,
    mesh_Scene* scene,
    tl_Op** argBarFocusOverride,
    snz_Arena* scratch,
    bool targetOpen) {
    snzu_boxNew("updatesParent");
    snzu_boxFillParent();

    _sc_CommandFuncArgs args = (_sc_CommandFuncArgs){
        .scratch = scratch,
        .activeSketch = activeSketch,
        .timeline = timeline,
        .currentView = outCurrentView,
        .outGeoFilter = outGeoFilter,
        .firstFrame = false,
        .argBarFocusOverride = argBarFocusOverride,
        .scene = scene,
    };

    {
        snzu_Interaction* inter = SNZU_USE_MEM(snzu_Interaction, "inter");
        snzu_boxSetInteractionOutput(inter, SNZU_IF_NONE);  // FIXME: huh
        if (inter->keyAction == SNZU_ACT_DOWN) {
            _sc_KeyPress kp = (_sc_KeyPress){
                .key = inter->keyCode,
                .mods = inter->keyMods,
            };

            // FIXME: this activates even when typing in a scene textbox bc it's a different instance
            if (snzu_isNothingFocused()) {
                for (int i = 0; i < _sc_commandCount; i++) {
                    _sc_Command* c = &_sc_commands[i];
                    if (!_sc_commandShouldBeAvailible(c, *outCurrentView, activeSketch != NULL)) {
                        continue;
                    }
                    // FIXME: left shift only is required thats bad
                    if (kp.key == c->key.key && (kp.mods == c->key.mods)) {
                        _sc_activeCommand = c;
                        args.firstFrame = true;
                        break;
                    }
                }
            }
            // FIXME: some indication if a cmd failed or not

            if (kp.key == SDLK_ESCAPE || !snzu_isNothingFocused()) {
                _sc_activeCommand = NULL;
                if (args.activeSketch) {
                    sk_sketchDeselectAll(args.activeSketch);
                }
                tl_timelineDeselectAll(args.timeline);
            }

            if (kp.key == SDLK_ESCAPE && snzu_isNothingFocused()) {
                *args.argBarFocusOverride = NULL;
            }
        }
    }  // command handling

    // to animate cmds on use in the hint window
    // here to still work before a command is used if it's instant
    _sc_Command* commandJustUsed = NULL;
    {
        _sc_Command** const prevCmd = SNZU_USE_MEM(_sc_Command*, "prevcmd");
        if (_sc_activeCommand != *prevCmd) {
            commandJustUsed = _sc_activeCommand;
        }
        *prevCmd = _sc_activeCommand;
    }

    snzu_boxScope() {
        if (_sc_activeCommand != NULL) {
            bool invalidated = !_sc_commandShouldBeAvailible(_sc_activeCommand, *outCurrentView, activeSketch != NULL);
            bool done = _sc_activeCommand->func(args);
            if (invalidated || done) {
                _sc_activeCommand = NULL;

                if (args.activeSketch) {
                    sk_sketchDeselectAll(args.activeSketch);
                }
                tl_timelineDeselectAll(args.timeline);
            }
        }

        snzu_boxNew("shortcutWindow");
        snzu_boxSetColor(ui_colorTransparentPanel);

        bool buildInners = true;
        snzu_Interaction* hoverInter = SNZU_USE_MEM(snzu_Interaction, "hintwindowinter");
        {
            float* const openAnim = SNZU_USE_MEM(float, "openanim");
            ;
            if (hoverInter->hovered) {
                targetOpen = true;
            }
            snzu_easeExp(openAnim, targetOpen, ui_menuAnimationSpeed);

            if (*openAnim < geo_EPSILON) {
                buildInners = false;
            }

            snzu_boxFillParent();
            snzu_boxSetSizeFromEndAx(SNZU_AX_X, *openAnim * 375);
        }

        if (buildInners) {
            snzu_boxScope() {
                snzu_boxNew("margin");
                snzu_boxSetSizeMarginFromParent(20);
                snzu_boxSetSizeMarginFromParentAx(23, SNZU_AX_X);
                snzu_boxScope() {
                    for (int i = 0; i < _sc_commandCount; i++) {
                        _sc_Command* c = &_sc_commands[i];
                        if (!_sc_commandShouldBeAvailible(c, *outCurrentView, activeSketch != NULL)) {
                            continue;
                        }

                        snzu_boxNew(c->nameLabel);
                        float* const useAnim = SNZU_USE_MEM(float, "useanim");
                        snzu_easeExp(useAnim, 0, 5);
                        if (commandJustUsed == c) {
                            *useAnim = 1;
                        }

                        snzu_boxFillParent();
                        snzu_boxSetSizeFromStartAx(SNZU_AX_Y, ui_lightLabelFont.renderedSize);
                        snzu_boxScope() {
                            HMM_Vec4 color = HMM_Lerp(ui_colorText, *useAnim, ui_colorAccent);

                            snzu_boxNew("desc");
                            snzu_boxSetDisplayStr(&ui_lightLabelFont, color, c->nameLabel);
                            snzu_boxSetSizeFitText(1);
                            snzu_boxAlignInParent(SNZU_AX_X, SNZU_ALIGN_RIGHT);
                            snzu_boxAlignInParent(SNZU_AX_Y, SNZU_ALIGN_CENTER);

                            _sc_buildCommandShortcutBox(c, color);
                        }
                    }  // end cmd loop
                }  // end margin box
                snzu_boxOrderChildrenInRowRecurse(4, SNZU_AX_Y, SNZU_ALIGN_LEFT);
                snzuc_scrollArea();
                snzu_boxClipChildren(false);

                snzu_boxNew("left border");
                snzu_boxFillParent();
                snzu_boxSetSizeFromStartAx(SNZU_AX_X, ui_borderThickness);
                snzu_boxSetColor(ui_colorText);

                snzu_boxNew("hover detector gross");  // FIXME: ew
                snzu_boxFillParent();
                snzu_boxSetInteractionOutput(hoverInter, SNZU_IF_HOVER | SNZU_IF_ALLOW_EVENT_FALLTHROUGH);
            }  // end hints window
            snzu_boxClipChildren(true);
        }

        float width = snzu_boxGetSize().X;
        if (width < 10) {
            ui_hiddenPanelIndicator(snzu_boxGetEnd().X, false, "panelIndicator");
        }
    }  // end entire window parent
}

void sc_buildSettings() {
    ui_menuMargin();
    snzu_boxScope() {
        snzu_boxNew("title");
        snzu_boxSetDisplayStr(&ui_titleFont, ui_colorText, "Shortcuts");
        snzu_boxSetSizeFitText(ui_padding);
    }
    snzu_boxOrderChildrenInRowRecurse(10, SNZU_AX_Y, SNZU_ALIGN_LEFT);
    snzuc_scrollArea();
}