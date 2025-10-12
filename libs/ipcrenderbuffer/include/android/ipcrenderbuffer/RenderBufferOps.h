/*
 * Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include <SkRect.h>
#include <gui/RenderCommandBuffer.h>
#include <gui/RenderCommandBufferConsumer.h>

#include <SkAndroidFrameworkUtils.h>
#include <SkCanvas.h>
/*
#include <SkCanvasPriv.h>
*/
#include <SkCanvasVirtualEnforcer.h>
#include <SkColor.h>
#include <SkDrawable.h>
// #include <SkGainmapInfo.h>
#include <SkBitmap.h>
#include <SkImage.h>
#include <SkImageAndroid.h>
#include <SkNoDrawCanvas.h>
#include <SkPaint.h>
#include <SkPath.h>
#include <SkPixmap.h>
#include <SkRRect.h>
#include <SkRect.h>
#include <SkRegion.h>
#include <SkRuntimeEffect.h>
#include <SkSerialProcs.h>
#include <SkTextBlob.h>
#include <SkVertices.h>

#include <SkStream.h>
#include <sys/stat.h>
// #include <ports/SkFontMgr_android.h>
#include <SkColorSpace.h>
#include <SkData.h>
#include <SkFontArguments.h>
#include <SkFontMgr.h>
#include <SkFontMgr_android.h>
#include <SkFontMgr_android_ndk.h>
#include <SkFontMgr_empty.h>

#include <functional>
#include <map>

#include <android/ipcrenderbuffer/RenderBufferDebugUtils.h>
#include <android/ipcrenderbuffer/RenderBufferOpTypes.h>
#include <android/ipcrenderbuffer/RenderBufferShmemImageInfo.h>
#include <android/ipcrenderbuffer/RenderBufferShmemPaint.h>

#define IPCRENDERBUFFER_UNIMPLEMENTED_IS_FATAL 0
#ifdef IPCRENDERRBUFFER_UNIMPLEMENTED_IS_FATAL
#define IPCRENDERBUFFER_UNIMPLEMENTED LOG_ALWAYS_FATAL("Not implemented %s", __FUNCTION)
#else
#define IPCRENDERBUFFER_UNIMPLEMENTED ALOGE("Not implemented %s", __FUNCTION__)
#endif

namespace android {

// Derived from RecordingCanvas.cpp
struct SaveOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SAVE;

    static SaveOp* Create(RenderCommandBuffer* commandBuffer) {
        SaveOp* op = commandBuffer->alloc<SaveOp>();
        if (!op) return nullptr;
        op->type = kType;
        return op;
    }
    void draw(SkCanvas* c, const SkMatrix&) { c->save(); }
    std::string toString() const { return std::string("SaveOp"); }
};

struct RestoreOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_RESTORE;

    static RestoreOp* Create(RenderCommandBuffer* commandBuffer) {
        RestoreOp* op = commandBuffer->alloc<RestoreOp>();
        if (!op) return nullptr;
        op->type = kType;
        return op;
    }
    void draw(SkCanvas* c, const SkMatrix&) { c->restore(); }
    std::string toString() const { return std::string("RestoreOp"); }
};

struct SaveLayerOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SAVELAYER;
    SkRect bounds;
    ShmemPaint paint;
    bool hasBounds;
    bool hasPaint;

    static SaveLayerOp* Create(RenderCommandBuffer* commandBuffer, const SkRect* bounds,
                               const SkPaint* paint) {
        SaveLayerOp* op = commandBuffer->alloc<SaveLayerOp>();
        if (!op) return nullptr;
        op->type = kType;
        if (bounds) {
            op->bounds = *bounds;
            op->hasBounds = true;
        } else {
            op->hasBounds = false;
        }
        if (paint) {
            op->paint = toShmemPaint(*paint);
            op->hasPaint = true;
        } else {
            op->hasPaint = false;
        }
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) {
        const SkRect* boundsPtr = hasBounds ? &bounds : nullptr;
        SkPaint p;
        const SkPaint* paintPtr = hasPaint ? &(p = fromShmemPaint(paint)) : nullptr;
        c->saveLayer(boundsPtr, paintPtr);
    }
    std::string toString() const { return std::string("SaveLayerOp"); }
};

struct SaveBehindOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SAVEBEHIND;
    SkRect subset;

    static SaveBehindOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& subset) {
        SaveBehindOp* op = commandBuffer->alloc<SaveBehindOp>();
        if (!op) return nullptr;
        op->type = kType;
        if (subset.isEmpty()) {
            op->subset.setEmpty();
        } else {
            op->subset = subset;
        }
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { SkAndroidFrameworkUtils::SaveBehind(c, &subset); }
    std::string toString() const {
        return std::string("SaveBehindOp subset: ") + rectToString(subset);
    }
};

struct ConcatOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CONCAT;
    SkM44 matrix;

    static ConcatOp* Create(RenderCommandBuffer* commandBuffer, const SkM44& matrix) {
        ConcatOp* op = commandBuffer->alloc<ConcatOp>();
        if (!op) return nullptr;
        op->type = kType;
        op->matrix = matrix;
        return op;
    }
    void draw(SkCanvas* c, const SkMatrix&) { c->concat(matrix); }
    std::string toString() const {
        return std::string("ConcatOp matrix: ") + skmatrixToString(matrix);
    }
};

struct SetMatrixOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SETMATRIX;
    SkM44 matrix;

    static SetMatrixOp* Create(RenderCommandBuffer* commandBuffer, const SkM44& matrix) {
        SetMatrixOp* op = commandBuffer->alloc<SetMatrixOp>();
        if (!op) return nullptr;
        op->type = kType;
        op->matrix = matrix;
        return op;
    }
    void draw(SkCanvas* c, const SkMatrix& original) { c->setMatrix(SkM44(original) * matrix); }
    std::string toString() const {
        return std::string("SetMatrixOp matrix: ") + skmatrixToString(matrix);
    }
};

struct ScaleOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SCALE;
    SkScalar sx, sy;

    static ScaleOp* Create(RenderCommandBuffer* commandBuffer, SkScalar sx, SkScalar sy) {
        ScaleOp* op = commandBuffer->alloc<ScaleOp>();
        if (!op) return nullptr;
        op->type = kType;
        op->sx = sx;
        op->sy = sy;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { c->scale(sx, sy); }
    std::string toString() const {
        return std::string("ScaleOp sx: ") + std::to_string(sx) + std::string(" sy: ") +
                std::to_string(sy);
    }
};

struct TranslateOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_TRANSLATE;
    SkScalar dx, dy;

    static TranslateOp* Create(RenderCommandBuffer* commandBuffer, SkScalar dx, SkScalar dy) {
        TranslateOp* op = commandBuffer->alloc<TranslateOp>();
        if (!op) return nullptr;
        op->type = kType;
        op->dx = dx;
        op->dy = dy;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { c->translate(dx, dy); }
    std::string toString() const {
        return std::string("TranslateOp dx: ") + std::to_string(dx) + std::string(" dy: ") +
                std::to_string(dy);
    }
};

struct ClipPathOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPPATH;
    SkClipOp op;
    bool aa;
    RSpan<uint8_t> pathData;

    static ClipPathOp* Create(RenderCommandBuffer* commandBuffer, const SkPath& path, SkClipOp op,
                              bool aa) {
        ClipPathOp* opData = commandBuffer->alloc<ClipPathOp>();
        if (!opData) return nullptr;
        size_t pathSize = path.writeToMemory(nullptr);
        if (!SetRSpan<uint8_t>(opData->pathData, commandBuffer, nullptr, pathSize)) {
            return nullptr;
        }
        path.writeToMemory(opData->pathData.data.get());
        opData->type = kType;
        opData->op = op;
        opData->aa = aa;
        return opData;
    }

    void draw(SkCanvas* c, const SkMatrix&) {
        SkPath path;
        path.readFromMemory(pathData.data.get(), pathData.size);
        c->clipPath(path, op, aa);
    }
    std::string toString() const { return "ClipPathOp"; }
};

struct ClipRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPRECT;
    SkRect rect;
    SkClipOp op;
    bool aa;

    static ClipRectOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& rect, SkClipOp op,
                              bool aa) {
        ClipRectOp* opData = commandBuffer->alloc<ClipRectOp>();
        if (!opData) return nullptr;
        opData->type = kType;
        opData->rect = rect;
        opData->op = op;
        opData->aa = aa;
        return opData;
    }

    void draw(SkCanvas* c, const SkMatrix&) { c->clipRect(rect, op, aa); }
    std::string toString() const {
        return std::string("ClipRectOp: rect: ") + rectToString(rect) + std::string(" op: ") +
                std::to_string((int)op) + std::string(" aa: ") + std::to_string(aa);
    }
};

struct ClipRRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPRRECT;
    SkRRect rrect;
    SkClipOp op;
    bool aa;

    static ClipRRectOp* Create(RenderCommandBuffer* commandBuffer, const SkRRect& rrect,
                               SkClipOp op, bool aa) {
        ClipRRectOp* opData = commandBuffer->alloc<ClipRRectOp>();
        if (!opData) return nullptr;
        opData->type = kType;
        opData->rrect = rrect;
        opData->op = op;
        opData->aa = aa;
        return opData;
    }

    void draw(SkCanvas* c, const SkMatrix&) { c->clipRRect(rrect, op, aa); }
    std::string toString() const {
        std::string rectAsString = std::string(rrect.dumpToString(false).c_str());
        return std::string("ClipRRectOp: rrect: ") + rectAsString + std::string(" op: ") +
                std::to_string((int)op) + std::string(" aa: ") + std::to_string(aa);
    }
};

struct ClipRegionOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPREGION;
    SkClipOp op;
    RSpan<uint8_t> regionData;

    static ClipRegionOp* Create(RenderCommandBuffer* commandBuffer, const SkRegion& region,
                                SkClipOp op) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        return nullptr;
    }

    void draw(SkCanvas* c, const SkMatrix&) {
        SkRegion region;
        region.readFromMemory(regionData.data.get(), regionData.size);
        c->clipRegion(region, op);
    }
    std::string toString() const { return "ClipRegionOp"; }
};

struct ClipShaderOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPSHADER;

    static ClipShaderOp* Create(RenderCommandBuffer* commandBuffer,
                                const sk_sp<SkShader>& /*shader*/, SkClipOp op) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
        return nullptr;
    }

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    SkClipOp op;
    std::string toString() const { return "ClipShaderOp"; }
};

struct ResetClipOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_RESETCLIP;

    static ResetClipOp* Create(RenderCommandBuffer* commandBuffer) {
        ResetClipOp* op = commandBuffer->alloc<ResetClipOp>();
        if (!op) return nullptr;
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { SkAndroidFrameworkUtils::ResetClip(c); }
    std::string toString() const { return "ResetClipOp"; }
};

struct DrawPaintOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPAINT;
    ShmemPaint paint;

    static DrawPaintOp* Create(RenderCommandBuffer* commandBuffer, const SkPaint& p) {
        DrawPaintOp* op = commandBuffer->alloc<DrawPaintOp>();
        if (!op) return nullptr;
        op->paint = toShmemPaint(p);
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { c->drawPaint(fromShmemPaint(paint)); }
    std::string toString() const { return "DrawPaintOp" + shmemPaintToString(paint); }
};

struct DrawBehindOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWBEHIND;
    ShmemPaint paint;

    static DrawBehindOp* Create(RenderCommandBuffer* commandBuffer, const SkPaint& p) {
        DrawBehindOp* op = commandBuffer->alloc<DrawBehindOp>();
        if (!op) return nullptr;
        op->paint = toShmemPaint(p);
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawBehindOp"; }
};

struct DrawPathOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPATH;
    ShmemPaint paint;
    RSpan<uint8_t> pathData;

    static DrawPathOp* Create(RenderCommandBuffer* commandBuffer, uint8_t* blob, size_t bs,
                              const SkPaint& p) {
        DrawPathOp* op = commandBuffer->alloc<DrawPathOp>();
        if (!op) return nullptr;
        if (!SetRSpan(op->pathData, commandBuffer, blob, bs)) {
            return nullptr;
        }
        op->paint = toShmemPaint(p);
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) {
        SkPath path;
        path.readFromMemory(pathData.data.get(), pathData.size);
        c->drawPath(path, fromShmemPaint(paint));
    }
    std::string toString() const { return "DrawPathOp"; }
    void resetForReplay() {}
};

struct DrawRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWRECT;
    SkRect rect;
    ShmemPaint paint;

    static DrawRectOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& r,
                              const SkPaint& p) {
        DrawRectOp* op = commandBuffer->alloc<DrawRectOp>();
        if (!op) return nullptr;
        op->rect = r;
        op->paint = toShmemPaint(p);
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { c->drawRect(rect, fromShmemPaint(paint)); }
    std::string toString() const {
        return std::string("DrawRectOp rect(") + rectToString(rect) + ") " +
                std::string(" paint: ") + shmemPaintToString(paint);
    }
};

struct DrawRegionOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWREGION;
    RSpan<uint8_t> regionData;
    ShmemPaint paint;

    static DrawRegionOp* Create(RenderCommandBuffer* commandBuffer, const SkRegion& r,
                                const SkPaint& p) {
        DrawRegionOp* op = commandBuffer->alloc<DrawRegionOp>();
        if (!op) return nullptr;
        size_t regionSize = r.writeToMemory(nullptr);
        if (!SetRSpan<uint8_t>(op->regionData, commandBuffer, nullptr, regionSize)) {
            return nullptr;
        }
        r.writeToMemory(op->regionData.data.get());
        op->paint = toShmemPaint(p);
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) {
        SkRegion region;
        region.readFromMemory(regionData.data.get(), regionData.size);
        c->drawRegion(region, fromShmemPaint(paint));
    }
    std::string toString() const { return "DrawRegionOp"; }
};

struct DrawOvalOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWOVAL;
    SkRect oval;
    ShmemPaint paint;

    static DrawOvalOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& o,
                              const SkPaint& p) {
        DrawOvalOp* op = commandBuffer->alloc<DrawOvalOp>();
        if (!op) return nullptr;
        op->oval = o;
        op->paint = toShmemPaint(p);
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { c->drawOval(oval, fromShmemPaint(paint)); }
    std::string toString() const { return std::string("DrawOvalOp") + rectToString(oval); }
};

struct DrawArcOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWARC;
    SkRect oval;
    SkScalar startAngle;
    SkScalar sweepAngle;
    bool useCenter;
    ShmemPaint paint;

    static DrawArcOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& oval,
                             SkScalar startAngle, SkScalar sweepAngle, bool useCenter,
                             const SkPaint& paint) {
        DrawArcOp* op = commandBuffer->alloc<DrawArcOp>();
        if (!op) return nullptr;
        op->type = kType;
        op->paint = toShmemPaint(paint);
        op->oval = oval;
        op->startAngle = startAngle;
        op->sweepAngle = sweepAngle;
        op->useCenter = useCenter;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) {
        c->drawArc(oval, startAngle, sweepAngle, useCenter, fromShmemPaint(paint));
    }
    std::string toString() const {
        return std::string("DrawArcOp") + rectToString(oval) + std::string(" startAngle: ") +
                std::to_string(startAngle) + std::string(" sweepAngle: ") +
                std::to_string(sweepAngle) + std::string(" useCenter: ") +
                std::to_string(useCenter);
    }
};

struct DrawRRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWRRECT;
    SkRRect rrect;
    ShmemPaint paint;

    static DrawRRectOp* Create(RenderCommandBuffer* commandBuffer, const SkRRect& rr,
                               const SkPaint& p) {
        DrawRRectOp* op = commandBuffer->alloc<DrawRRectOp>();
        if (!op) return nullptr;
        op->paint = toShmemPaint(p);
        op->rrect = rr;
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { c->drawRRect(rrect, fromShmemPaint(paint)); }
    std::string toString() const {
        return std::string("DrawRRectOp") + std::string(rrect.dumpToString(false).c_str());
    }
};

struct DrawAnnotationOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWANNOTATION;

    static DrawAnnotationOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& rect,
                                    const char* text, SkData* data) {
        DrawAnnotationOp* op = commandBuffer->alloc<DrawAnnotationOp>();
        if (!op) return nullptr;
        IPCRENDERBUFFER_UNIMPLEMENTED;
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawAnnotationOp"; }
};

struct DrawDrawableOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWDRAWABLE;

    static DrawDrawableOp* Create(RenderCommandBuffer* commandBuffer, SkDrawable* drawable,
                                  const SkMatrix* matrix) {
        DrawDrawableOp* op = commandBuffer->alloc<DrawDrawableOp>();
        if (!op) return nullptr;
        IPCRENDERBUFFER_UNIMPLEMENTED;
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawDrawableOp"; }
};

struct DrawPictureOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPICTURE;

    static DrawPictureOp* Create(RenderCommandBuffer* commandBuffer, const SkPicture* picture,
                                 const SkMatrix* matrix, const SkPaint* paint) {
        DrawPictureOp* op = commandBuffer->alloc<DrawPictureOp>();
        if (!op) return nullptr;
        IPCRENDERBUFFER_UNIMPLEMENTED;
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawPictureOp"; }
};

struct DrawImageOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWIMAGE;

    static DrawImageOp* Create(RenderCommandBuffer* commandBuffer, const SkImage* image, SkScalar x,
                               SkScalar y, const SkSamplingOptions& sampling,
                               const SkPaint* paint) {
        DrawImageOp* op = commandBuffer->alloc<DrawImageOp>();
        if (!op) return nullptr;
        IPCRENDERBUFFER_UNIMPLEMENTED;
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawImageOp"; }
};

// TODO(b/448196792): Implement hardware bitmap support
struct DrawImageRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWIMAGERECT;
    int offset;
    SkRect srcRect;
    SkRect dstRect;
    SkSamplingOptions sampling;
    ShmemPaint paint;
    int constraint;
    bool hasPaint;
    int uniqueId;
    bool isHardware;

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    virtual std::string toString() const { return "DrawImageRectOp"; }
};

struct DrawTextBlobOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWTEXTBLOB;
    ShmemPaint paint;
    SkScalar x;
    SkScalar y;
    RSpan<uint8_t> blobData;

    static DrawTextBlobOp* Create(RenderCommandBuffer* commandBuffer, uint8_t* blob, size_t bs,
                                  SkScalar x_in, SkScalar y_in, const SkPaint& p) {
        DrawTextBlobOp* op = commandBuffer->alloc<DrawTextBlobOp>();
        if (!op) return nullptr;
        op->type = kType;
        op->paint = toShmemPaint(p);
        if (!SetRSpan(op->blobData, commandBuffer, blob, bs)) {
            return nullptr;
        }
        op->x = x_in;
        op->y = y_in;
        return op;
    }
    void draw(SkCanvas* c, const SkMatrix&) {
        IPCRENDERBUFFER_UNIMPLEMENTED;
    }
    std::string toString() const { return "DrawTextBlobOp"; }

    void resetForReplay() {}
};

struct DrawPatchOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPATCH;
    RSpan<SkPoint> points;
    RSpan<SkColor> colors;
    RSpan<SkPoint> texCoords;
    SkBlendMode mode;
    ShmemPaint paint;

    static DrawPatchOp* Create(RenderCommandBuffer* commandBuffer, const SkPoint inPoints[12],
                               const SkColor inColors[4], const SkPoint inTexCoords[4],
                               SkBlendMode inMode, const SkPaint& inPaint) {
        DrawPatchOp* op = commandBuffer->alloc<DrawPatchOp>();
        if (!op) return nullptr;
        op->type = kType;
        op->mode = inMode;
        op->paint = toShmemPaint(inPaint);

        if (!SetRSpan(op->points, commandBuffer, inPoints, 12)) {
            return nullptr;
        }
        if (!SetRSpan(op->colors, commandBuffer, inColors, 4)) {
            return nullptr;
        }
        if (!SetRSpan(op->texCoords, commandBuffer, inTexCoords, 4)) {
            return nullptr;
        }
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) {
        c->drawPatch(points.data.get(), colors.data.get(), texCoords.data.get(), mode,
                     fromShmemPaint(paint));
    }
    std::string toString() const { return "DrawPatchOp"; }
};

struct DrawPointsOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPOINTS;
    SkCanvas::PointMode mode;
    RSpan<SkPoint> points;
    ShmemPaint paint;

    static DrawPointsOp* Create(RenderCommandBuffer* commandBuffer, SkCanvas::PointMode mode,
                                size_t count, const SkPoint* points, const SkPaint& paint) {
        DrawPointsOp* op = commandBuffer->alloc<DrawPointsOp>();
        if (!op) return nullptr;
        op->type = kType;
        op->mode = mode;
        op->paint = toShmemPaint(paint);
        if (!SetRSpan(op->points, commandBuffer, points, count)) {
            return nullptr;
        }
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) {
        c->drawPoints(mode, points.size, points.data.get(), fromShmemPaint(paint));
    }
    std::string toString() const { return "DrawPointsOp"; }
};

struct DrawVerticesOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWVERTICES;
    SkBlendMode mode;
    ShmemPaint paint;
    RSpan<uint8_t> verticesData;

    static DrawVerticesOp* Create(RenderCommandBuffer* commandBuffer, const SkVertices* vertices,
                                  SkBlendMode mode, const SkPaint& paint) {
        DrawVerticesOp* op = commandBuffer->alloc<DrawVerticesOp>();
        if (!op) return nullptr;
        op->type = kType;
        op->mode = mode;
        op->paint = toShmemPaint(paint);
        IPCRENDERBUFFER_UNIMPLEMENTED;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawVerticesOp"; }
};

/*struct DrawMeshOp final : IPCRenderBufferOp {
  static const auto kType = TYPE_DRAWMESH;
  DrawMeshOp(const Mesh& mesh, const SkPaint& paint) {
    ALOGE("Not implemented %s", __FUNCTION__);
  }
};*/

struct DrawSkMeshOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWMESH;

    static DrawSkMeshOp* Create(RenderCommandBuffer* commandBuffer, const SkMesh& mesh,
                                sk_sp<SkBlender> blender, const SkPaint& paint) {
        DrawSkMeshOp* op = commandBuffer->alloc<DrawSkMeshOp>();
        if (!op) return nullptr;
        op->type = kType;
        IPCRENDERBUFFER_UNIMPLEMENTED;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawSkMeshOp"; }
};

struct DrawAtlasOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWATLAS;

    static DrawAtlasOp* Create(RenderCommandBuffer* commandBuffer, const SkImage* atlas,
                               const SkRSXform* xform, const SkRect* tex, const SkColor* colors,
                               int count, SkBlendMode mode, const SkSamplingOptions& sampling,
                               const SkRect* cull, const SkPaint* paint) {
        DrawAtlasOp* op = commandBuffer->alloc<DrawAtlasOp>();
        if (!op) return nullptr;
        IPCRENDERBUFFER_UNIMPLEMENTED;
        op->type = kType;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) { IPCRENDERBUFFER_UNIMPLEMENTED; }
    std::string toString() const { return "DrawAtlasOp"; }
};

struct DrawProxySurfaceControlOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPROXYSURFACECONTROL;
    int proxyId;

    static DrawProxySurfaceControlOp* Create(RenderCommandBuffer* commandBuffer, int id) {
        DrawProxySurfaceControlOp* op = commandBuffer->alloc<DrawProxySurfaceControlOp>();
        if (!op) return nullptr;
        op->type = kType;
        op->proxyId = id;
        return op;
    }

    void draw(SkCanvas* c, const SkMatrix&) {
        LOG_ALWAYS_FATAL_IF("DrawProxySurfaceControlOp::draw unexpected");
    }

    std::string toString() const { return "DrawProxySurfaceControlOp"; }
};
} // namespace android
