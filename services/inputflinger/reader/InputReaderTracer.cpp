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

#include "include/InputReaderTracer.h"

#include <input/Input.h>

#include <memory>

#include "InputTracingBackendInterface.h"
#include "RawEvent.h"

namespace android {

InputReaderTracer::InputReaderTracer(
        std::shared_ptr<input_trace::InputTracingBackendInterface> backend)
      : mBackend(backend) {}

void InputReaderTracer::traceRawEvent(const RawEvent& event) {
    mBackend->traceRawEvent(event);
}

void InputReaderTracer::traceDeviceAddition(nsecs_t timestamp,
                                            const input_trace::TracedEvdevDevice& device) {
    mBackend->traceEvdevDeviceAddition(timestamp, device);
}

void InputReaderTracer::traceDeviceRemoval(nsecs_t timestamp, RawDeviceId deviceId) {
    mBackend->traceEvdevDeviceRemoval(timestamp, deviceId);
}

} // namespace android
