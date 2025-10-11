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
 */

#include <cutils/ashmem.h>
#include <cutils/log.h>
#include <gui/RenderCommandBufferConsumer.h>
#include <sys/mman.h>

#include <private/gui/ParcelUtils.h>
#include "utils/Errors.h"

namespace android {

RenderCommandBufferConsumer::RenderCommandBufferConsumer() {}

RenderCommandBufferConsumer::~RenderCommandBufferConsumer() {
    munmap(mCommandBuffer, sizeof(LocklessTripleBuffer<RenderCommandBuffer>));
    if (mFdCommandBuffer != -1) {
        close(mFdCommandBuffer);
    }
    if (mContext != nullptr) {
        mContextFreeCallback(mContext);
    }
}

void RenderCommandBufferConsumer::adoptFdCommandBuffer(int fd) {
    mFdCommandBuffer = fd;
    void* region = mmap(nullptr, sizeof(LocklessTripleBuffer<RenderCommandBuffer>),
                        PROT_READ | PROT_WRITE, MAP_SHARED, mFdCommandBuffer, 0);
    mCommandBuffer = (LocklessTripleBuffer<RenderCommandBuffer>*)region;
}

RenderCommandBuffer* RenderCommandBufferConsumer::consumerAcquire() {
    // LOG_ALWAYS_FATAL_IF(mCurrentBuffer != nullptr, "Unbalanced calls to
    // consumerAcquire/consumerRelease");
    if (mCommandBuffer == nullptr) {
        return mCurrentBuffer; // For replay tool.
    }
    mCurrentBuffer = mCommandBuffer->consume();
    return mCurrentBuffer;
}

void RenderCommandBufferConsumer::consumerRelease() {
    // LOG_ALWAYS_FATAL_IF(mCurrentBuffer == nullptr, "Unbalanced calls to
    // consumerAcquire/consumerRelease"); mCommandBuffer->consumerRelease();
    mCurrentBuffer = nullptr;
}

status_t RenderCommandBufferConsumer::readFromParcel(const Parcel& parcel,
                                                     RenderCommandBufferConsumer* consumer) {
    int fd = -1;
    fd = parcel.readFileDescriptor();
    LOG_ALWAYS_FATAL_IF(fd < 0, "Failed to read file descriptor from parcel");
    consumer->adoptFdCommandBuffer(dup(fd));
    return NO_ERROR;
}

void RenderCommandBufferConsumer::setContext(void* context,
                                             std::function<void(void*)> contextFreeCallback) {
    mContextFreeCallback = contextFreeCallback;
    mContext = context;
}

} // namespace android
