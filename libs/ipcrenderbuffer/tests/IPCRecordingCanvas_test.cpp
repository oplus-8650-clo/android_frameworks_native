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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

#include <gui/RenderCommandBuffer.h>
#include <gui/RenderCommandBufferConsumer.h>
#include <gui/RenderCommandBufferProducer.h>

#include <android/ipcrenderbuffer/IPCRecordingCanvas.h>
#include <android/ipcrenderbuffer/RenderBufferHelpers.h>

#include <gtest/gtest.h>

#include <SkCanvas.h>
#include <SkData.h>
#include <SkEncodedImageFormat.h>
#include <SkImage.h>
#include <SkPngEncoder.h>
#include <SkSurface.h>

namespace android {

#define CANVAS_TEST_SIZE 512

struct SoftwareSurfaceAndCanvas {
    SoftwareSurfaceAndCanvas(int width, int height) {
        SkImageInfo info = SkImageInfo::MakeN32Premul(width, height); // Example size
        const size_t minRowBytes = info.minRowBytes();
        const size_t size = info.computeMinByteSize();
        pixels = new SkPMColor[size];

        // Create SkSurface using SkSurfaces::WrapPixels
        surface = SkSurfaces::WrapPixels(info, pixels, minRowBytes);
        if (!surface) {
            ALOGE("Failed to create SkSurface with SkSurfaces::WrapPixels");
            delete[] pixels;
            canvas = nullptr;
            surface = nullptr;
        }
        canvas = surface->getCanvas(); // Get Canvas from the Surface
    }
    ~SoftwareSurfaceAndCanvas() { delete[] pixels; }
    sk_sp<SkSurface> surface;
    SkCanvas* canvas;
    SkPMColor* pixels;
};

bool compareData(const sk_sp<SkData>& a, const sk_sp<SkData>& b) {
    if (a->size() != b->size()) {
        return false;
    }
    return (memcmp(a->data(), b->data(), a->size()) == 0);
}

sk_sp<SkData> surfaceToPNGData(const sk_sp<SkSurface>& surface) {
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        ALOGE("Failed to create image snapshot");
        return nullptr;
    }

    SkPngEncoder::Options options;
    return SkPngEncoder::Encode(nullptr, image.get(), options);
}

bool compareSurfaces(const sk_sp<SkSurface>& a, const sk_sp<SkSurface>& b) {
    auto da = surfaceToPNGData(a);
    auto db = surfaceToPNGData(b);

    // TODO: Dump png when they differ.
    return compareData(da, db);
}

class IPCRecordingCanvasTest : public ::testing::Test {
    void SetUp() override {
        mDirectCanvas =
                std::make_shared<SoftwareSurfaceAndCanvas>(CANVAS_TEST_SIZE, CANVAS_TEST_SIZE);
        mIPCCanvasBackend =
                std::make_shared<SoftwareSurfaceAndCanvas>(CANVAS_TEST_SIZE, CANVAS_TEST_SIZE);
        mIPCRecordingCanvas = std::make_shared<IPCRecordingCanvas>();
        Parcel p;
        mIPCRecordingCanvas->getRenderCommandBufferProducer()->writeToParcel(&p);
        p.setDataPosition(0);
        mRenderCommandBufferConsumer = std::make_shared<RenderCommandBufferConsumer>();
        RenderCommandBufferConsumer::readFromParcel(p, mRenderCommandBufferConsumer.get());
    }
    void TearDown() override {
        mDirectCanvas = nullptr;
        mIPCCanvasBackend = nullptr;
        mIPCRecordingCanvas = nullptr;
        mRenderCommandBufferConsumer = nullptr;
    }

public:
    void renderWithIPCCanvas(const std::function<void(SkCanvas*)>& doDraw) {
        mIPCRecordingCanvas->startRecording();
        doDraw(mIPCRecordingCanvas.get());
        mIPCRecordingCanvas->endRecording();
        renderCommandBufferToCanvas(mRenderCommandBufferConsumer, mIPCCanvasBackend->canvas,
                                    [&](int) {});
    }

    bool compareRendering(const std::function<void(SkCanvas*)>& doDraw) {
        doDraw(mDirectCanvas->canvas);
        renderWithIPCCanvas(doDraw);
        return compareSurfaces(mDirectCanvas->surface, mIPCCanvasBackend->surface);
    }

    std::shared_ptr<SoftwareSurfaceAndCanvas> mDirectCanvas;
    std::shared_ptr<SoftwareSurfaceAndCanvas> mIPCCanvasBackend;
    std::shared_ptr<IPCRecordingCanvas> mIPCRecordingCanvas;
    std::shared_ptr<RenderCommandBufferConsumer> mRenderCommandBufferConsumer;
};

TEST_F(IPCRecordingCanvasTest, ClearToRed) {
    auto clearToRed = [&](SkCanvas* c) { c->clear(SK_ColorRED); };
    ASSERT_TRUE(compareRendering(clearToRed));
}

} // namespace android
