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

#include "PipelineCallbackHandler.h"

#include <android-base/stringprintf.h>
#include <common/trace.h>
#include "Base64.h"

#include <vector>

namespace android::renderengine::skia {

namespace {

void traceSerializedKey(sk_sp<SkData> data) {
    if (!data->size()) {
        SFTRACE_FORMAT("re_skia_serialized_key:invalid_key_empty");
        return;
    }

    std::string str;
    str.resize(Base64::EncodedSize(data->size()));
    Base64::Encode(data->data(), data->size(), str.data());

    SFTRACE_FORMAT("re_skia_serialized_key:%s", str.c_str());
}

} // anonymous namespace

PipelineCallbackHandler::PipelineCallbackHandler(bool storeSerialedKeys)
      : mStartTime(std::chrono::steady_clock::now()), mStoreSerializedKeys(storeSerialedKeys) {}

void PipelineCallbackHandler::beginWarmup() {
    std::lock_guard<std::mutex> guard(mMutex);
    mInWarmup = true;
}
void PipelineCallbackHandler::endWarmup() {
    std::lock_guard<std::mutex> guard(mMutex);
    mInWarmup = false;
}

void PipelineCallbackHandler::add(skgpu::graphite::ContextOptions::PipelineCacheOp op,
                                  const std::string& label, uint32_t uniqueKeyHash,
                                  bool fromPrecompile, sk_sp<SkData> serializedKey) {
    std::lock_guard<std::mutex> guard(mMutex);

    auto iter = mMap.find(PipelineKey(&label, uniqueKeyHash));
    if (iter != mMap.end()) {
        // Pre-existing Pipeline - just update its usage(s)
        iter->second->mUses++;
    } else {
        SkASSERT(op == skgpu::graphite::ContextOptions::PipelineCacheOp::kAddingPipeline);

        if (serializedKey) {
            traceSerializedKey(serializedKey);
            if (!mStoreSerializedKeys) {
                serializedKey.reset();
            }
        }

        std::chrono::milliseconds creationTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - mStartTime);

        std::unique_ptr<PipelineData> newData =
                std::make_unique<PipelineData>(label, creationTime, std::move(serializedKey),
                                               fromPrecompile, mInWarmup);

        mMap.emplace(
                std::make_pair(PipelineKey(&newData->mLabel, uniqueKeyHash), std::move(newData)));
    }
}

void PipelineCallbackHandler::report(const char* label, std::string& result) {
    // The assumption is that we're just doing this very infrequently so we just lock for the
    // entire method.
    std::lock_guard<std::mutex> guard(mMutex);

    std::vector<const PipelineData*> tmp;
    int precompileCount = 0, warmupCount = 0, normalCount = 0;

    tmp.reserve(mMap.size());
    for (const auto& [_, value] : mMap) {
        tmp.push_back(value.get());
        if (value->mFromPrecompile) {
            ++precompileCount;
        } else if (value->mFromWarmup) {
            ++warmupCount;
        } else {
            ++normalCount;
        }
    }

    std::sort(tmp.begin(), tmp.end(), [](const PipelineData* a, const PipelineData* b) {
        if (a->mUses != b->mUses) {
            return a->mUses > b->mUses;
        }
        return a->mLabel < b->mLabel;
    });

    base::StringAppendF(&result,
                        "%zu %s Pipelines (%d Warmup/%d Normal/%d Precompile) ----------\n",
                        tmp.size(), label, warmupCount, normalCount, precompileCount);

    for (const PipelineData* data : tmp) {
        base::StringAppendF(&result, "%c %d %.3fs %s %zuB\n",
                            data->mFromPrecompile ? 'P' : (data->mFromWarmup ? 'W' : 'N'),
                            data->mUses, data->mCreationTime.count() / 1000.0f,
                            data->mLabel.c_str(),
                            data->mSerializedKey ? data->mSerializedKey.get()->size() : 0);
    }
}

} // namespace android::renderengine::skia
