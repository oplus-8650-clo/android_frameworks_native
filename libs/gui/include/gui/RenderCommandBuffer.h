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

#include <gui/LocklessStaticQueue.h>
#include <log/log.h>
#include <atomic>
#include <string>
#include "RPointer.h"

constexpr int RENDER_COMMAND_BUFFER_DEFAULT_SIZE = 1024 * 1024;

constexpr bool RENDER_COMMAND_BUFFER_VERBOSE = false;

namespace android {

struct IPCRenderBufferOp {
    RPointer<IPCRenderBufferOp> next;
    uint32_t type;
};

class RenderCommandBuffer {
public:
    RenderCommandBuffer() { memset(mBytes, 0, sizeof(mBytes)); }
    ~RenderCommandBuffer() {}

    IPCRenderBufferOp* getOps() { return mHead.get(); }

    // op must be allocated by this RenderCommandBuffer
    void pushOp(const IPCRenderBufferOp* op);

    void reset();

    template <typename T>
    T* allocAligned(size_t count = 1) {
        size_t aligned = roundUp(mUsed, alignof(T));
        size_t allocationSize = count * sizeof(T);
        if (aligned + allocationSize > sizeof(mBytes)) {
            return nullptr;
        }
        uint8_t* ptr = mBytes + aligned;
        mUsed = aligned + allocationSize;
        return reinterpret_cast<T*>(ptr);
    }

    bool dumpToFile(const char* filename) const;
    static RenderCommandBuffer* loadFromFile(const char* filename);

    void setFrameSize(int width, int height) {
        mWidth = width;
        mHeight = height;
    }

    void getFrameSize(int& width, int& height) {
        width = mWidth;
        height = mHeight;
    }

    uint8_t mBytes[RENDER_COMMAND_BUFFER_DEFAULT_SIZE];
    size_t mUsed = 0;

    static size_t roundUp(size_t n, size_t m) { return ((n + m - 1) / m) * m; }
    RPointer<IPCRenderBufferOp> mTail;
    RPointer<IPCRenderBufferOp> mHead;
    // These are somewhat awkward, and used to achieve compatibility with the buffer based geometry
    // calculations. Effectively this is the size that the buffer would have been in the normal
    // rendering mode.
    int mWidth = 0;
    int mHeight = 0;
};

template <typename T>
inline bool SetRSpan(RSpan<T>& span, RenderCommandBuffer* commandBuffer, const T* data,
                     size_t count) {
    span.data = commandBuffer->allocAligned<T>(count);
    if (!span.data) {
        return false;
    }
    span.size = count;
    if (data) {
        memcpy(span.data.get(), data, count * sizeof(T));
    }
    return true;
}

struct IpcRenderRegion {
    LocklessStaticQueue<RenderCommandBuffer, 8> mCommandBuffers;
    std::atomic<uint64_t> mFrameNumber;
};

} // namespace android
