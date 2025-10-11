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


#include <android/ipcrenderbuffer/RenderBufferShmemPaint.h>

#include <SkBlendMode.h>
#include <SkColorFilter.h>
#include <SkColorSpace.h>

namespace android {
ShmemPaint toShmemPaint(const SkPaint& paint) {
    auto color = paint.getColor();
    SkColor4f color4f = SkColor4f::FromColor(color);

    auto filter = paint.getColorFilter();
    if (filter != nullptr) {
        auto colorspace = SkColorSpace::MakeSRGB();
        color4f = filter->filterColor4f(color4f, colorspace.get(), colorspace.get());
    }
    std::optional<SkBlendMode> blendMode = paint.asBlendMode();
    if (!blendMode.has_value()) {
        blendMode = SkBlendMode::kSrcOver;
    }

    return ShmemPaint{color4f, paint.getStyle(), *blendMode};
}

SkPaint fromShmemPaint(const ShmemPaint& paint) {
    SkPaint p = SkPaint(paint.color);
    p.setStyle(paint.style);
    p.setBlendMode(paint.blendMode);
    return p;
}
} // namespace android
