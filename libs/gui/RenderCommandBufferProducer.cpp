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

#include <cutils/ashmem.h>
#include <cutils/log.h>
#include <gui/RenderCommandBufferProducer.h>

#include <sys/mman.h>

#include <private/gui/ParcelUtils.h>

#include <cstring>
#include <string>

namespace android {

bool createSharedMemoryRegion(int* fd, void** sharedMemory, size_t size) {
    // ALOGE("Initializing render command buffer producer shared memory region");
    *fd = ashmem_create_region("TODO(racarr): Add names", size);
    if (*fd < 0) {
        ALOGE("Failed to create shared memory region for RenderCommandBufferProducer");
        return false;
    }
    if (ashmem_set_prot_region(*fd, PROT_READ | PROT_WRITE) < 0) {
        ALOGE("RenderCommandBufferProducer: Failed ashmem_set_prot_region");
        return false;
    }
    *sharedMemory = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    if (*sharedMemory == MAP_FAILED) {
        ALOGE("RenderCommandBufferProducer: Failed mmap");
        return false;
    }

    return true;
}

RenderCommandBufferProducer::RenderCommandBufferProducer() {
    if (!createSharedMemoryRegion(&mAshmemFdRenderCommands, &mSharedRegionRenderCommands,
                                  sizeof(LocklessTripleBuffer<RenderCommandBuffer>))) {
        LOG_ALWAYS_FATAL("Failed to create shared memory region for RenderCommandBufferProducer");
    }
    mCommandBuffer = new (mSharedRegionRenderCommands) LocklessTripleBuffer<RenderCommandBuffer>();

    // mHardwareBufferCacheToken = new BBinder();
}

RenderCommandBufferProducer::~RenderCommandBufferProducer() {
    ::munmap(mSharedRegionRenderCommands, sizeof(LocklessTripleBuffer<RenderCommandBuffer>));
    close(mAshmemFdRenderCommands);
}

int RenderCommandBufferProducer::getFd() {
    return mAshmemFdRenderCommands;
}

void RenderCommandBufferProducer::startRecording() {
    LOG_ALWAYS_FATAL_IF(mCurrentBuffer != nullptr, "Unbalanced calls to start/finish recording");
    mCurrentBuffer = mCommandBuffer->producerAcquire();
    mCurrentBuffer->reset();
}
void RenderCommandBufferProducer::finishRecording() {
    LOG_ALWAYS_FATAL_IF(mCurrentBuffer == nullptr, "Unbalanced calls to start/finish recording");
    mCommandBuffer->producerRelease();
    mCurrentBuffer = nullptr;
}

status_t RenderCommandBufferProducer::writeToParcel(Parcel* parcel) const {
    SAFE_PARCEL(parcel->writeDupFileDescriptor, mAshmemFdRenderCommands);
    return NO_ERROR;
}

} // namespace android
