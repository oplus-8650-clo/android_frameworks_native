/*
 * Copyright 2020 The Android Open Source Project
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

#include <inttypes.h>
#include <iosfwd>
#include <iostream>

#include <math/HashCombine.h>

namespace android {

struct BlurRegion {
    uint32_t blurRadius;
    float cornerRadiusTLX;
    float cornerRadiusTLY;
    float cornerRadiusTRX;
    float cornerRadiusTRY;
    float cornerRadiusBLX;
    float cornerRadiusBLY;
    float cornerRadiusBRX;
    float cornerRadiusBRY;
    float alpha;
    int left;
    int top;
    int right;
    int bottom;

    inline bool operator==(const BlurRegion& other) const {
        return blurRadius == other.blurRadius && cornerRadiusTLX == other.cornerRadiusTLX &&
                cornerRadiusTLY == other.cornerRadiusTLY &&
                cornerRadiusTRX == other.cornerRadiusTRX &&
                cornerRadiusTRY == other.cornerRadiusTRY &&
                cornerRadiusBLX == other.cornerRadiusBLX &&
                cornerRadiusBLY == other.cornerRadiusBLY &&
                cornerRadiusBRX == other.cornerRadiusBRX &&
                cornerRadiusBRY == other.cornerRadiusBRY && alpha == other.alpha &&
                left == other.left && top == other.top && right == other.right &&
                bottom == other.bottom;
    }

    inline bool operator!=(const BlurRegion& other) const { return !(*this == other); }
};

static inline void PrintTo(const BlurRegion& blurRegion, ::std::ostream* os) {
    *os << "BlurRegion {";
    *os << "\n    .blurRadius = " << blurRegion.blurRadius;
    *os << "\n    .cornerRadiusTLX = " << blurRegion.cornerRadiusTLX;
    *os << "\n    .cornerRadiusTLY = " << blurRegion.cornerRadiusTLY;
    *os << "\n    .cornerRadiusTRX = " << blurRegion.cornerRadiusTRX;
    *os << "\n    .cornerRadiusTRY = " << blurRegion.cornerRadiusTRY;
    *os << "\n    .cornerRadiusBLX = " << blurRegion.cornerRadiusBLX;
    *os << "\n    .cornerRadiusBLY = " << blurRegion.cornerRadiusBLY;
    *os << "\n    .cornerRadiusBRX = " << blurRegion.cornerRadiusBRX;
    *os << "\n    .cornerRadiusBRY = " << blurRegion.cornerRadiusBRY;
    *os << "\n    .alpha = " << blurRegion.alpha;
    *os << "\n    .left = " << blurRegion.left;
    *os << "\n    .top = " << blurRegion.top;
    *os << "\n    .right = " << blurRegion.right;
    *os << "\n    .bottom = " << blurRegion.bottom;
    *os << "\n}";
}

// copied from skia/src/core/SkBlurMask.cpp
inline float convertBlurUserRadiusToSigma(float radius) {
    return radius > 0 ? 0.57735f * radius + 0.5f : 0.0f;
}
// copied from skia/src/core/SkBlurEngine.h
inline float convertBlurSigmaToKernelRadius(float sigma) {
    return sigma <= 0.03f ? 0 : ceilf(3.f * sigma);
}

} // namespace android

namespace std {
template <>
struct hash<android::BlurRegion> {
    size_t operator()(const android::BlurRegion& region) const {
        return android::hashCombine(region.blurRadius, region.cornerRadiusTLX,
                                    region.cornerRadiusTLY, region.cornerRadiusTRX,
                                    region.cornerRadiusTRY, region.cornerRadiusBLX,
                                    region.cornerRadiusBLY, region.cornerRadiusBRX,
                                    region.cornerRadiusBRY, region.alpha, region.left, region.top,
                                    region.right, region.bottom);
    }
};
} // namespace std