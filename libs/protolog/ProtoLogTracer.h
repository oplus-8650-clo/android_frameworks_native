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

#ifndef ANDROID_PROTOLOG_TRACER_H
#define ANDROID_PROTOLOG_TRACER_H

#include "ProtoLogDataSource.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace android {
namespace protolog {

struct LogParam;

class ProtoLogTracer {
public:
    static ProtoLogTracer* Get();
    static void Initialize();
    static void Destroy();

    void Log(perfetto::protos::ProtoLogLevel level, const char* group, const char* format,
             va_list args);
    void Log(perfetto::protos::ProtoLogLevel level, const char* group, const char* format,
             std::vector<LogParam>& params);
    void Log(perfetto::protos::ProtoLogLevel level, const char* group, uint64_t messageHash,
             std::vector<LogParam>& params);

    // Called by ProtoLogDataSource
    void onStart(uint32_t instanceIndex);
    void onStop(uint32_t instanceIndex);

private:
    ProtoLogTracer() = default;
    ~ProtoLogTracer() = default;

    std::atomic<int> mActiveSessions{0};
};

struct LogParam {
    enum class Type { kInt, kDouble, kString, kBool, kInternedString };
    Type type;
    union {
        int64_t int_val;
        double double_val;
        const char* string_val;
        bool bool_val;
        uint64_t interned_string_id;
    };
};

} // namespace protolog
} // namespace android

#endif // ANDROID_PROTOLOG_TRACER_H
