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

#include "ProtoLogDataSource.h"
#include <log/log.h>
#include "ProtoLogTracer.h"

#include <perfetto/config/android/protolog_config.pbzero.h>

namespace android {
namespace protolog {
// We use a thread_local variable to pass the configuration from OnSetup to
// OnStart. This is necessary because OnSetup is where the config is provided, but
// we need the instance_index (which is only available in OnStart) to store the
// config in our map. Perfetto guarantees that OnSetup and OnStart for a given
// data source instance are called sequentially on the same thread. Using a
// thread_local avoids a race condition where concurrent tracing sessions could
// overwrite a shared member variable before OnStart has a chance to read it.
thread_local ProtoLogConfig g_tls_config;

void ProtoLogDataSource::OnSetup(const SetupArgs& args) {
    const auto configRaw = args.config->protolog_config_raw();
    perfetto::protos::pbzero::ProtoLogConfig::Decoder config(configRaw);

    ProtoLogConfig newConfig;
    if (config.has_default_log_from_level()) {
        newConfig.defaultConfig.logFrom =
                static_cast<perfetto::protos::ProtoLogLevel>(config.default_log_from_level());
    }

    for (auto it = config.group_overrides(); it; ++it) {
        perfetto::protos::pbzero::ProtoLogGroup::Decoder groupDecoder(*it);
        std::string groupName = groupDecoder.group_name().ToStdString();
        GroupConfig groupConfig;
        if (groupDecoder.has_log_from()) {
            groupConfig.logFrom =
                    static_cast<perfetto::protos::ProtoLogLevel>(groupDecoder.log_from());
        }
        if (groupDecoder.has_collect_stacktrace()) {
            groupConfig.collectStackTrace = groupDecoder.collect_stacktrace();
        }
        newConfig.groupConfigs[groupName] = groupConfig;
    }

    g_tls_config = std::move(newConfig);
}

void ProtoLogDataSource::OnStart(const StartArgs& args) {
    uint32_t instanceIndex = args.internal_instance_index;
    ProtoLogConfig config = std::move(g_tls_config);

    {
        std::lock_guard<std::mutex> lock(mConfigsMutex);
        mInstanceConfigs[instanceIndex] = std::move(config);
    }

    ProtoLogTracer::Get()->onStart(instanceIndex);
}

void ProtoLogDataSource::OnStop(const StopArgs& args) {
    uint32_t instanceIndex = args.internal_instance_index;
    {
        std::lock_guard<std::mutex> lock(mConfigsMutex);
        auto it = mInstanceConfigs.find(instanceIndex);
        if (it != mInstanceConfigs.end()) {
            mInstanceConfigs.erase(it);
        }
    }

    ProtoLogTracer::Get()->onStop(instanceIndex);
}

ProtoLogConfig ProtoLogDataSource::getInstanceConfig(uint32_t instanceIndex) const {
    std::lock_guard<std::mutex> lock(mConfigsMutex);
    auto it = mInstanceConfigs.find(instanceIndex);
    if (it != mInstanceConfigs.end()) {
        return it->second;
    }
    return {};
}

} // namespace protolog
} // namespace android

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(android::protolog::ProtoLogDataSource);
