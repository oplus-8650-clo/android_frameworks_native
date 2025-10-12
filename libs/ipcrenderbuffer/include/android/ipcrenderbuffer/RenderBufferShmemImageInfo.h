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

namespace android {

struct ShmemImageInfo {
    int width;
    int height;
    SkColorType colorType;
    SkAlphaType alphaType;
};

ShmemImageInfo toShmemImageInfo(const SkImageInfo& info);
SkImageInfo fromShmemImageInfo(const ShmemImageInfo& info);

// TODO: This is used with the BitmapArenaAllocator system from the prototype and
// may not be needed in an immutable bitmap only implementation. We keep it around for now though
// until the new bitmap implementation is finished.
struct OffsetToImageCache {
    std::map<int, sk_sp<SkImage>> images;
};

} // namespace android
