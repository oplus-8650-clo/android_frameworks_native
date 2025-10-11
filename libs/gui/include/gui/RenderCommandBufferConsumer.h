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

#pragma once

#include <android-base/unique_fd.h>

#include <gui/LocklessTripleBuffer.h>
#include <gui/RenderCommandBuffer.h>

#include <binder/Parcel.h>
#include <binder/Parcelable.h>

namespace android {
class RenderCommandBufferConsumer {
public:
    RenderCommandBufferConsumer();
    virtual ~RenderCommandBufferConsumer();

    RenderCommandBuffer* consumerAcquire();
    void consumerRelease();

    void adoptFdCommandBuffer(int fd);

    static status_t readFromParcel(const Parcel& parcel, RenderCommandBufferConsumer* consumer);

    // Avoid linking skia to libgui, so awkward void types
    void setContext(void* context, std::function<void(void*)> contextFreeCallback);
    void* getContext() { return mContext; }

    // Used for replay
    void setRenderCommandBuffer(RenderCommandBuffer* commandBuffer) {
        mCommandBuffer = nullptr;
        mCurrentBuffer = commandBuffer;
    }

private:
    int mFdCommandBuffer = -1;
    LocklessTripleBuffer<RenderCommandBuffer>* mCommandBuffer;
    RenderCommandBuffer* mCurrentBuffer = nullptr;

    std::function<void(void*)> mContextFreeCallback;
    void* mContext = nullptr;
};

} // namespace android
