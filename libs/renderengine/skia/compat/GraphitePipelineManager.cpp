/*
 * Copyright 2025 The Android Open Source Project
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

#include "GraphitePipelineManager.h"

#include <include/core/SkData.h>
#include <include/core/SkRefCnt.h>
#include <include/effects/SkRuntimeEffect.h>
#include <include/gpu/graphite/GraphiteTypes.h>
#include <include/gpu/graphite/PrecompileContext.h>
#include <include/gpu/graphite/precompile/PaintOptions.h>
#include <include/gpu/graphite/precompile/Precompile.h>
#include <include/gpu/graphite/precompile/PrecompileColorFilter.h>
#include <include/gpu/graphite/precompile/PrecompileRuntimeEffect.h>
#include <include/gpu/graphite/precompile/PrecompileShader.h>
#include <include/gpu/graphite/vk/precompile/VulkanPrecompileShader.h>
#include <include/gpu/vk/VulkanTypes.h>

#include <type_traits>

#include "skia/filters/RuntimeEffectManager.h"

namespace android::renderengine::skia {

using namespace skgpu::graphite;

using PrecompileShaders::ImageShaderFlags;

using ::skgpu::graphite::DrawTypeFlags;
using ::skgpu::graphite::PaintOptions;
using ::skgpu::graphite::RenderPassProperties;

struct PrecompileSettings {
    PaintOptions fPaintOptions;
    DrawTypeFlags fDrawTypeFlags = skgpu::graphite::DrawTypeFlags::kNone;
    RenderPassProperties fRenderPassProps;

    bool isSubsetOf(const PrecompileSettings& superSet) const {
        return (fDrawTypeFlags & superSet.fDrawTypeFlags) &&
                fRenderPassProps == superSet.fRenderPassProps;
    }
};

// Used in lieu of SkEnumBitMask to avoid adding casts when copying in precompile cases.
static constexpr DrawTypeFlags operator|(DrawTypeFlags a, DrawTypeFlags b) {
    return static_cast<DrawTypeFlags>(static_cast<std::underlying_type<DrawTypeFlags>::type>(a) |
                                      static_cast<std::underlying_type<DrawTypeFlags>::type>(b));
}

sk_sp<PrecompileShader> vulkan_ycbcr_image_shader(uint64_t format,
                                                  VkSamplerYcbcrModelConversion model,
                                                  VkSamplerYcbcrRange range,
                                                  VkChromaLocation location, bool pqCS = false) {
    SkColorInfo ci{kRGBA_8888_SkColorType, kPremul_SkAlphaType,
                   pqCS ? SkColorSpace::MakeRGB(SkNamedTransferFn::kPQ, SkNamedGamut::kRec2020)
                        : nullptr};

    skgpu::VulkanYcbcrConversionInfo info;

    info.fExternalFormat = format;
    info.fYcbcrModel = model;
    info.fYcbcrRange = range;
    info.fXChromaOffset = location;
    info.fYChromaOffset = location;
    info.fChromaFilter = VK_FILTER_LINEAR;

    return PrecompileShaders::VulkanYCbCrImage(info,
                                               PrecompileShaders::ImageShaderFlags::kExcludeCubic,
                                               {&ci, 1}, {});
}

// Specifies the child shader to be created for a LinearEffect
enum class ChildType {
    kSolidColor,
    kHWTexture,
    kHWTextureYCbCr247,
};

sk_sp<PrecompileShader> create_child_shader(ChildType childType) {
    switch (childType) {
        case ChildType::kSolidColor:
            return PrecompileShaders::Color();
        case ChildType::kHWTexture: {
            SkColorInfo ci{kRGBA_8888_SkColorType, kPremul_SkAlphaType,
                           SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB,
                                                 SkNamedGamut::kAdobeRGB)};

            return PrecompileShaders::Image(PrecompileShaders::ImageShaderFlags::kExcludeCubic,
                                            {&ci, 1}, {});
        }
        case ChildType::kHWTextureYCbCr247:
            // HardwareImage(3: kEwAAPcAAAAAAAAA)
            return vulkan_ycbcr_image_shader(247, VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020,
                                             VK_SAMPLER_YCBCR_RANGE_ITU_NARROW,
                                             VK_CHROMA_LOCATION_COSITED_EVEN,
                                             /* pqCS= */ true);
    }

    return nullptr;
}

skgpu::graphite::PaintOptions LinearEffect(sk_sp<SkRuntimeEffect> linearEffect, ChildType childType,
                                           SkBlendMode blendMode, bool paintColorIsOpaque = true,
                                           bool matrixColorFilter = false, bool dither = false) {
    PaintOptions paintOptions;

    sk_sp<PrecompileShader> child = create_child_shader(childType);

    paintOptions.setShaders({PrecompileRuntimeEffects::MakePrecompileShader(std::move(linearEffect),
                                                                            {{std::move(child)}})});
    if (matrixColorFilter) {
        paintOptions.setColorFilters({PrecompileColorFilters::Matrix()});
    }
    paintOptions.setBlendModes({blendMode});
    paintOptions.setPaintColorIsOpaque(paintColorIsOpaque);
    paintOptions.setDither(dither);

    return paintOptions;
}

// =======================================
//         PaintOptions
// =======================================
// NOTE: keep in sync with upstream external/skia/tests/graphite/precompile/PrecompileTestUtils.cpp
// clang-format off

PaintOptions SolidSrcover() {
    PaintOptions paintOptions;
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

PaintOptions SolidMatrixCFSrcover() {
    PaintOptions paintOptions;

    paintOptions.setColorFilters({ PrecompileColorFilters::Matrix() });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });

    return paintOptions;
}

PaintOptions TransparentPaintImagePremulHWOnlyMatrixCFSrcover() {
    PaintOptions paintOptions;

    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setColorFilters({ PrecompileColorFilters::Matrix() });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setPaintColorIsOpaque(false);
    return paintOptions;
}

PaintOptions TransparentPaintImagePremulHWOnlyMatrixCFDitherSrcover() {
    PaintOptions paintOptions;

    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setColorFilters({ PrecompileColorFilters::Matrix() });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setPaintColorIsOpaque(false);
    paintOptions.setDither(true);
    return paintOptions;
}

PaintOptions TransparentPaintImageSRGBHWOnlyMatrixCFDitherSrcover() {
    SkColorInfo ci { kRGBA_8888_SkColorType,
                     kPremul_SkAlphaType,
                     SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kAdobeRGB) };

    PaintOptions paintOptions;

    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setColorFilters({ PrecompileColorFilters::Matrix() });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setPaintColorIsOpaque(false);
    paintOptions.setDither(true);
    return paintOptions;
}

PaintOptions TransparentPaintImagePremulHWOnlySrcover() {
    PaintOptions paintOptions;

    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setPaintColorIsOpaque(false);
    return paintOptions;
}

PaintOptions TransparentPaintImageSRGBHWOnlySrcover() {
    SkColorInfo ci { kRGBA_8888_SkColorType,
                     kPremul_SkAlphaType,
                     SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kAdobeRGB) };

    PaintOptions paintOptions;

    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setPaintColorIsOpaque(false);
    return paintOptions;
}

PaintOptions SolidSrcSrcover() {
    PaintOptions paintOptions;
    paintOptions.setBlendModes({ SkBlendMode::kSrc, SkBlendMode::kSrcOver });
    return paintOptions;
}

PaintOptions ImagePremulHWOnlySrc() {
    PaintOptions paintOptions;

    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setBlendModes({ SkBlendMode::kSrc });
    return paintOptions;
}

PaintOptions ImagePremulHWOnlySrcover() {
    PaintOptions paintOptions;

    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

PaintOptions ImageAlphaPremulHWOnlyMatrixCFSrcover() {
    PaintOptions paintOptions;

    SkColorInfo ci { kAlpha_8_SkColorType, kUnpremul_SkAlphaType, nullptr };
    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setColorFilters({ PrecompileColorFilters::Matrix() });

    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

PaintOptions ImageAlphaSRGBHWOnlyMatrixCFSrcover() {
    // Note: this is different from the other SRGB ColorInfos
    SkColorInfo ci { kAlpha_8_SkColorType,
                     kUnpremul_SkAlphaType,
                     SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kAdobeRGB) };

    PaintOptions paintOptions;

    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setColorFilters({ PrecompileColorFilters::Matrix() });

    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

PaintOptions ImagePremulHWOnlyMatrixCFSrcover() {
    PaintOptions paintOptions;

    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setColorFilters({ PrecompileColorFilters::Matrix() });

    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

PaintOptions ImagePremulHWOnlyMatrixCFDitherSrcover() {
    PaintOptions paintOptions;

    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setColorFilters({ PrecompileColorFilters::Matrix() });

    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setDither(true);

    return paintOptions;
}

PaintOptions ImageSRGBHWOnlyMatrixCFDitherSrcover() {
    SkColorInfo ci { kRGBA_8888_SkColorType,
                     kPremul_SkAlphaType,
                     SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kAdobeRGB) };

    PaintOptions paintOptions;

    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });
    paintOptions.setColorFilters({ PrecompileColorFilters::Matrix() });

    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setDither(true);

    return paintOptions;
}

PaintOptions ImageHWOnlySRGBSrcover() {
    PaintOptions paintOptions;

    SkColorInfo ci { kRGBA_8888_SkColorType,
                     kPremul_SkAlphaType,
                     SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB,
                                           SkNamedGamut::kAdobeRGB) };
    paintOptions.setShaders({ PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                       { &ci, 1 },
                                                       {}) });

    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

skgpu::graphite::PaintOptions MouriMapCrosstalkAndChunk16x16(RuntimeEffectManager& effectManager) {
    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    sk_sp<PrecompileShader> img = PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                           { &ci, 1 },
                                                           {});

    sk_sp<PrecompileShader> crosstalk = PrecompileRuntimeEffects::
            MakePrecompileShader(effectManager.getKnownRuntimeEffect(
                                         RuntimeEffectManager::KnownId::
                                                 kMouriMap_CrossTalkAndChunk16x16Effect),
                                 {{std::move(img)}});

    PaintOptions paintOptions;
    paintOptions.setShaders({ std::move(crosstalk) });
    paintOptions.setBlendModes({ SkBlendMode::kSrc });
    return paintOptions;
}

skgpu::graphite::PaintOptions MouriMapChunk8x8Effect(RuntimeEffectManager& effectManager) {
    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    sk_sp<PrecompileShader> img = PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                           { &ci, 1 },
                                                           {});

    sk_sp<PrecompileShader> chunk8x8 = PrecompileRuntimeEffects::
            MakePrecompileShader(effectManager.getKnownRuntimeEffect(
                                         RuntimeEffectManager::KnownId::kMouriMap_Chunk8x8Effect),
                                 {{std::move(img)}});

    PaintOptions paintOptions;
    paintOptions.setShaders({ std::move(chunk8x8) });
    paintOptions.setBlendModes({ SkBlendMode::kSrc });
    return paintOptions;
}

skgpu::graphite::PaintOptions MouriMapBlur(RuntimeEffectManager& effectManager) {
    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    sk_sp<PrecompileShader> img = PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                           { &ci, 1 },
                                                           {});

    sk_sp<PrecompileShader> blur = PrecompileRuntimeEffects::
            MakePrecompileShader(effectManager.getKnownRuntimeEffect(
                                         RuntimeEffectManager::KnownId::kMouriMap_BlurEffect),
                                 {{std::move(img)}});

    PaintOptions paintOptions;
    paintOptions.setShaders({ std::move(blur) });
    paintOptions.setBlendModes({ SkBlendMode::kSrc });
    return paintOptions;
}

skgpu::graphite::PaintOptions MouriMapToneMap(RuntimeEffectManager& effectManager) {
    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    sk_sp<PrecompileShader> img1 = PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                            { &ci, 1 },
                                                            {});
    sk_sp<PrecompileShader> img2 = PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                            { &ci, 1 },
                                                            {});

    sk_sp<PrecompileShader> toneMap = PrecompileRuntimeEffects::
            MakePrecompileShader(effectManager.getKnownRuntimeEffect(
                                         RuntimeEffectManager::KnownId::kMouriMap_TonemapEffect),
                                 {{std::move(img1)}, {std::move(img2)}});

    PaintOptions paintOptions;
    paintOptions.setShaders({ std::move(toneMap) });
    paintOptions.setBlendModes({ SkBlendMode::kSrc });
    return paintOptions;
}

skgpu::graphite::PaintOptions KawaseBlurLowSrcSrcOver(RuntimeEffectManager& effectManager) {
    sk_sp<SkRuntimeEffect> lowSampleBlurEffect = effectManager.getKnownRuntimeEffect(
            RuntimeEffectManager::KnownId::kKawaseBlurDualFilter_LowSampleBlurEffect);

    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    sk_sp<PrecompileShader> img = PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                           { &ci, 1 },
                                                           {});

    sk_sp<PrecompileShader> kawase = PrecompileRuntimeEffects::MakePrecompileShader(
            std::move(lowSampleBlurEffect),
            { { img } });

    PaintOptions paintOptions;
    paintOptions.setShaders({ std::move(kawase) });
    paintOptions.setBlendModes({ SkBlendMode::kSrc, SkBlendMode::kSrcOver });
    return paintOptions;
}

skgpu::graphite::PaintOptions KawaseBlurHighSrc(RuntimeEffectManager& effectManager) {
    sk_sp<SkRuntimeEffect> highSampleBlurEffect = effectManager.getKnownRuntimeEffect(
            RuntimeEffectManager::KnownId::kKawaseBlurDualFilter_HighSampleBlurEffect);

    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    sk_sp<PrecompileShader> img = PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                           { &ci, 1 },
                                                           {});

    sk_sp<PrecompileShader> kawase = PrecompileRuntimeEffects::MakePrecompileShader(
            std::move(highSampleBlurEffect),
            { { img } });

    PaintOptions paintOptions;
    paintOptions.setShaders({ std::move(kawase) });
    paintOptions.setBlendModes({ SkBlendMode::kSrc });
    return paintOptions;
}

skgpu::graphite::PaintOptions BlurFilterMix(RuntimeEffectManager& effectManager) {
    sk_sp<SkRuntimeEffect> mixEffect = effectManager.getKnownRuntimeEffect(
            RuntimeEffectManager::KnownId::kBlurFilter_MixEffect);

    SkColorInfo ci { kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr };
    sk_sp<PrecompileShader> img = PrecompileShaders::Image(ImageShaderFlags::kExcludeCubic,
                                                           { &ci, 1 },
                                                           {});

    sk_sp<PrecompileShader> mix = PrecompileRuntimeEffects::MakePrecompileShader(
            std::move(mixEffect),
            { { img }, { img } });

    PaintOptions paintOptions;
    paintOptions.setShaders({ std::move(mix) });
    paintOptions.setBlendModes({ SkBlendMode::kSrc });
    return paintOptions;
}

PaintOptions ImagePremulYCbCr238Srcover() {
    PaintOptions paintOptions;

    // HardwareImage(3: kHoAAO4AAAAAAAAA)
    paintOptions.setShaders({ vulkan_ycbcr_image_shader(238,
                                                        VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709,
                                                        VK_SAMPLER_YCBCR_RANGE_ITU_NARROW,
                                                        VK_CHROMA_LOCATION_MIDPOINT) });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

PaintOptions TransparentPaintImagePremulYCbCr238Srcover() {
    PaintOptions paintOptions;

    // HardwareImage(3: kHoAAO4AAAAAAAAA)
    paintOptions.setShaders({ vulkan_ycbcr_image_shader(238,
                                                        VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709,
                                                        VK_SAMPLER_YCBCR_RANGE_ITU_NARROW,
                                                        VK_CHROMA_LOCATION_MIDPOINT) });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setPaintColorIsOpaque(false);
    return paintOptions;
}

PaintOptions ImagePremulYCbCr240Srcover() {
    PaintOptions paintOptions;

    // HardwareImage(3: kHIAAPAAAAAAAAAA)
    paintOptions.setShaders({ vulkan_ycbcr_image_shader(240,
                                                        VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709,
                                                        VK_SAMPLER_YCBCR_RANGE_ITU_FULL,
                                                        VK_CHROMA_LOCATION_MIDPOINT) });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    return paintOptions;
}

PaintOptions TransparentPaintImagePremulYCbCr240Srcover() {
    PaintOptions paintOptions;

    // HardwareImage(3: kHIAAPAAAAAAAAAA)
    paintOptions.setShaders({ vulkan_ycbcr_image_shader(240,
                                                        VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709,
                                                        VK_SAMPLER_YCBCR_RANGE_ITU_FULL,
                                                        VK_CHROMA_LOCATION_MIDPOINT) });
    paintOptions.setBlendModes({ SkBlendMode::kSrcOver });
    paintOptions.setPaintColorIsOpaque(false);
    return paintOptions;
}

skgpu::graphite::PaintOptions MouriMapCrosstalkAndChunk16x16YCbCr247(RuntimeEffectManager& effectManager) {
    PaintOptions paintOptions;

    // HardwareImage(3: kEwAAPcAAAAAAAAA)
    sk_sp<PrecompileShader> img = vulkan_ycbcr_image_shader(
            247,
            VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020,
            VK_SAMPLER_YCBCR_RANGE_ITU_NARROW,
            VK_CHROMA_LOCATION_COSITED_EVEN);

    sk_sp<PrecompileShader> crosstalk = PrecompileRuntimeEffects::MakePrecompileShader(
            effectManager.getKnownRuntimeEffect(RuntimeEffectManager::KnownId::kMouriMap_CrossTalkAndChunk16x16Effect),
            { { std::move(img) } });

    paintOptions.setShaders({ std::move(crosstalk) });
    paintOptions.setBlendModes({ SkBlendMode::kSrc });
    return paintOptions;
}

// clang-format on

// =======================================
//         RenderPassProperties
// =======================================
// NOTE: keep in sync with upstream external/skia/tests/graphite/precompile/PrecompileTestUtils.h
// clang-format off

// RGBA version of the above
const skgpu::graphite::RenderPassProperties kRGBA_1_D {
    skgpu::graphite::DepthStencilFlags::kDepth,
    kRGBA_8888_SkColorType,
    /* fDstCS= */ nullptr,
    /* fRequiresMSAA= */ false
};

// RGBA version of the above
const skgpu::graphite::RenderPassProperties kRGBA_4_D {
    skgpu::graphite::DepthStencilFlags::kDepth,
    kRGBA_8888_SkColorType,
    /* fDstCS= */ nullptr,
    /* fRequiresMSAA= */ true
};

// RGBA version of the above
const skgpu::graphite::RenderPassProperties kRGBA_4_DS {
    skgpu::graphite::DepthStencilFlags::kDepthStencil,
    kRGBA_8888_SkColorType,
    /* fDstCS= */ nullptr,
    /* fRequiresMSAA= */ true
};

// RGBA version of the above
const skgpu::graphite::RenderPassProperties kRGBA_1_D_SRGB {
    skgpu::graphite::DepthStencilFlags::kDepth,
    kRGBA_8888_SkColorType,
    SkColorSpace::MakeSRGB(),
    /* fRequiresMSAA= */ false
};

// RGBA version of the above
const skgpu::graphite::RenderPassProperties kRGBA_4_DS_SRGB {
    skgpu::graphite::DepthStencilFlags::kDepthStencil,
    kRGBA_8888_SkColorType,
    SkColorSpace::MakeSRGB(),
    /* fRequiresMSAA= */ true
};

// Single sampled RGBA16F w/ just depth
const skgpu::graphite::RenderPassProperties kRGBA16F_1_D {
    skgpu::graphite::DepthStencilFlags::kDepth,
    kRGBA_F16_SkColorType,
    /* fDstCS= */ nullptr,
    /* fRequiresMSAA= */ false
};

// The same as kRGBA16F_1_D but w/ an SRGB colorSpace
const skgpu::graphite::RenderPassProperties kRGBA16F_1_D_SRGB {
        skgpu::graphite::DepthStencilFlags::kDepth,
        kRGBA_F16_SkColorType,
        SkColorSpace::MakeSRGB(),
        /* fRequiresMSAA= */ false
};

// clang-format on

// =======================================
//            DrawTypeFlags
// =======================================
// NOTE: keep in sync with upstream external/skia/tests/graphite/precompile/PrecompileTestUtils.h
// clang-format off

constexpr skgpu::graphite::DrawTypeFlags kRRectAndNonAARect =
        static_cast<skgpu::graphite::DrawTypeFlags>(skgpu::graphite::DrawTypeFlags::kAnalyticRRect |
                                                    skgpu::graphite::DrawTypeFlags::kNonAAFillRect);
constexpr skgpu::graphite::DrawTypeFlags kQuadAndNonAARect =
        static_cast<skgpu::graphite::DrawTypeFlags>(skgpu::graphite::DrawTypeFlags::kPerEdgeAAQuad |
                                                    skgpu::graphite::DrawTypeFlags::kNonAAFillRect);

// clang-format on

void GraphitePipelineManager::PrecompilePipelines(
        std::unique_ptr<graphite::PrecompileContext> precompileContext,
        RuntimeEffectManager& effectManager) {
    // Easy references to SkRuntimeEffects for various LinearEffects that may be reused in multiple
    // precompilation scenarios.
    // clang-format off
    const auto kUNKNOWN__SRGB__false__UNKNOWN__Shader =
            effectManager.getOrCreateLinearRuntimeEffect({
                    .inputDataspace = ui::Dataspace::UNKNOWN, // Default
                    .outputDataspace = ui::Dataspace::SRGB,   // (deprecated) sRGB sRGB Full range
                    .undoPremultipliedAlpha = false,
                    .fakeOutputDataspace = ui::Dataspace::UNKNOWN, // Default
                    .type = shaders::LinearEffect::SkSLType::Shader,
            });

    const auto kBT2020_ITU_PQ__BT2020__false__UNKNOWN__Shader =
            effectManager.getOrCreateLinearRuntimeEffect({
                    .inputDataspace = ui::Dataspace::BT2020_ITU_PQ,     // BT2020 SMPTE 2084 Limited range
                    .outputDataspace = ui::Dataspace::BT2020, // BT2020 SMPTE_170M Full range
                    .undoPremultipliedAlpha = false,
                    .fakeOutputDataspace = ui::Dataspace::UNKNOWN, // Default
                    .type = shaders::LinearEffect::SkSLType::Shader,
            });

    const auto k0x188a0000__DISPLAY_P3__false__0x90a0000__Shader =
            effectManager.getOrCreateLinearRuntimeEffect({
                    .inputDataspace = static_cast<ui::Dataspace>(0x188a0000), // DCI-P3 sRGB Extended range
                    .outputDataspace = ui::Dataspace::DISPLAY_P3, // DCI-P3 sRGB Full range
                    .undoPremultipliedAlpha = false,
                    .fakeOutputDataspace = static_cast<ui::Dataspace>(0x90a0000), // DCI-P3 gamma 2.2 Full range
                    .type = shaders::LinearEffect::SkSLType::Shader,
            });
    // clang-format on

    // =======================================
    //            Combinations
    // =======================================
    // NOTE: keep in sync with upstream
    // external/skia/tests/graphite/precompile/AndroidPrecompileTest.cpp
    // clang-format off
    const PrecompileSettings precompileCases[] = {
/*  0 */ { ImagePremulHWOnlySrcover(),         DrawTypeFlags::kNonAAFillRect,   kRGBA16F_1_D },
/*  1 */ { ImagePremulHWOnlySrcover(),         kRRectAndNonAARect,              kRGBA_1_D },

/*  2 */ { ImagePremulHWOnlySrcover(),         kRRectAndNonAARect,              kRGBA_4_DS },

/*  3 */ { ImageHWOnlySRGBSrcover(),           DrawTypeFlags::kNonAAFillRect,   kRGBA16F_1_D_SRGB },
/*  4 */ { ImageHWOnlySRGBSrcover(),           kRRectAndNonAARect,              kRGBA_1_D_SRGB },

/*  5 */ { TransparentPaintImagePremulHWOnlySrcover(), kRRectAndNonAARect,      kRGBA_1_D },
/*  6 */ { TransparentPaintImagePremulHWOnlySrcover(), kRRectAndNonAARect,      kRGBA_4_DS },

/*  7 */ { TransparentPaintImageSRGBHWOnlySrcover(), kRRectAndNonAARect,        kRGBA_1_D_SRGB },

/*  8 */ { SolidSrcSrcover(),                  kRRectAndNonAARect,              kRGBA_1_D },

/*  9 */ { SolidSrcover(),                     kRRectAndNonAARect,              kRGBA_4_DS },

/* 10 */ { ImagePremulHWOnlyMatrixCFSrcover(), kRRectAndNonAARect,              kRGBA_1_D },

/* 11 */ { TransparentPaintImagePremulHWOnlyMatrixCFSrcover(),
                                               DrawTypeFlags::kAnalyticRRect,   kRGBA_1_D },

/* 12 */ { TransparentPaintImagePremulHWOnlyMatrixCFDitherSrcover(),
                                               kRRectAndNonAARect,              kRGBA_1_D },
/* 13 */ { TransparentPaintImagePremulHWOnlyMatrixCFDitherSrcover(),
                                               DrawTypeFlags::kNonAAFillRect,   kRGBA_4_DS },

/* 14 */ { ImagePremulHWOnlyMatrixCFDitherSrcover(), kRRectAndNonAARect,        kRGBA_1_D },

/* 15 */ { ImagePremulHWOnlyMatrixCFDitherSrcover(), DrawTypeFlags::kNonAAFillRect, kRGBA_4_DS },

/* 16 */ { TransparentPaintImageSRGBHWOnlyMatrixCFDitherSrcover(),
                                               kRRectAndNonAARect,              kRGBA_1_D_SRGB },

/* 17 */ { ImageSRGBHWOnlyMatrixCFDitherSrcover(), kRRectAndNonAARect,          kRGBA_1_D_SRGB },

/* 18 */ { ImageSRGBHWOnlyMatrixCFDitherSrcover(), DrawTypeFlags::kAnalyticRRect, kRGBA_4_DS_SRGB },

/* 19 */ { ImageAlphaPremulHWOnlyMatrixCFSrcover(), DrawTypeFlags::kNonAAFillRect, kRGBA_1_D },

/* 20 */ { ImageAlphaPremulHWOnlyMatrixCFSrcover(), DrawTypeFlags::kNonAAFillRect, kRGBA_4_DS },

/* 21 */ { ImageAlphaSRGBHWOnlyMatrixCFSrcover(), DrawTypeFlags::kNonAAFillRect,kRGBA_1_D_SRGB },

/* 22 */ { ImagePremulHWOnlySrc(),             DrawTypeFlags::kNonAAFillRect,   kRGBA_1_D },
/* 23 */ { ImagePremulHWOnlySrc(),             DrawTypeFlags::kPerEdgeAAQuad,   kRGBA_1_D },
/* 24 */ { ImagePremulHWOnlySrc(),             DrawTypeFlags::kNonAAFillRect,   kRGBA_4_DS },

/* 25 */ { MouriMapBlur(effectManager),                     DrawTypeFlags::kNonAAFillRect,   kRGBA16F_1_D },
/* 26 */ { MouriMapToneMap(effectManager),                  DrawTypeFlags::kNonAAFillRect,   kRGBA_1_D_SRGB },
/* 27 */ { MouriMapCrosstalkAndChunk16x16(effectManager),   DrawTypeFlags::kNonAAFillRect,   kRGBA16F_1_D_SRGB },
/* 28 */ { MouriMapChunk8x8Effect(effectManager),           DrawTypeFlags::kNonAAFillRect,   kRGBA16F_1_D },
/* 29 */ { KawaseBlurLowSrcSrcOver(effectManager),          DrawTypeFlags::kNonAAFillRect,   kRGBA_1_D },
/* 30 */ { KawaseBlurHighSrc(effectManager),                DrawTypeFlags::kNonAAFillRect,   kRGBA_1_D },
/* 31 */ { BlurFilterMix(effectManager),                    DrawTypeFlags::kNonAAFillRect,   kRGBA_1_D },

// These two are solid colors drawn w/ a LinearEffect

/* 32 */ { LinearEffect(kUNKNOWN__SRGB__false__UNKNOWN__Shader,
                        ChildType::kSolidColor,
                        SkBlendMode::kSrcOver),
           DrawTypeFlags::kNonAAFillRect,
           kRGBA16F_1_D_SRGB },
/* 33 */ { LinearEffect(kBT2020_ITU_PQ__BT2020__false__UNKNOWN__Shader,
                        ChildType::kSolidColor,
                        SkBlendMode::kSrc),
           DrawTypeFlags::kNonAAFillRect,
           kRGBA_1_D_SRGB },

/* 34 */ { LinearEffect(kUNKNOWN__SRGB__false__UNKNOWN__Shader,
                        ChildType::kHWTexture,
                        SkBlendMode::kSrcOver),
           DrawTypeFlags::kNonAAFillRect,
           kRGBA16F_1_D_SRGB },

// These next two only differ by dst surface

/* 35 */ { LinearEffect(k0x188a0000__DISPLAY_P3__false__0x90a0000__Shader,
                        ChildType::kHWTexture,
                        SkBlendMode::kSrcOver),
           DrawTypeFlags::kAnalyticRRect,
           kRGBA_1_D_SRGB },
/* 36 */ { LinearEffect(k0x188a0000__DISPLAY_P3__false__0x90a0000__Shader,
                        ChildType::kHWTexture,
                        SkBlendMode::kSrcOver),
           DrawTypeFlags::kAnalyticRRect,
           kRGBA_4_DS_SRGB },

// These next two are the same as the above two but w/ transparent paints

/* 37 */ { LinearEffect(k0x188a0000__DISPLAY_P3__false__0x90a0000__Shader,
                        ChildType::kHWTexture,
                        SkBlendMode::kSrcOver,
                        /* paintColorIsOpaque= */ false),
           DrawTypeFlags::kAnalyticRRect,
           kRGBA_1_D_SRGB },
// 50% (1/2) handlers 62 - due to the w/o msaa load variants not being used
/* 38 */ { LinearEffect(k0x188a0000__DISPLAY_P3__false__0x90a0000__Shader,
                        ChildType::kHWTexture,
                        SkBlendMode::kSrcOver,
                        /* paintColorIsOpaque= */ false),
           DrawTypeFlags::kAnalyticRRect,
           kRGBA_4_DS_SRGB },

// The next 3 have a RE_LinearEffect and a MatrixFilter along w/ different ancillary additions
/* 39 */ { LinearEffect(k0x188a0000__DISPLAY_P3__false__0x90a0000__Shader,
                        ChildType::kHWTexture,
                        SkBlendMode::kSrcOver,
                        /* paintColorIsOpaque= */ true,
                        /* matrixColorFilter= */ true),
           DrawTypeFlags::kAnalyticRRect,
           kRGBA_1_D_SRGB },
/* 40 */ { LinearEffect(k0x188a0000__DISPLAY_P3__false__0x90a0000__Shader,
                        ChildType::kHWTexture,
                        SkBlendMode::kSrcOver,
                        /* paintColorIsOpaque= */ false,
                        /* matrixColorFilter= */ true),
           DrawTypeFlags::kAnalyticRRect,
           kRGBA_1_D_SRGB },
/* 41 */ { LinearEffect(k0x188a0000__DISPLAY_P3__false__0x90a0000__Shader,
                        ChildType::kHWTexture,
                        SkBlendMode::kSrcOver,
                        /* paintColorIsOpaque= */ true,
                        /* matrixColorFilter= */ true,
                        /* dither= */ true),
           DrawTypeFlags::kAnalyticRRect,
           kRGBA_1_D_SRGB },

/* 42 */ { SolidSrcover(), DrawTypeFlags::kNonSimpleShape, kRGBA_4_DS },

// AnalyticClip block - all the PrecompileOptions here are just clones of earlier ones
// with an additional kAnalyticClip flag

/* 43 */ { ImagePremulHWOnlySrcover(),
           kRRectAndNonAARect | DrawTypeFlags::kAnalyticClip,
           kRGBA_1_D },

/* 44 */ { ImagePremulHWOnlySrcover(),
           DrawTypeFlags::kNonAAFillRect | DrawTypeFlags::kAnalyticClip,
           kRGBA_4_DS },

/* 45 */ { ImageHWOnlySRGBSrcover(),
           kRRectAndNonAARect | DrawTypeFlags::kAnalyticClip,
           kRGBA_1_D_SRGB },

/* 46 */ { TransparentPaintImagePremulHWOnlySrcover(),
           DrawTypeFlags::kNonAAFillRect | DrawTypeFlags::kAnalyticClip,
           kRGBA_1_D },

/* 47 */ { SolidSrcSrcover(),
           kRRectAndNonAARect | DrawTypeFlags::kAnalyticClip,
           kRGBA_1_D },

/* 48 */ { SolidSrcSrcover(),
           DrawTypeFlags::kNonAAFillRect | DrawTypeFlags::kAnalyticClip,
           kRGBA_4_DS },

/* 49 */ { ImagePremulHWOnlyMatrixCFDitherSrcover(),
           kRRectAndNonAARect | DrawTypeFlags::kAnalyticClip,
           kRGBA_1_D },

/* 50 */ { ImageSRGBHWOnlyMatrixCFDitherSrcover(),
           kRRectAndNonAARect | DrawTypeFlags::kAnalyticClip,
           kRGBA_1_D_SRGB },

//--------------------------------------------------

/* 51 */ { {}, // ignored
           DrawTypeFlags::kDropShadows,
           kRGBA_1_D },

/* 52 */ { {}, // ignored
           DrawTypeFlags::kDropShadows,
           kRGBA_4_DS },

// 238 block ----------------

/* 53 */ { ImagePremulYCbCr238Srcover(),       kRRectAndNonAARect,              kRGBA_1_D },

/* 54 */ { ImagePremulYCbCr238Srcover(),
           kRRectAndNonAARect | DrawTypeFlags::kAnalyticClip,
           kRGBA_1_D },

/* 55 */ { TransparentPaintImagePremulYCbCr238Srcover(), kRRectAndNonAARect,    kRGBA_1_D },
/* 56 */ { ImagePremulYCbCr238Srcover(),       kRRectAndNonAARect,              kRGBA_4_DS },

/* 57 */ { ImagePremulYCbCr238Srcover(),
           DrawTypeFlags::kNonAAFillRect | DrawTypeFlags::kAnalyticClip,
           kRGBA_4_DS },

// 240 block ----------------

/* 58 */ { ImagePremulYCbCr240Srcover(),       DrawTypeFlags::kNonAAFillRect,   kRGBA_1_D },
/* 59 */ { ImagePremulYCbCr240Srcover(),       DrawTypeFlags::kNonAAFillRect,   kRGBA_4_DS },
/* 50 */ { TransparentPaintImagePremulYCbCr240Srcover(), DrawTypeFlags::kNonAAFillRect,kRGBA_4_DS },

// 247 block ----------------

/* 61 */ { MouriMapCrosstalkAndChunk16x16YCbCr247(effectManager),
           DrawTypeFlags::kNonAAFillRect,
           kRGBA16F_1_D_SRGB },

// The next 2 have the same PaintOptions but different destination surfaces

/* 62 */ { LinearEffect(kBT2020_ITU_PQ__BT2020__false__UNKNOWN__Shader,
                        ChildType::kHWTextureYCbCr247,
                        SkBlendMode::kSrcOver,
                        /* paintColorIsOpaque= */ true,
                        /* matrixColorFilter= */ false,
                        /* dither= */ true),
           kRRectAndNonAARect,
           kRGBA_1_D_SRGB },

/* 63 */ { LinearEffect(kBT2020_ITU_PQ__BT2020__false__UNKNOWN__Shader,
                        ChildType::kHWTextureYCbCr247,
                        SkBlendMode::kSrcOver,
                        /* paintColorIsOpaque= */ true,
                        /* matrixColorFilter= */ false,
                        /* dither= */ true),
           DrawTypeFlags::kNonAAFillRect,
           kRGBA_4_DS_SRGB },

/* 64 */ { LinearEffect(kBT2020_ITU_PQ__BT2020__false__UNKNOWN__Shader,
                        ChildType::kHWTextureYCbCr247,
                        SkBlendMode::kSrcOver,
                        /* paintColorIsOpaque= */ true,
                        /* matrixColorFilter= */ false,
                        /* dither= */ true),
           DrawTypeFlags::kNonAAFillRect | DrawTypeFlags::kAnalyticClip,
           kRGBA_1_D_SRGB },
    };
    // clang-format on

    ALOGD("Pipeline precompilation started");

    for (size_t i = 0; i < std::size(precompileCases); i++) {
        const PrecompileSettings& settings = precompileCases[i];
        Precompile(precompileContext.get(), settings.fPaintOptions, settings.fDrawTypeFlags,
                   {&settings.fRenderPassProps, 1});
    }

    ALOGD("Pipeline precompilation finished");
}

} // namespace android::renderengine::skia