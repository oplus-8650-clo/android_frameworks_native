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

#ifndef ANDROID_PROTOLOG_DATA_SOURCE_H
#define ANDROID_PROTOLOG_DATA_SOURCE_H

#include <perfetto/config/android/protolog_config.pb.h>
#include <perfetto/tracing.h>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace android {
namespace protolog {

// Helper to convert log level to a comparable priority.
// A lower priority value means more verbose logging.
inline int32_t toProtoLogPriority(perfetto::protos::ProtoLogLevel level) {
    switch (level) {
        case perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_VERBOSE:
            return 0;
        case perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_DEBUG:
            return 1;
        case perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_INFO:
            return 2;
        case perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_WARN:
            return 3;
        case perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_ERROR:
            return 4;
        case perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_WTF:
            return 5;
        default:
            return -1;
    }
}

// Helper to combine hashes, from boost::hash_combine.
template <class T>
inline void hash_combine(std::size_t& seed, const T& v) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// Forward declarations.
class ProtoLogTracer;

struct MessageKey {
    const char* format;
    perfetto::protos::ProtoLogLevel level;
    const char* group;

    bool operator==(const MessageKey& other) const {
        return level == other.level && strcmp(format, other.format) == 0 &&
                strcmp(group, other.group) == 0;
    }
};

struct MessageKeyHash {
    std::size_t operator()(const MessageKey& k) const {
        size_t hash = 0;
        hash_combine(hash, std::string_view(k.format));
        hash_combine(hash, static_cast<int>(k.level));
        hash_combine(hash, std::string_view(k.group));
        return hash;
    }
};

struct CStringHash {
    using is_transparent = void;
    size_t operator()(const char* s) const { return std::hash<std::string_view>{}(s); }
    size_t operator()(const std::string& s) const { return std::hash<std::string>{}(s); }
};

template <typename K, typename V, typename Hash = std::hash<K>, typename Eq = std::equal_to<K>>
class LruCache {
public:
    LruCache(size_t maxSize) : mMaxSize(maxSize) {}

    std::optional<V> get(const K& key) {
        auto it = mMap.find(key);
        if (it == mMap.end()) {
            return std::nullopt;
        }
        mList.splice(mList.begin(), mList, it->second);
        return it->second->second;
    }

    void put(const K& key, V value) {
        auto it = mMap.find(key);
        if (it != mMap.end()) {
            it->second->second = value;
            mList.splice(mList.begin(), mList, it->second);
            return;
        }

        if (mMap.size() >= mMaxSize) {
            // Map is full, remove the least recently used element.
            auto lruNodeIt = std::prev(mList.end());
            auto node = mMap.extract(lruNodeIt->first);

            lruNodeIt->first = key;
            lruNodeIt->second = value;
            mList.splice(mList.begin(), mList, lruNodeIt);

            node.key() = key;
            node.mapped() = mList.begin();
            mMap.insert(std::move(node));
        } else {
            // Map not full. Default case.
            mList.emplace_front(key, value);
            mMap.emplace(key, mList.begin());
        }
    }

private:
    size_t mMaxSize;
    std::list<std::pair<K, V>> mList;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator, Hash, Eq> mMap;
};

// Data structures for ProtoLog state and configuration.
struct ProtoLogIncrementalState {
    // Interning maps
    std::unordered_map<std::string, uint64_t, CStringHash, std::equal_to<>> groupInterningMap;
    std::unordered_map<MessageKey, uint64_t, MessageKeyHash> messageInterningMap;
    LruCache<std::string, uint64_t> argStringInterningMap{1024};
    LruCache<std::string, uint64_t> stacktraceInterningMap{128};

    uint64_t nextInterningId = 1;

    // Whether the incremental state has been cleared.
    bool clearReported = false;
};

struct GroupConfig {
    perfetto::protos::ProtoLogLevel logFrom = perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_WTF;
    bool collectStackTrace = false;
};

struct ProtoLogConfig {
    GroupConfig defaultConfig;
    std::unordered_map<std::string, GroupConfig, CStringHash, std::equal_to<>> groupConfigs;

    const GroupConfig& getConfigFor(const char* groupTag) const {
        if (auto it = groupConfigs.find(groupTag); it != groupConfigs.end()) {
            return it->second;
        }
        return defaultConfig;
    }
};

struct ProtoLogTlsState {
    template <typename TraceContext>
    explicit ProtoLogTlsState(const TraceContext& trace_context) {
        auto dataSource = trace_context.GetDataSourceLocked();
        if (dataSource.valid()) {
            config = dataSource->getInstanceConfig(trace_context.instance_index());
        }
    }

    ProtoLogConfig config;
};

struct ProtoLogDataSourceTraits : public perfetto::DefaultDataSourceTraits {
    using IncrementalStateType = ProtoLogIncrementalState;
    using TlsStateType = ProtoLogTlsState;
};

class ProtoLogDataSource
      : public perfetto::DataSource<ProtoLogDataSource, ProtoLogDataSourceTraits> {
public:
    void OnSetup(const SetupArgs&) override;
    void OnStart(const StartArgs&) override;
    void OnStop(const StopArgs&) override;

    ProtoLogConfig getInstanceConfig(uint32_t instanceIndex) const;

    static constexpr auto* kName = "android.protolog";

private:
    mutable std::mutex mConfigsMutex;
    std::unordered_map<uint32_t, ProtoLogConfig> mInstanceConfigs;
};

} // namespace protolog
} // namespace android

#endif // ANDROID_PROTOLOG_DATA_SOURCE_H
