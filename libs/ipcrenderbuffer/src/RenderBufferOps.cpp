/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

#include <SkColorFilter.h>

#include "SkFontScanner_FreeType.h"

#include <android/ipcrenderbuffer/RenderBufferOps.h>
#include <android/ipcrenderbuffer/RenderBufferDebugUtils.h>

#define DUMP_OPS 0
// #define DUMP_OPS 1

namespace android {

ShmemImageInfo toShmemImageInfo(const SkImageInfo& info) {
    return ShmemImageInfo{info.width(), info.height(), info.colorType(), info.alphaType()};
}

SkImageInfo fromShmemImageInfo(const ShmemImageInfo& info) {
    return SkImageInfo::Make(info.width, info.height, info.colorType, info.alphaType);
}

void renderOpToCanvas(const std::shared_ptr<RenderCommandBufferConsumer>& consumer,
                      IPCRenderBufferOp* op, SkCanvas* canvas,
                      const std::function<void(int)>& renderProxyCallback) {
    switch (op->type) {
        case TYPE_SAVE: {
            SaveOp* co = (SaveOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_RESTORE: {
            RestoreOp* co = (RestoreOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_SAVELAYER: {
            SaveLayerOp* co = (SaveLayerOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_SAVEBEHIND: {
            SaveBehindOp* co = (SaveBehindOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_CONCAT: {
            ConcatOp* co = (ConcatOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_SETMATRIX: {
            SetMatrixOp* co = (SetMatrixOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_SCALE: {
            ScaleOp* co = (ScaleOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_TRANSLATE: {
            TranslateOp* co = (TranslateOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_CLIPPATH: {
            ClipPathOp* co = (ClipPathOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_CLIPRECT: {
            ClipRectOp* co = (ClipRectOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_CLIPRRECT: {
            ClipRRectOp* co = (ClipRRectOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_CLIPREGION: {
            ClipRegionOp* co = (ClipRegionOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_CLIPSHADER: {
            ClipShaderOp* co = (ClipShaderOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_RESETCLIP: {
            ResetClipOp* co = (ResetClipOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWPAINT: {
            DrawPaintOp* co = (DrawPaintOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWBEHIND: {
            DrawBehindOp* co = (DrawBehindOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWPATH: {
            DrawPathOp* co = (DrawPathOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWRECT: {
            DrawRectOp* co = (DrawRectOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWREGION: {
            DrawRegionOp* co = (DrawRegionOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWOVAL: {
            DrawOvalOp* co = (DrawOvalOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWARC: {
            DrawArcOp* co = (DrawArcOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWRRECT: {
            DrawRRectOp* co = (DrawRRectOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWANNOTATION: {
            DrawAnnotationOp* co = (DrawAnnotationOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWDRAWABLE: {
            DrawDrawableOp* co = (DrawDrawableOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWPICTURE: {
            DrawPictureOp* co = (DrawPictureOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWIMAGE: {
            DrawImageOp* co = (DrawImageOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWIMAGERECT: {
            DrawImageRectOp* co = (DrawImageRectOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWTEXTBLOB: {
            DrawTextBlobOp* co = (DrawTextBlobOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWPATCH: {
            DrawPatchOp* co = (DrawPatchOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWPOINTS: {
            DrawPointsOp* co = (DrawPointsOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWVERTICES: {
            DrawVerticesOp* co = (DrawVerticesOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWSKMESH: {
            DrawSkMeshOp* co = (DrawSkMeshOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWATLAS: {
            DrawAtlasOp* co = (DrawAtlasOp*)op;
            co->draw(canvas, SkMatrix::I());
            break;
        }
        case TYPE_DRAWPROXYSURFACECONTROL: {
            DrawProxySurfaceControlOp* co = (DrawProxySurfaceControlOp*)op;
            // co->draw(canvas, SkMatrix::I());
            renderProxyCallback(co->proxyId);
            break;
        }
        default: {
            ALOGE("Unexpected op in RenderCommandBuffer");
            break;
        }
    }
}

bool isDrawingOp(uint32_t type) {
    switch (type) {
        case TYPE_SAVE:
        case TYPE_RESTORE:
        case TYPE_SAVELAYER:
        case TYPE_SAVEBEHIND:
        case TYPE_CONCAT:
        case TYPE_SETMATRIX:
        case TYPE_SCALE:
        case TYPE_TRANSLATE:
        case TYPE_CLIPPATH:
        case TYPE_CLIPRECT:
        case TYPE_CLIPRRECT:
        case TYPE_CLIPREGION:
        case TYPE_CLIPSHADER:
        case TYPE_RESETCLIP:
        case TYPE_DRAWPROXYSURFACECONTROL: // Questionable
            return false;
        case TYPE_DRAWPAINT:
        case TYPE_DRAWBEHIND:
        case TYPE_DRAWPATH:
        case TYPE_DRAWRECT:
        case TYPE_DRAWREGION:
        case TYPE_DRAWOVAL:
        case TYPE_DRAWARC:
        case TYPE_DRAWRRECT:
        case TYPE_DRAWDRRECT:
        case TYPE_DRAWANNOTATION:
        case TYPE_DRAWDRAWABLE:
        case TYPE_DRAWPICTURE:
        case TYPE_DRAWIMAGE:
        case TYPE_DRAWIMAGERECT:
        case TYPE_DRAWIMAGELATTICE:
        case TYPE_DRAWTEXTBLOB:
        case TYPE_DRAWPATCH:
        case TYPE_DRAWPOINTS:
        case TYPE_DRAWVERTICES:
        case TYPE_DRAWATLAS:
        case TYPE_DRAWSHADOWREC:
        case TYPE_DRAWVECTORDRAWABLE:
        case TYPE_DRAWRIPPLEDRAWABLE:
        case TYPE_DRAWWEBVIEW:
        case TYPE_DRAWSKMESH:
        case TYPE_DRAWMESH:
            return true;
        default:
            return false;
    }
}

bool renderCommandBufferToCanvas(const std::shared_ptr<RenderCommandBufferConsumer>& consumer,
                                 SkCanvas* canvas,
                                 const std::function<void(int)>& renderProxyCallback) {
    auto buffer = consumer->consumerAcquire();

    if (consumer->getContext() == nullptr) {
        OffsetToImageCache* c = new OffsetToImageCache();
        consumer->setContext(c, [](void* ctx) { delete (OffsetToImageCache*)ctx; });
    }

    bool foundFirstDrawingOp = false;

    if constexpr (DUMP_OPS) {
        ALOGE("Rendering command buffer");
    }

    for (IPCRenderBufferOp* op = buffer->getOps(); op; op = op->next) {
        if (!foundFirstDrawingOp && isDrawingOp(op->type)) {
            foundFirstDrawingOp = true;
            if (op->type == TYPE_DRAWPAINT) {
                DrawPaintOp* co = (DrawPaintOp*)op;
                const ShmemPaint& paint = co->paint;
                if (paint.color.fR == 0.0f && paint.color.fG == 0.0f && paint.color.fB == 0.0f &&
                    paint.color.fA == 0.0f) {
                  if constexpr (DUMP_OPS) {
                      ALOGE("Skipping clear paint");
                  }
                    continue;
                }
            }
        }
        if constexpr (DUMP_OPS) {
            ALOGE("Rendering op %s", opTypeToString(op->type).c_str());
            ALOGE("Details %s", opToString(op).c_str());
        }
        renderOpToCanvas(consumer, op, canvas, renderProxyCallback);
    }
    if constexpr (DUMP_OPS) {
        ALOGE("Done rendering command buffer");
    }
    return true;
}

void resetRenderCommandBufferForReplay(
        const std::shared_ptr<RenderCommandBufferConsumer>& consumer) {
    auto buffer = consumer->consumerAcquire();
    if (buffer == nullptr) {
        ALOGE("Failed to acquire RenderCommandBuffer for replay");
        return;
    }

    for (IPCRenderBufferOp* op = buffer->getOps(); op; op = op->next) {
        if (op->type == TYPE_DRAWPATH) {
            DrawPathOp* co = (DrawPathOp*)op;
            co->resetForReplay();
        } else if (op->type == TYPE_DRAWTEXTBLOB) {
            DrawTextBlobOp* co = (DrawTextBlobOp*)op;
            co->resetForReplay();
        }
    }
}

std::string shmemPaintToString(const ShmemPaint& paint) {
    return std::string("color: ") + std::to_string(paint.color.fR) + std::string(" ") +
            std::to_string(paint.color.fG) + std::string(" ") + std::to_string(paint.color.fB) +
            std::string(" ") + std::to_string(paint.color.fA) + std::string(" ") +
            std::string(" style: ") + std::to_string((int)paint.style) + std::string(" ") +
            std::to_string((int)paint.blendMode);
}

} // namespace android

#pragma clang diagnostic pop
