
#include "BoxShadowUtils.h"

#include <common/trace.h>
#include <include/core/SkBlurTypes.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkColor.h>
#include <include/core/SkColorFilter.h>
#include <include/core/SkImage.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkMaskFilter.h>
#include <include/core/SkPaint.h>
#include <include/core/SkRRect.h>
#include <include/core/SkRect.h>
#include <include/core/SkShader.h>
#include <include/core/SkSurface.h>
#include <include/core/SkVertices.h>
#include <ui/BlurRegion.h>

namespace android::renderengine::skia {

namespace {

const float kSigmaFactor = 3.0;
const float kSin45Deg = 0.7071067811f;

sk_sp<SkImage> makeBlurredRRect(SkiaGpuContext* context, float sigma, float cornerRadius) {
    float kernelRadius = kSigmaFactor * sigma;
    float size = kernelRadius + cornerRadius + cornerRadius + kernelRadius;

    SkRect rrectBounds = SkRect::MakeXYWH(kernelRadius, kernelRadius, size - 2 * kernelRadius,
                                          size - 2 * kernelRadius);
    SkRRect rrectToBlur = SkRRect::MakeRectXY(rrectBounds, cornerRadius, cornerRadius);

    SkImageInfo info = SkImageInfo::Make(size, size, kRGBA_8888_SkColorType, kPremul_SkAlphaType);

    sk_sp<SkSurface> surface = context->createRenderTarget(info);
    LOG_ALWAYS_FATAL_IF(surface == nullptr,
                        "Failed to create render target for box shadow texture!");

    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);

    SkPaint blurPaint;
    blurPaint.setAntiAlias(true);
    blurPaint.setColor(SK_ColorWHITE);
    blurPaint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma));

    canvas->drawRRect(rrectToBlur, blurPaint);
    return surface->makeImageSnapshot()->withDefaultMipmaps();
}

void drawNineSlice(SkCanvas* canvas, const sk_sp<SkShader> shader, const SkRect& dest,
                   const SkColor& color, float cornerScale) {
    int kVertexCount = 16;
    // 8 quads * 6 indices per quad
    int kImageIndexCount = 8 * 6;

    SkVertices::Builder builder(SkVertices::kTriangles_VertexMode, kVertexCount, kImageIndexCount,
                                SkVertices::kHasTexCoords_BuilderFlag);

    SkImage* image = shader->isAImage(nullptr, (SkTileMode*)nullptr);
    float s = static_cast<float>(image->width());

    // Corner source size.
    float c = s / 2.0f;

    // Clamp corner dest size to prevent corners from overlapping.
    float dstCornerSize = c * cornerScale;
    float xc = std::min(dstCornerSize, 0.5f * (dest.fRight - dest.fLeft));
    float yc = std::min(dstCornerSize, 0.5f * (dest.fBottom - dest.fTop));

    // Dest coordinates
    float dx[] = {dest.fLeft, dest.fLeft + xc, dest.fRight - xc, dest.fRight};
    float dy[] = {dest.fTop, dest.fTop + yc, dest.fBottom - yc, dest.fBottom};

    // Source coordinates adjusted to take into account dest center tile expansion.
    float sx[] = {0.0f, c, c, s};
    float sy[] = {0.0f, c, c, s};

    SkPoint* posPtr = builder.positions();
    SkPoint* texPtr = builder.texCoords();

    // Populate the 16 vertices
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            int v_idx = j * 4 + i;
            posPtr[v_idx] = {dx[i], dy[j]};
            texPtr[v_idx] = {sx[i], sy[j]};
        }
    }

    uint16_t* indicesPtr = builder.indices();
    int indicesOffset = 0;

    // Add indices for the 8 image patches
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            if (i == 1 && j == 1) continue; // skip center

            uint16_t v00 = j * 4 + i;
            uint16_t v01 = j * 4 + (i + 1);
            uint16_t v10 = (j + 1) * 4 + i;
            uint16_t v11 = (j + 1) * 4 + (i + 1);

            indicesPtr[indicesOffset + 0] = v00;
            indicesPtr[indicesOffset + 1] = v01;
            indicesPtr[indicesOffset + 2] = v10;

            indicesPtr[indicesOffset + 3] = v01;
            indicesPtr[indicesOffset + 4] = v11;
            indicesPtr[indicesOffset + 5] = v10;
            indicesOffset += 6;
        }
    }

    sk_sp<SkVertices> skirtVerts = builder.detach();

    SkPaint skirtPaint;
    skirtPaint.setColorFilter(SkColorFilters::Blend(color, SkBlendMode::kSrcIn));
    skirtPaint.setShader(shader);
    skirtPaint.setAntiAlias(false);
    canvas->drawVertices(skirtVerts, /*ignored*/ SkBlendMode::kSrcOver, skirtPaint);
}

} // namespace

void BoxShadowUtils::init(SkiaGpuContext* context) {
    int memorySize = 0;
    for (int i = 0; i < kSupportedBlurRadius.size(); i++) {
        float blurRadius = kSupportedBlurRadius[i];
        float sigma = convertBlurUserRadiusToSigma(blurRadius);
        sk_sp<SkImage> image = makeBlurredRRect(context, sigma, kDefaultCornerRadius);
        SkSamplingOptions sampling(SkFilterMode::kLinear, SkMipmapMode::kLinear);
        mBlurImages[i] = image->makeShader(sampling);

        memorySize += image->width() * image->height() * 4;
    }

    ALOGI("[BoxShadowUtils] Shadow memory size: %d MB", memorySize / (1024 * 1024));
}

void BoxShadowUtils::cleanup() {
    for (int i = 0; i < kSupportedBlurRadius.size(); i++) {
        mBlurImages[i] = nullptr;
    }
}

void BoxShadowUtils::drawBoxShadows(SkCanvas* canvas, const SkRect& rect, float cornerRadius,
                                    const android::gui::BoxShadowSettings& settings) {
    for (const gui::BoxShadowSettings::BoxShadowParams& box : settings.boxShadows) {
        SkRect boxRect = rect;
        boxRect.outset(box.spreadRadius, box.spreadRadius);
        boxRect.offset(box.offsetX, box.offsetY);

        float desiredCornerRadius = std::max(4.0f, cornerRadius + box.spreadRadius);
        float cornerScale = desiredCornerRadius / kDefaultCornerRadius;

        float optimalBlurRadius = kDefaultCornerRadius * (box.blurRadius / desiredCornerRadius);

        int blurRadiusIndex = 0;
        float minError = std::abs(optimalBlurRadius - kSupportedBlurRadius[0]);
        for (int i = 1; i < kSupportedBlurRadius.size(); i++) {
            float err = std::abs(optimalBlurRadius - kSupportedBlurRadius[i]);
            if (err <= minError) {
                blurRadiusIndex = i;
                minError = err;
            }
        }

        float effectiveBlurRadius = kSupportedBlurRadius[blurRadiusIndex];
        float sigma = convertBlurUserRadiusToSigma(effectiveBlurRadius);

        float kernelRadius = sigma * kSigmaFactor * cornerScale;
        boxRect.outset(kernelRadius, kernelRadius);

        sk_sp<SkShader> shader = mBlurImages[blurRadiusIndex];
        drawNineSlice(canvas, shader, boxRect, box.color, cornerScale);
    }
}

} // namespace android::renderengine::skia