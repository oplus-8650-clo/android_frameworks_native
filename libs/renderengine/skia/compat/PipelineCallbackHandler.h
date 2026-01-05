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

#ifndef PipelineCallbackHandler_DEFINED
#define PipelineCallbackHandler_DEFINED

#include <android-base/thread_annotations.h>

#include <include/core/SkData.h>
#include <include/gpu/graphite/ContextOptions.h>

#include <chrono>
#include <mutex>
#include <unordered_map>

namespace android::renderengine::skia {

class PipelineCallbackHandler {
public:
    static void Callback(void* context, skgpu::graphite::ContextOptions::PipelineCacheOp op,
                         const std::string& label, uint32_t uniqueKeyHash, bool fromPrecompile,
                         sk_sp<SkData> androidStyleKey) {
        PipelineCallbackHandler* handler = reinterpret_cast<PipelineCallbackHandler*>(context);
        handler->add(op, label, uniqueKeyHash, fromPrecompile, std::move(androidStyleKey));
    }

    PipelineCallbackHandler();

    void beginWarmup() EXCLUDES(mMutex);
    void endWarmup() EXCLUDES(mMutex);

    // This is called by Skia to report some Pipeline caching event:
    //    PipelineCacheOp::kAddingPipeline --> a Pipeline is being added to the cache
    //    PipelineCacheOp::kPipelineFound  --> a preexisting Pipeline was found in the cache
    // Note that, due to purging, it is possible for the same Pipeline to be added to the cache
    // multiple times.
    void add(skgpu::graphite::ContextOptions::PipelineCacheOp op, const std::string& label,
             uint32_t uniqueKeyHash, bool fromPrecompile, sk_sp<SkData> androidStyleKey)
            EXCLUDES(mMutex);

    void reset() EXCLUDES(mMutex);

    void report(const char* label, std::string& result) EXCLUDES(mMutex);

private:
    std::mutex mMutex;

    // This is held as a unique_ptr in 'mMap' to simplify sorting in report() and to provide
    // a stable std::string* for the PipelineKey to use.
    struct PipelineData {
        explicit PipelineData(const std::string& label, std::chrono::milliseconds creationTime,
                              bool fromPrecompile, bool fromWarmup)
              : mLabel(label),
                mCreationTime(creationTime),
                mUses(fromPrecompile ? 0 : (fromWarmup ? 0 : 1)),
                mFromPrecompile(fromPrecompile),
                mFromWarmup(fromWarmup) {}
        const std::string mLabel;
        const std::chrono::milliseconds mCreationTime;
        uint32_t mUses;
        const bool mFromPrecompile;
        const bool mFromWarmup;
    };

    // 'mLabel' will either point to: a temporary search label for find()
    //                                or PipelineData::mLabel for emplace()
    struct PipelineKey {
        explicit PipelineKey(const std::string* label, uint32_t uniqueKeyHash)
              : mLabel(label), mUniqueKeyHash(uniqueKeyHash) {}
        PipelineKey() = default;

        std::size_t operator()(const PipelineKey& k) const { return k.mUniqueKeyHash; }

        bool operator==(const PipelineKey& other) const {
            return mUniqueKeyHash == other.mUniqueKeyHash && *mLabel == *other.mLabel;
        }

        const std::string* mLabel = nullptr;
        uint32_t mUniqueKeyHash = 0;
    };

    const std::chrono::time_point<std::chrono::steady_clock> mStartTime;
    bool mInWarmup GUARDED_BY(mMutex) = false;
    std::unordered_map<PipelineKey, std::unique_ptr<PipelineData>, PipelineKey> mMap
            GUARDED_BY(mMutex);
};

} // namespace android::renderengine::skia

#endif // PipelineCallbackHandler_DEFINED
