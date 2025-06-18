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

#include "hardware/gralloc.h"
#undef LOG_TAG
#define LOG_TAG "LibSurfaceFlingerUnittests"

#include <DisplayHardware/HWComposer.h>
#include <compositionengine/DisplaySurface.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <gui/BufferItem.h>
#include <gui/BufferItemConsumer.h>
#include <gui/Surface.h>
#include <log/log_main.h>
#include <nativebase/nativebase.h>
#include <system/window.h>
#include <ui/DisplayId.h>
#include <ui/GraphicBuffer.h>
#include <utils/Errors.h>

#include <condition_variable>
#include <mutex>
#include <variant>

#include "mock/DisplayHardware/MockHWComposer.h"

#include "DisplayHardware/VirtualDisplay/VirtualDisplaySurface2.h"

using namespace std::literals::chrono_literals;
using namespace testing;

namespace android {

constexpr size_t kNumFramesForTest = 100;

class HoldingFrameAvailableListener : public BufferItemConsumer::FrameAvailableListener {
public:
    HoldingFrameAvailableListener(const sp<BufferItemConsumer>& consumer) : mConsumer(consumer) {}

    virtual void onFrameAvailable(const BufferItem&) override {
        std::scoped_lock _l(mMutex);

        BufferItem& item = mItems.emplace_back();
        mConsumer->acquireBuffer(&item, 0);
        mConsumer->detachBuffer(item.mGraphicBuffer);
        mCondVar.notify_all();
    }

    std::vector<BufferItem> stealFrames() {
        auto lock = std::unique_lock(mMutex);

        if (mItems.empty()) {
            LOG_ALWAYS_FATAL_IF(std::cv_status::timeout == mCondVar.wait_for(lock, 10s),
                                "It took too long to get a frame. This likely means a frame was "
                                "never queued.");
        }

        std::vector<BufferItem> out;
        mItems.swap(out);
        return out;
    }

private:
    std::mutex mMutex;
    std::condition_variable mCondVar;
    sp<BufferItemConsumer> mConsumer;
    std::vector<BufferItem> mItems;
};

class VirtualDisplaySurface2Test : public Test {
public:
    void SetUp() override {
        std::tie(mSinkConsumer, mSinkSurface) =
                BufferItemConsumer::create(GraphicBuffer::USAGE_SW_READ_OFTEN);
        mSinkConsumer->setName(String8("TestBQ-SINK"));
    }

    void SetUpForGpu() {
        ASSERT_TRUE(std::holds_alternative<GpuVirtualDisplayId>(mDisplayId))
                << "mDisplayId should be member-initialized to GPU";
        mVirtualDisplay = sp<VirtualDisplaySurface2>::make(mHwc, mDisplayId,
                                                           "GpuTestVirtualDisplay", mSinkSurface);

        mRenderSurface = mVirtualDisplay->getCompositionSurface();
        mRenderSurfaceListener = sp<StubSurfaceListener>::make();
        ASSERT_EQ(NO_ERROR, mRenderSurface->connect(NATIVE_WINDOW_API_CPU, mRenderSurfaceListener));
    }

    void SetUpForHwc() {
        mDisplayId = HalVirtualDisplayId(55555);
        mVirtualDisplay = sp<VirtualDisplaySurface2>::make(mHwc, mDisplayId,
                                                           "HwcTestVirtualDisplay", mSinkSurface);

        mRenderSurface = mVirtualDisplay->getCompositionSurface();
        mRenderSurfaceListener = sp<StubSurfaceListener>::make();
        ASSERT_EQ(NO_ERROR, mRenderSurface->connect(NATIVE_WINDOW_API_CPU, mRenderSurfaceListener));
    }

    void DoFakeGpuRender() {
        // This should mimic, roughly, what's done in RenderSurface::dequeueBuffer and
        // RenderSurface::queueBuffer.
        ANativeWindow* anw = mRenderSurface.get();

        ANativeWindowBuffer* buffer;
        int fence;
        ASSERT_EQ(NO_ERROR, anw->dequeueBuffer(anw, &buffer, &fence));
        ASSERT_EQ(NO_ERROR, anw->queueBuffer(anw, buffer, fence));
    }

protected:
    testing::StrictMock<mock::HWComposer> mHwc;

    sp<BufferItemConsumer> mSinkConsumer;
    sp<Surface> mSinkSurface;

    VirtualDisplayIdVariant mDisplayId = GpuVirtualDisplayId(11111);
    sp<VirtualDisplaySurface2> mVirtualDisplay;
    sp<Surface> mRenderSurface;
    sp<SurfaceListener> mRenderSurfaceListener;
};

TEST_F(VirtualDisplaySurface2Test, GpuComposition) {
    SetUpForGpu();

    sp<HoldingFrameAvailableListener> sinkListener =
            sp<HoldingFrameAvailableListener>::make(mSinkConsumer);
    mSinkConsumer->setFrameAvailableListener(sinkListener);

    for (size_t i = 0; i < kNumFramesForTest; ++i) {
        EXPECT_EQ(OK, mVirtualDisplay->beginFrame(/*mustRecompose*/ true));
        EXPECT_EQ(OK, mVirtualDisplay->prepareFrame(VirtualDisplaySurface2::CompositionType::Gpu));

        DoFakeGpuRender();

        EXPECT_EQ(OK, mVirtualDisplay->advanceFrame(1.0f));
        mVirtualDisplay->onFrameCommitted();

        std::vector<BufferItem> frames = sinkListener->stealFrames();
        EXPECT_EQ(1u, frames.size());
    }
}

TEST_F(VirtualDisplaySurface2Test, HwcComposition) {
    SetUpForHwc();

    HalVirtualDisplayId virtualDisplayId = std::get<HalVirtualDisplayId>(mDisplayId);
    HalDisplayId halDisplayId = virtualDisplayId;

    EXPECT_CALL(mHwc, getPresentFence(halDisplayId))
            .Times(kNumFramesForTest)
            .WillRepeatedly(Return(Fence::NO_FENCE));
    EXPECT_CALL(mHwc, setOutputBuffer(virtualDisplayId, _, _))
            .Times(kNumFramesForTest)
            .WillRepeatedly(Return(OK));

    for (size_t i = 0; i < kNumFramesForTest; ++i) {
        sp<HoldingFrameAvailableListener> sinkListener =
                sp<HoldingFrameAvailableListener>::make(mSinkConsumer);
        mSinkConsumer->setFrameAvailableListener(sinkListener);

        EXPECT_EQ(OK, mVirtualDisplay->beginFrame(/*mustRecompose*/ true));
        EXPECT_EQ(OK, mVirtualDisplay->prepareFrame(VirtualDisplaySurface2::CompositionType::Hwc));

        DoFakeGpuRender();

        EXPECT_EQ(OK, mVirtualDisplay->advanceFrame(1.0f));
        mVirtualDisplay->onFrameCommitted();

        std::vector<BufferItem> frames = sinkListener->stealFrames();
        EXPECT_EQ(1u, frames.size());
    }
}

TEST_F(VirtualDisplaySurface2Test, MixedComposition) {
    SetUpForHwc();

    HalVirtualDisplayId virtualDisplayId = std::get<HalVirtualDisplayId>(mDisplayId);
    HalDisplayId halDisplayId = virtualDisplayId;

    EXPECT_CALL(mHwc, getPresentFence(halDisplayId))
            .Times(kNumFramesForTest)
            .WillRepeatedly(Return(Fence::NO_FENCE));
    EXPECT_CALL(mHwc, setOutputBuffer(virtualDisplayId, _, _))
            .Times(kNumFramesForTest)
            .WillRepeatedly(Return(OK));
    EXPECT_CALL(mHwc, setClientTarget(halDisplayId, _, _, _, _, _))
            .Times(kNumFramesForTest)
            .WillRepeatedly(Return(OK));

    sp<HoldingFrameAvailableListener> sinkListener =
            sp<HoldingFrameAvailableListener>::make(mSinkConsumer);
    mSinkConsumer->setFrameAvailableListener(sinkListener);

    for (size_t i = 0; i < kNumFramesForTest; ++i) {
        EXPECT_EQ(OK, mVirtualDisplay->beginFrame(/*mustRecompose*/ true));
        EXPECT_EQ(OK,
                  mVirtualDisplay->prepareFrame(VirtualDisplaySurface2::CompositionType::Mixed));

        DoFakeGpuRender();

        EXPECT_EQ(OK, mVirtualDisplay->advanceFrame(1.0f));
        mVirtualDisplay->onFrameCommitted();

        std::vector<BufferItem> frames = sinkListener->stealFrames();
        EXPECT_EQ(1u, frames.size());
    }
}

TEST_F(VirtualDisplaySurface2Test, Hwc_BufferDetails) {
    SetUpForHwc();

    HalVirtualDisplayId virtualDisplayId = std::get<HalVirtualDisplayId>(mDisplayId);
    HalDisplayId halDisplayId = virtualDisplayId;

    EXPECT_CALL(mHwc, getPresentFence(halDisplayId)).WillOnce(Return(Fence::NO_FENCE));
    EXPECT_CALL(mHwc, setOutputBuffer(virtualDisplayId, _, _)).WillOnce(Return(OK));

    sp<HoldingFrameAvailableListener> sinkListener =
            sp<HoldingFrameAvailableListener>::make(mSinkConsumer);
    mSinkConsumer->setFrameAvailableListener(sinkListener);

    EXPECT_EQ(OK, mVirtualDisplay->beginFrame(/*mustRecompose*/ true));
    EXPECT_EQ(OK, mVirtualDisplay->prepareFrame(VirtualDisplaySurface2::CompositionType::Hwc));

    DoFakeGpuRender();

    EXPECT_EQ(OK, mVirtualDisplay->advanceFrame(1.0f));
    mVirtualDisplay->onFrameCommitted();

    std::vector<BufferItem> frames = sinkListener->stealFrames();
    EXPECT_EQ(1u, frames.size());

    sp<GraphicBuffer>& buffer = frames[0].mGraphicBuffer;

    EXPECT_TRUE(buffer->usage & (GRALLOC_USAGE_HW_COMPOSER));
}

} // namespace android
