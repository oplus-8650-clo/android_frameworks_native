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

#define LOG_TAG "ProtoLogTracer"

#include "ProtoLogTracer.h"

#include <log/log.h>
#include <perfetto/base/time.h>
#include <perfetto/public/producer.h>
#include <perfetto/trace/android/protolog.pbzero.h>
#include <perfetto/trace/interned_data/interned_data.pbzero.h>
#include <perfetto/trace/profiling/profile_common.pbzero.h>
#include <perfetto/trace/trace_packet.pbzero.h>
// #include <perfetto/tracing/trace_writer.h>

#include <utils/CallStack.h>
#include <utils/String8.h>
#include <string>

#include <android/log.h>
#include <unistd.h>
#include <optional>
#include <sstream>
#include <string>

namespace android {
namespace protolog {

namespace {
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

inline uint64_t InternGroupName(
        ProtoLogDataSource::TraceContext& ctx,
        ProtoLogDataSourceTraits::ProtoLogDataSourceTraits::IncrementalStateType* incrementalState,
        const char* group) {
    auto it = incrementalState->groupInterningMap.find(group);
    if (it != incrementalState->groupInterningMap.end()) {
        return it->second;
    }

    uint64_t groupId = incrementalState->nextInterningId++;
    incrementalState->groupInterningMap[group] = groupId;

    auto packet = ctx.NewTracePacket();
    auto* viewer_config = packet->set_protolog_viewer_config();
    auto* group_config = viewer_config->add_groups();
    group_config->set_id(groupId);
    group_config->set_name(group);
    group_config->set_tag(group);
    return groupId;
}

inline uint64_t InternMessage(
        ProtoLogDataSource::TraceContext& ctx,
        ProtoLogDataSourceTraits::ProtoLogDataSourceTraits::IncrementalStateType* incrementalState,
        const char* format, perfetto::protos::ProtoLogLevel level, const char* group,
        uint64_t groupId) {
    MessageKey message_key = {format, level, group};
    auto it_msg = incrementalState->messageInterningMap.find(message_key);
    if (it_msg != incrementalState->messageInterningMap.end()) {
        return it_msg->second;
    }

    uint64_t messageId = incrementalState->nextInterningId++;
    incrementalState->messageInterningMap[message_key] = messageId;

    auto packet = ctx.NewTracePacket();
    packet->set_sequence_flags(perfetto::protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
    auto* viewer_config = packet->set_protolog_viewer_config();
    auto* msg_data = viewer_config->add_messages();
    msg_data->set_message_id(messageId);
    msg_data->set_message(format);
    msg_data->set_level(static_cast<perfetto::protos::pbzero::ProtoLogLevel>(level));
    msg_data->set_group_id(groupId);
    return messageId;
}

inline void InternStringArgs(
        ProtoLogDataSource::TraceContext& ctx,
        ProtoLogDataSourceTraits::ProtoLogDataSourceTraits::IncrementalStateType* incrementalState,
        std::vector<LogParam>& params) {
    std::optional<protozero::MessageHandle<perfetto::protos::pbzero::TracePacket>> packet;
    perfetto::protos::pbzero::InternedData* interned_data = nullptr;

    for (auto& param : params) {
        if (param.type == LogParam::Type::kString) {
            std::string str_param = param.string_val ? param.string_val : "NULL";

            auto opt_id = incrementalState->argStringInterningMap.get(str_param);
            if (opt_id.has_value()) {
                param.type = LogParam::Type::kInternedString;
                param.interned_string_id = *opt_id;
                continue;
            }

            uint64_t strId = incrementalState->nextInterningId++;
            param.type = LogParam::Type::kInternedString;
            param.interned_string_id = strId;

            incrementalState->argStringInterningMap.put(str_param, strId);

            if (!packet) {
                packet.emplace(ctx.NewTracePacket());
                interned_data = (*packet)->set_interned_data();
            }

            auto* string_arg = interned_data->add_protolog_string_args();
            string_arg->set_iid(strId);
            string_arg->set_str(reinterpret_cast<const uint8_t*>(str_param.data()),
                                str_param.size());
        }
    }
}

inline uint64_t InternStackTrace(
        ProtoLogDataSource::TraceContext& ctx, const std::string& stacktrace,
        ProtoLogDataSourceTraits::ProtoLogDataSourceTraits::IncrementalStateType*
                incrementalState) {
    if (stacktrace.empty()) {
        return 0;
    }

    auto opt_id = incrementalState->stacktraceInterningMap.get(stacktrace);
    if (opt_id.has_value()) {
        return *opt_id;
    }

    uint64_t stacktraceId = incrementalState->nextInterningId++;
    incrementalState->stacktraceInterningMap.put(stacktrace, stacktraceId);

    auto packet = ctx.NewTracePacket();
    perfetto::protos::pbzero::InternedData* interned_data = packet->set_interned_data();
    auto* string_arg = interned_data->add_protolog_stacktrace();
    string_arg->set_iid(stacktraceId);
    string_arg->set_str(reinterpret_cast<const uint8_t*>(stacktrace.data()), stacktrace.size());
    return stacktraceId;
}

inline void WriteLogMessage(ProtoLogDataSource::TraceContext& ctx, uint64_t messageId,
                            uint64_t stacktraceId, const std::vector<LogParam>& params) {
    auto packet = ctx.NewTracePacket();
    packet->set_sequence_flags(perfetto::protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
    packet->set_timestamp(static_cast<uint64_t>(perfetto::base::GetBootTimeNs().count()));
    auto* protolog_msg = packet->set_protolog_message();
    protolog_msg->set_message_id(messageId);
    if (stacktraceId != 0) {
        protolog_msg->set_stacktrace_iid(stacktraceId);
    }

    for (const auto& param : params) {
        if (param.type == LogParam::Type::kInt) protolog_msg->add_sint64_params(param.int_val);
        if (param.type == LogParam::Type::kDouble)
            protolog_msg->add_double_params(param.double_val);
        if (param.type == LogParam::Type::kInternedString)
            protolog_msg->add_str_param_iids(param.interned_string_id);
        if (param.type == LogParam::Type::kBool) protolog_msg->add_boolean_params(param.bool_val);
    }
}
} // namespace

ProtoLogTracer* gTracer = nullptr;
std::mutex gTracerMutex;

ProtoLogTracer* ProtoLogTracer::Get() {
    return gTracer;
}

void ProtoLogTracer::Initialize() {
    std::lock_guard<std::mutex> lock(gTracerMutex);

    if (gTracer) {
        return;
    }

    gTracer = new ProtoLogTracer();
}

void ProtoLogTracer::Destroy() {
    // This is intended for use in test environments.
    std::lock_guard<std::mutex> lock(gTracerMutex);

    delete gTracer;
    gTracer = nullptr;
}

void ProtoLogTracer::onStart(uint32_t instanceIndex) {
    (void)instanceIndex;
    mActiveSessions.fetch_add(1, std::memory_order_relaxed);
}

void ProtoLogTracer::onStop(uint32_t instanceIndex) {
    (void)instanceIndex;
    mActiveSessions.fetch_sub(1, std::memory_order_relaxed);
}

std::string getNativeStackTraceString() {
    android::CallStack stack("ProtoLogTracer");
    stack.update(/*ignoreDepth=*/4);

    return std::string(stack.toString().c_str());
}

void ProtoLogTracer::Log(perfetto::protos::ProtoLogLevel level, const char* group,
                         const char* format, va_list args) {
    if (mActiveSessions.load(std::memory_order_relaxed) == 0) {
        return;
    }

    // Pre-parse all variadic arguments. This is more efficient than parsing them
    // repeatedly for each active tracing session.
    thread_local std::vector<LogParam> params;
    params.clear();
    va_list args_for_parsing;
    va_copy(args_for_parsing, args);
    for (const char* p = format; *p; ++p) {
        if (*p == '%') {
            p++; // Move past '%'
            if (*p == '%') continue;

            switch (*p) {
                case 'd': {
                    LogParam param;
                    param.type = LogParam::Type::kInt;
                    param.int_val = va_arg(args_for_parsing, int);
                    params.push_back(param);
                    break;
                }
                case 'f': {
                    LogParam param;
                    param.type = LogParam::Type::kDouble;
                    param.double_val = va_arg(args_for_parsing, double);
                    params.push_back(param);
                    break;
                }
                case 's': {
                    LogParam param;
                    param.type = LogParam::Type::kString;
                    param.string_val = va_arg(args_for_parsing, const char*);
                    params.push_back(param);
                    break;
                }
                case 'b': {
                    LogParam param;
                    param.type = LogParam::Type::kBool;
                    param.bool_val = (va_arg(args_for_parsing, int) != 0);
                    params.push_back(param);
                    break;
                }
            }
        }
    }
    va_end(args_for_parsing);

    ProtoLogDataSource::Trace([&](ProtoLogDataSource::TraceContext ctx) mutable {
        auto* tlsState = ctx.GetCustomTlsState();
        const auto& config = tlsState->config;
        const auto& groupConfig = config.getConfigFor(group);

        if (toProtoLogPriority(level) < toProtoLogPriority(groupConfig.logFrom)) {
            return;
        }

        auto* incrementalState = ctx.GetIncrementalState();

        if (!incrementalState->clearReported) {
            auto packet = ctx.NewTracePacket();
            packet->set_sequence_flags(
                    perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);
            incrementalState->clearReported = true;
        }

        uint64_t groupId = InternGroupName(ctx, incrementalState, group);
        uint64_t messageId = InternMessage(ctx, incrementalState, format, level, group, groupId);
        InternStringArgs(ctx, incrementalState, params);

        uint64_t stacktraceId = 0;
        if (groupConfig.collectStackTrace) {
            std::string stacktrace = getNativeStackTraceString();
            stacktraceId = InternStackTrace(ctx, stacktrace, incrementalState);
        }

        WriteLogMessage(ctx, messageId, stacktraceId, params);
    });
}

} // namespace protolog
} // namespace android
