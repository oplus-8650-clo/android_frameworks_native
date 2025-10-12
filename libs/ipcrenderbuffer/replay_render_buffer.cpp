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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

#include <SkCanvas.h>
#include <SkData.h>
#include <SkEncodedImageFormat.h>
#include <SkImage.h>
#include <SkPngEncoder.h>
#include <SkSurface.h>
#include <android/ipcrenderbuffer/RenderBufferOps.h>
// TODO: Bitmap Arena system not in yet.
// #include <gui/BitmapArenaAllocator.h>
#include <gui/RenderCommandBuffer.h>
#include <gui/RenderCommandBufferConsumer.h>
#include <fstream>
#include <memory>

using namespace android;

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: replay_render_buffer <command_buffer_file> <bitmap_arena_file>\n");
        return 1;
    }
    const char* commandBufferFile = argv[1];
    const char* bitmapArenaFile = argv[2];

    // Load from files
    std::unique_ptr<RenderCommandBuffer> loadedCommandBuffer = std::unique_ptr<RenderCommandBuffer>(
            RenderCommandBuffer::loadFromFile(commandBufferFile));
    /*
    std::unique_ptr<BitmapArenaAllocator> loadedAllocator = std::unique_ptr<BitmapArenaAllocator>(
            BitmapArenaAllocator::loadFromFile(bitmapArenaFile));
    */

    if (!loadedCommandBuffer || !loadedAllocator) {
        printf("Failed to load from files");
        return 1;
    }

    std::shared_ptr<RenderCommandBufferConsumer> consumer =
            std::make_shared<RenderCommandBufferConsumer>();

    consumer->setRenderCommandBuffer(loadedCommandBuffer.get());
    /*
    consumer->setBitmapArenaAllocator(std::move(loadedAllocator));
    */

    resetRenderCommandBufferForReplay(consumer);

    // Dump to text file
    // dumpRenderCommandBufferToText(consumer, "/sdcard/render_command_buffer.txt");

    SkImageInfo info = SkImageInfo::MakeN32Premul(512, 512); // Example size
    const size_t minRowBytes = info.minRowBytes();
    const size_t size = info.computeMinByteSize();
    SkPMColor* pixels = new SkPMColor[size];

    // Create SkSurface using SkSurfaces::WrapPixels
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels, minRowBytes);
    if (!surface) {
        ALOGE("Failed to create SkSurface with SkSurfaces::WrapPixels");
        delete[] pixels;
        return 1;
    }
    SkCanvas* canvas = surface->getCanvas(); // Get Canvas from the Surface

    canvas->clear(SK_ColorWHITE); // Example background

    // Replay the render commands
    renderCommandBufferToCanvas(consumer, canvas); // Pass the canvas obtained from the surface

    // Now you can display the surface's image or save it to a file.
    // For example, to save to a PNG:
    sk_sp<SkImage> image = surface->makeImageSnapshot(); // Create image from surface
    if (!image) {
        ALOGE("Failed to create image snapshot");
        return 1;
    }

    SkPngEncoder::Options options; // Use default options for now
    sk_sp<SkData> pngData =
            SkPngEncoder::Encode(nullptr, image.get(), options); // Encode using SkPngEncoder
    if (pngData) {
        std::ofstream pngFile("/data/replay.png", std::ios::binary);
        pngFile.write(reinterpret_cast<const char*>(pngData->data()), pngData->size());
        pngFile.close();
        ALOGE("Replay saved to /data/replay.png");
    } else {
        ALOGE("Failed to encode to PNG using SkPngEncoder");
    }

    delete[] pixels; // Delete allocated pixels
    return 0;
}

#pragma clang diagnostic pop
