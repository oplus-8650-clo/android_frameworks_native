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

#define LOG_TAG "ProtoLog"

#include "ProtoLog.h"
#include "ProtoLogTracer.h"

#include <android_tracing.h>
#include <log/log.h>

namespace android {
namespace protolog {

std::once_flag gInitializeFlag;

void Initialize() {
    Initialize(perfetto::kSystemBackend);
}

void Initialize(uint32_t backends) {
    if (!android_tracing_native_proto_logging()) {
        ALOGD("ProtoLog is disabled. Skipping initialization.");
        return;
    }
    ALOGD("Initializing ProtoLog");

    std::call_once(gInitializeFlag, [backends] {
        perfetto::TracingInitArgs args;
        args.backends = backends;
        perfetto::Tracing::Initialize(args);

        perfetto::DataSourceDescriptor descriptor;
        descriptor.set_name(android::protolog::ProtoLogDataSource::kName);
        ProtoLogDataSource::Register(descriptor);
    });

    ProtoLogTracer::Initialize();
}

void Destroy() {
    if (!android_tracing_native_proto_logging()) {
        return;
    }
    ALOGD("Destroying ProtoLog");
    ProtoLogTracer::Destroy();
}

void Log(perfetto::protos::ProtoLogLevel level, const char* group, const char* format, ...) {
    if (!android_tracing_native_proto_logging()) {
        return;
    }
    va_list args;
    va_start(args, format);
    ProtoLogTracer::Get()->Log(level, group, format, args);
    va_end(args);
}

} // namespace protolog
} // namespace android
