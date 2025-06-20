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

// #define LOG_NDEBUG 0
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <android/data_space.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <gui/BufferItemConsumer.h>
#include <gui/Surface.h>
#include <hardware/gralloc.h>
#include <log/log_main.h>
#include <system/window.h>
#include <ui/DisplayId.h>
#include <ui/Fence.h>
#include <ui/GraphicBuffer.h>
#include <utils/Errors.h>
#include <utils/Trace.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "DisplayHardware/HWComposer.h"
#include "compositionengine/DisplaySurface.h"

#include "VirtualDisplaySurface2.h"

namespace android {

class VirtualDisplaySurface2::RenderConsumerListener
      : public BufferItemConsumer::FrameAvailableListener {
public:
    RenderConsumerListener(const sp<VirtualDisplaySurface2>& virtualDisplay)
          : mVirtualDisplay(virtualDisplay) {}

    // BufferItemConsumer::FrameAvailableListener
    virtual void onFrameAvailable(const BufferItem&) override {
        sp<VirtualDisplaySurface2> virtualDisplay = mVirtualDisplay.promote();
        if (virtualDisplay) {
            virtualDisplay->onRenderFrameAvailable();
        }
    }

private:
    wp<VirtualDisplaySurface2> mVirtualDisplay;
};

VirtualDisplaySurface2::VirtualDisplaySurface2(HWComposer& hwComposer,
                                               VirtualDisplayIdVariant displayId,
                                               const std::string& name,
                                               const sp<Surface>& sinkSurface)
      : mHWC(hwComposer), mDisplayId(displayId), mName(name), mSinkSurface(sinkSurface) {}

VirtualDisplaySurface2::~VirtualDisplaySurface2() {
    mSinkSurface->disconnect(NATIVE_WINDOW_API_CPU);
    mRendererConsumer->abandon();
    if (mOutputConsumer) {
        mOutputConsumer->abandon();
    }
    if (mOutputSurface) {
        mOutputSurface->disconnect(NATIVE_WINDOW_API_CPU);
    }
}

void VirtualDisplaySurface2::onFirstRef() {
    mSinkSurfaceListener = sp<StubSurfaceListener>::make();
    mSinkSurface->connect(NATIVE_WINDOW_API_CPU, mSinkSurfaceListener);
    mSinkName = mSinkSurface->getConsumerName();

    status_t ret = mSinkSurface->getConsumerUsage(&mSinkUsage);
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to get consumer usage from the sink surface. Status: %d", __func__, ret);
        mSinkUsage = 0;
    }

    mSinkFormat = ANativeWindow_getFormat(mSinkSurface.get());
    if (mSinkFormat < 0) {
        ALOGE("%s: Bad format returned from %s. Status: %d", __func__, mSinkName.c_str(),
              mSinkFormat);
        mSinkFormat = 0;
    }

    int32_t dataSpace = ANativeWindow_getBuffersDefaultDataSpace(mSinkSurface.get());
    if (dataSpace < 0) {
        ALOGE("%s: Bad dataSpace returned from %s. Status: %d", __func__, mSinkName.c_str(),
              dataSpace);
        dataSpace = 0;
    }
    mSinkDataSpace = static_cast<ADataSpace>(dataSpace);

    int32_t width = ANativeWindow_getWidth(mSinkSurface.get());
    if (width < 0) {
        ALOGE("%s: Bad width returned from %s. Status: %d", __func__, mSinkName.c_str(), width);
        width = 0;
    }
    mSinkWidth = (uint32_t)width;

    int32_t height = ANativeWindow_getHeight(mSinkSurface.get());
    if (height < 0) {
        ALOGE("%s: Bad height returned from %s. Status: %d", __func__, mSinkName.c_str(), height);
        height = 0;
    }
    mSinkHeight = (uint32_t)height;

    ret = mSinkSurface->setAsyncMode(true);
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to set async mode for %s. Status: %d", __func__, mSinkName.c_str(), ret);
    }

    // If we might be rendering to the HAL, this buffer could be used in HWComposer::setClientTarget
    // in Mixed mode.
    uint64_t rendererUsage = isHalDisplay() ? (GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_COMPOSER)
                                            : GRALLOC_USAGE_HW_RENDER;

    // Since the renderer can be used for GPU compositing at any point, make sure we are generating
    // buffers we can send over to the app.
    rendererUsage |= mSinkUsage;

    std::tie(mRendererConsumer, mRendererSurface) = BufferItemConsumer::create(rendererUsage);
    mRendererListener =
            sp<RenderConsumerListener>::make(sp<VirtualDisplaySurface2>::fromExisting(this));
    mRendererConsumer->setFrameAvailableListener(mRendererListener);
    mRendererConsumer->setName(String8(mName + "-RenderBQ"));

    if (isHalDisplay()) {
        std::tie(mOutputConsumer, mOutputSurface) =
                BufferItemConsumer::create(GRALLOC_USAGE_HW_COMPOSER | mSinkUsage);
        mOutputConsumer->setDefaultBufferFormat(mSinkFormat);
        mOutputConsumer->setDefaultBufferSize(mSinkWidth, mSinkHeight);
        mOutputConsumer->setDefaultBufferDataSpace(static_cast<android_dataspace>(mSinkDataSpace));
        mOutputConsumer->setName(String8(mName + "-OutputBQ"));

        mOutputSurfaceListener = sp<StubSurfaceListener>::make();
        ret = mOutputSurface->connect(NATIVE_WINDOW_API_CPU, mOutputSurfaceListener);
        if (ret != NO_ERROR) {
            ALOGE("%s: Unable to set up output surface listener. Status: %d", __func__, ret);
        }
    }
}

sp<Surface> VirtualDisplaySurface2::getCompositionSurface() const {
    return mRendererSurface;
}

status_t VirtualDisplaySurface2::beginFrame(bool mustRecompose) {
    ATRACE_CALL();

    std::scoped_lock _l(mMutex);

    if (mPendingResize) {
        applyResizeLocked(*mPendingResize);
        mPendingResize.reset();
    }

    ALOGE_IF(mCurrentFrame != std::nullopt,
             "%s: called while another frame is being processed. Overwriting.", __func__);

    mCurrentFrame.emplace(FrameInfo());
    mCurrentFrame->mustRecompose = mustRecompose;

    return OK;
}

status_t VirtualDisplaySurface2::prepareFrame(CompositionType compositionType) {
    ATRACE_CALL();

    if (isGpuDisplay() &&
        (compositionType == CompositionType::Hwc || compositionType == CompositionType::Mixed)) {
        ALOGE("%s: called with bad composition type %d on GPU display.", __func__, compositionType);
        return BAD_VALUE;
    }

    std::scoped_lock _l(mMutex);

    if (!mCurrentFrame) {
        ALOGE("%s: called without a current frame.", __func__);
        return INVALID_OPERATION;
    }
    FrameInfo& frameInfo = *mCurrentFrame;
    frameInfo.compositionType = compositionType;

    return OK;
}

status_t VirtualDisplaySurface2::advanceFrame(float hdrSdrRatio) {
    ATRACE_CALL();

    std::scoped_lock _l(mMutex);

    if (!mCurrentFrame) {
        ALOGE("advanceFrame called without a current frame.");
        return INVALID_OPERATION;
    }
    FrameInfo& frameInfo = *mCurrentFrame;

    if (isGpuDisplay() || frameInfo.compositionType == CompositionType::Gpu) {
        ALOGD("%s: No work to be done for GPU composition.", __func__);
        return OK;
    }

    auto halDisplayId = std::get<HalVirtualDisplayId>(mDisplayId);

    sp<GraphicBuffer> buffer;
    sp<Fence> fence;
    mOutputSurface->dequeueBuffer(&buffer, &fence);
    status_t status = mHWC.setOutputBuffer(halDisplayId, fence, buffer);
    if (status != NO_ERROR) {
        ALOGE("%s: Failed to set output buffer. Status: %d", __func__, status);
        return status;
    }
    frameInfo.outputBuffer = buffer;
    frameInfo.outputFence = fence;

    if (frameInfo.compositionType == CompositionType::Mixed) {
        ui::Dataspace dataspace = ui::Dataspace::SRGB; // TODO
        sp<GraphicBuffer>& buffer = frameInfo.clientComposedBufferItem.mGraphicBuffer;
        sp<Fence>& fence = frameInfo.clientComposedBufferItem.mFence;

        // TODO: use an LRU to track slots instead of constantly overwriting them.
        uint32_t slot = 0;
        bool shouldSendBuffer = true;
        status =
                mHWC.setClientTarget(halDisplayId, slot, fence,
                                     (shouldSendBuffer ? buffer : nullptr), dataspace, hdrSdrRatio);
        if (status != NO_ERROR) {
            ALOGE("%s: Failed to set client target buffer. Status: %d", __func__, status);
            return status;
        }
    }

    return OK;
}

void VirtualDisplaySurface2::onFrameCommitted() {
    ATRACE_CALL();

    std::scoped_lock _l(mMutex);

    if (!mCurrentFrame) {
        ALOGE("onFrameCommitted called without a current frame.");
        return;
    }

    FrameInfo frameInfo = std::move(mCurrentFrame).value();
    mCurrentFrame.reset();

    // GPU composition is done in onRenderFrameAvailable()
    if (isGpuDisplay() || frameInfo.compositionType == CompositionType::Gpu) {
        return;
    }

    auto halDisplayId = std::get<HalVirtualDisplayId>(mDisplayId);
    sp<Fence> presentFence = mHWC.getPresentFence(halDisplayId);

    status_t ret = mOutputSurface->queueBuffer(frameInfo.outputBuffer,
                                               Fence::merge("VD Acquire/Present",
                                                            frameInfo.outputFence, presentFence));
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to queue output buffer. Status: %d", __func__, ret);
        return;
    }

    BufferItem item;
    ret = mOutputConsumer->acquireBuffer(&item, /*presentWhen*/ -1);
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to acquire output buffer. Status: %d", __func__, ret);
        return;
    }

    ret = mOutputConsumer->detachBuffer(item.mGraphicBuffer);
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to detach output buffer. Status: %d", __func__, ret);
        return;
    }

    ret = mSinkSurface->attachBuffer(item.mGraphicBuffer->getNativeBuffer());
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to attach buffer to sink. Status: %d", __func__, ret);
        return;
    }

    ret = mSinkSurface->queueBuffer(item.mGraphicBuffer, item.mFence);
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to queue buffer to sink. Status: %d", __func__, ret);
    }
}

void VirtualDisplaySurface2::dumpAsString(String8& result) const {
    std::scoped_lock _l(mMutex);

    std::string displayIdStr = std::visit([](auto&& arg) { return to_string(arg); }, mDisplayId);
    std::string type = isGpuDisplay() ? "GPU" : "HWC";

    result.append("    VirtualDisplaySurface2\n");
    result.appendFormat("        type=%s\n", type.c_str());
    result.appendFormat("        mName=%s\n", mName.c_str());
    result.appendFormat("        mDisplayId=%s\n", displayIdStr.c_str());
    result.appendFormat("        mSinkName=%s\n", mSinkName.c_str());
    result.appendFormat("        mSinkFormat=%d\n", mSinkFormat);
    result.appendFormat("        mSinkUsage=%" PRIu64 "\n", mSinkUsage);
    result.appendFormat("        mSinkDataSpace=%d\n", mSinkDataSpace);
    result.appendFormat("        mSinkWidth=%d\n", mSinkWidth);
    result.appendFormat("        mSinkHeight=%d\n", mSinkHeight);
}

void VirtualDisplaySurface2::resizeBuffers(const ui::Size& newSize) {
    ATRACE_CALL();

    std::scoped_lock _l(mMutex);
    if (mCurrentFrame) {
        mPendingResize = {newSize};
    } else {
        applyResizeLocked(newSize);
    }
}

void VirtualDisplaySurface2::applyResizeLocked(const ui::Size& newSize) {
    if (newSize.width < 0 || newSize.height < 0) {
        ALOGE("%s: Called with invalid size %dx%d", __func__, newSize.width, newSize.height);
        return;
    }
    if ((uint32_t)newSize.width == mSinkWidth && (uint32_t)newSize.height == mSinkHeight) {
        return;
    }

    mSinkWidth = (uint32_t)newSize.width;
    mSinkHeight = (uint32_t)newSize.height;

    status_t ret = mSinkSurface->setBuffersDimensions(mSinkWidth, mSinkHeight);
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to set sink buffer size %dx%d", __func__, mSinkWidth, mSinkHeight);
    }

    // TODO: should these fail harder? Brick the Virtual Display?
    ret = mOutputConsumer->setDefaultBufferSize(mSinkWidth, mSinkHeight);
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to set output consumer default buffer size %dx%d", __func__, mSinkWidth,
              mSinkHeight);
    }

    ret = mOutputSurface->setBuffersDimensions(mSinkWidth, mSinkHeight);
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to set output surface buffer size %dx%d", __func__, mSinkWidth,
              mSinkHeight);
    }

    ret = mRendererConsumer->setDefaultBufferSize(mSinkWidth, mSinkHeight);
    if (ret != NO_ERROR) {
        ALOGE("%s: Unable to set render default buffer size %dx%d", __func__, mSinkWidth,
              mSinkHeight);
    }
}

const sp<Fence>& VirtualDisplaySurface2::getClientTargetAcquireFence() const {
    std::scoped_lock _l(mMutex);

    if (!mCurrentFrame) {
        return Fence::NO_FENCE;
    }

    return mCurrentFrame->clientComposedBufferItem.mFence
            ? mCurrentFrame->clientComposedBufferItem.mFence
            : Fence::NO_FENCE;
}

void VirtualDisplaySurface2::onRenderFrameAvailable() {
    ATRACE_CALL();

    std::scoped_lock _l(mMutex);

    if (!mCurrentFrame) {
        ALOGE("Notified that render frame is available without a pending frame.");
        return;
    }
    FrameInfo& frameInfo = *mCurrentFrame;

    BufferItem item;
    status_t ret = mRendererConsumer->acquireBuffer(&item,
                                                    /*presentWhen*/ -1,
                                                    /*waitForFence*/ false);
    if (ret != NO_ERROR) {
        ALOGE("%s: Failed to acquire the buffer queued to the render surface. Status: %d", __func__,
              ret);
        return;
    }

    ret = mRendererConsumer->detachBuffer(item.mGraphicBuffer);
    if (ret != NO_ERROR) {
        ALOGE("%s: Failed to detach the buffer queued to the render surface. Status: %d", __func__,
              ret);
        return;
    }

    if (frameInfo.compositionType != CompositionType::Gpu) {
        ALOGI("%s: HWC composition, storing buffer %" PRIu64 " for later", __func__,
              item.mGraphicBuffer->getId());
        frameInfo.clientComposedBufferItem = std::move(item);
        return;
    }

    sp<GraphicBuffer> buffer = item.mGraphicBuffer;
    sp<Fence> fence = item.mFence;

    ret = mSinkSurface->attachBuffer(buffer->getNativeBuffer());
    if (ret != NO_ERROR) {
        ALOGE("%s: Failed to attach buffer to sink. Status: %d", __func__, ret);
        return;
    }

    ret = mSinkSurface->queueBuffer(buffer, fence);
    if (ret != NO_ERROR) {
        ALOGE("%s: Failed to queue buffer to sink. Status: %d", __func__, ret);
    }
}

} // namespace android
