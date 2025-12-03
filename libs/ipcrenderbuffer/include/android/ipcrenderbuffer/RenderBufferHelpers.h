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

#include <android/ipcrenderbuffer/RenderBufferOps.h>
#include <gui/RenderCommandBufferConsumer.h>

namespace android {
void renderOpToCanvas(const std::shared_ptr<RenderCommandBufferConsumer>& consumer,
                      IPCRenderBufferOp* op, SkCanvas* canvas);
bool renderCommandBufferToCanvas(const std::shared_ptr<RenderCommandBufferConsumer>& consumer,
                                 SkCanvas* canvas,
                                 const std::function<void(int)>& renderProxyCallback);
void resetRenderCommandBufferForReplay(
        const std::shared_ptr<RenderCommandBufferConsumer>& consumer);
} // namespace android
