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

#include <gui/LocklessTripleBuffer.h>
#include <gui/RenderCommandBuffer.h>

#include <android-base/unique_fd.h>
#include <binder/Parcel.h>

#include <log/log.h>

namespace android {
class RenderCommandBufferProducer {
public:
    RenderCommandBufferProducer();
    virtual ~RenderCommandBufferProducer();

    int getFd();
    RenderCommandBuffer* acquire() {
        LOG_ALWAYS_FATAL_IF(mCurrentBuffer != nullptr, "Already acquired");
        mCurrentBuffer = mCommandBuffer->producerAcquire();
        mCurrentBuffer->reset();
        return mCurrentBuffer;
    }
    void release() {
        LOG_ALWAYS_FATAL_IF(mCurrentBuffer == nullptr, "Already released");

        mCurrentBuffer = nullptr;
        mCommandBuffer->producerRelease();
        mSharedRegionRenderCommands->mFrameNumber++;
    }

    void startRecording();
    void finishRecording();

    status_t writeToParcel(Parcel* parcel) const;
    uint64_t getFrameNumber() { return mSharedRegionRenderCommands->mFrameNumber; }

private:
    int mAshmemFdRenderCommands;
    IpcRenderRegion* mSharedRegionRenderCommands;
    LocklessTripleBuffer<RenderCommandBuffer>* mCommandBuffer;
    RenderCommandBuffer* mCurrentBuffer = nullptr;
};
} // namespace android
