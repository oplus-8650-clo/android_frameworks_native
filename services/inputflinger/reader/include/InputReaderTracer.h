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

#include <android-base/thread_annotations.h>
#include <gui/WindowInfosListener.h>
#include <input/Input.h>
#include <utils/RefBase.h>
#include <utils/StrongPointer.h>

#include <memory>
#include <mutex>

#include "InputTracingBackendInterface.h"
#include "RawEvent.h"

namespace android {

/**
 * Tracer implementation for InputReader and associated components.
 */
class InputReaderTracer {
public:
    explicit InputReaderTracer(std::shared_ptr<input_trace::InputTracingBackendInterface> backend);
    InputReaderTracer(const InputReaderTracer&) = delete;
    InputReaderTracer& operator=(const InputReaderTracer&) = delete;

    void traceRawEvent(const RawEvent& event);
    void traceDeviceAddition(nsecs_t timestamp, const input_trace::TracedEvdevDevice& device);
    void traceDeviceRemoval(nsecs_t timestamp, RawDeviceId deviceId);

    void onWindowInfosChanged(const gui::WindowInfosUpdate& update);

private:
    std::shared_ptr<input_trace::InputTracingBackendInterface> mBackend;

    std::mutex mLock;
    bool mSecureWindowVisible GUARDED_BY(mLock) = false;

    void setSecureWindowVisible(bool visible);
};

} // namespace android
