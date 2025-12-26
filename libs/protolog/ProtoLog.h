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

#ifndef ANDROID_INTERNAL_PROTOLOG_CPP_H
#define ANDROID_INTERNAL_PROTOLOG_CPP_H

#include <cstdarg>

#if defined(__ANDROID__)
#include <perfetto/trace/android/protolog.pb.h>
#endif

namespace android {
namespace protolog {

#if defined(__ANDROID__)
/**
 * Initializes the C++ ProtoLog data source and registers it with Perfetto.
 * This must be called once before any logging can occur, typically at process
 * startup.
 */
void Initialize();

/**
 * Initializes the C++ ProtoLog data source and registers it with Perfetto.
 * This must be called once before any logging can occur, typically at process
 * startup.
 */
void Initialize(uint32_t backends);

/**
 * This is exposed for testing purposes. And doesn't need to be used in real code.
 */
void Destroy();

void Log(perfetto::protos::ProtoLogLevel level, const char* group, const char* format, ...)
        __attribute__((format(printf, 3, 4)));

#define PROTOLOG_D(group, format, ...) \
    PROTOLOG(perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_DEBUG, group, format, ##__VA_ARGS__)
#define PROTOLOG_V(group, format, ...) \
    PROTOLOG(perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_VERBOSE, group, format, ##__VA_ARGS__)
#define PROTOLOG_I(group, format, ...) \
    PROTOLOG(perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_INFO, group, format, ##__VA_ARGS__)
#define PROTOLOG_W(group, format, ...) \
    PROTOLOG(perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_WARN, group, format, ##__VA_ARGS__)
#define PROTOLOG_E(group, format, ...) \
    PROTOLOG(perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_ERROR, group, format, ##__VA_ARGS__)
#define PROTOLOG_WTF(group, format, ...) \
    PROTOLOG(perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_WTF, group, format, ##__VA_ARGS__)

#else
// Stub out ProtoLog for host builds
inline void Initialize() {}
inline void Initialize(uint32_t backends) {}
inline void Destroy() {}

#define PROTOLOG_D(...) (void)0
#define PROTOLOG_V(...) (void)0
#define PROTOLOG_I(...) (void)0
#define PROTOLOG_W(...) (void)0
#define PROTOLOG_E(...) (void)0
#define PROTOLOG_WTF(...) (void)0
#endif

// Public API: Call these macros to log.
#define PROTOLOG(level, group, format, ...) \
    android::protolog::Log(level, group, format, ##__VA_ARGS__)

} // namespace protolog
} // namespace android

#endif // ANDROID_INTERNAL_PROTOLOG_CPP_H
