/*
 * Copyright (C) 2026 The Android Open Source Project
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

#undef LOG_TAG
#define LOG_TAG "PipelineCallbackHandlerTest"

#include <gtest/gtest.h>

#include "../skia/compat/PipelineCallbackHandler.h"

namespace android::renderengine {

namespace {

// This derived class splits open the PipelineCallbackHandler for testing
class PipelineCallbackHandlerTest : public skia::PipelineCallbackHandler {
public:
    PipelineCallbackHandlerTest()
          : skia::PipelineCallbackHandler(/* isProtected= */ false,
                                          /* storeSerializedKeys= */ true) {}

    struct PipelineStats {
        uint32_t mUses = 0;
        bool mHasData = false;
    };

    PipelineStats getPipelineStats(const char* label, uint32_t uniqueKeyHash) EXCLUDES(mMutex) {
        std::lock_guard<std::mutex> guard(mMutex);

        std::string searchLabel(label);
        auto iter = mMap.find(PipelineKey(&searchLabel, uniqueKeyHash));
        if (iter != mMap.end()) {
            return {iter->second->mUses, iter->second->mSerializedKey != nullptr};
        }

        return {};
    }

    struct CacheStats {
        uint32_t mTotNumPipelines = 0;
        uint32_t mNumPipelinesWithData = 0;
        uint32_t mNumPipelinesWithOutData = 0;
        size_t mTotalMemInSerializedKeys = 0;
    };

    CacheStats getCacheStats() EXCLUDES(mMutex) {
        std::lock_guard<std::mutex> guard(mMutex);

        CacheStats stats;

        stats.mTotNumPipelines = static_cast<uint32_t>(mMap.size());
        for (const auto& [_, value] : mMap) {
            if (value->mSerializedKey) {
                ++stats.mNumPipelinesWithData;
                stats.mTotalMemInSerializedKeys += value->mSerializedKey->size();
            } else {
                ++stats.mNumPipelinesWithOutData;
            }
        }

        return stats;
    }
};

enum class ActionType {
    kAddPipeline,
    kUsePipeline,
    kCheckCacheStats,
    kCheckPipeline,
};

class Action {
public:
    static Action AddPipeline(const char* label, uint32_t hash, bool hasData,
                              uint32_t dataPayload) {
        Action action(ActionType::kAddPipeline);
        action.mLabel = label;
        action.mHash = hash;
        action.mData = hasData ? SkData::MakeWithCopy(&dataPayload, sizeof(dataPayload)) : nullptr;
        return action;
    }
    static Action UsePipeline(const char* label, uint32_t hash) {
        Action action(ActionType::kUsePipeline);
        action.mLabel = label;
        action.mHash = hash;
        return action;
    }
    static Action CheckCacheStats(const PipelineCallbackHandlerTest::CacheStats& expectedStats) {
        Action action(ActionType::kCheckCacheStats);
        action.mExpectedCacheStats = expectedStats;
        return action;
    }
    static Action CheckPipeline(const char* label, uint32_t hash,
                                const PipelineCallbackHandlerTest::PipelineStats& expectedStats) {
        Action action(ActionType::kCheckPipeline);
        action.mLabel = label;
        action.mHash = hash;
        action.mExpectedPipelineStats = expectedStats;
        return action;
    }

    void execute(PipelineCallbackHandlerTest* handler, std::string& /* log */) const {
        using skgpu::graphite::ContextOptions::PipelineCacheOp::kAddingPipeline;
        using skgpu::graphite::ContextOptions::PipelineCacheOp::kPipelineFound;

        switch (mType) {
            case ActionType::kAddPipeline:
                handler->add(kAddingPipeline, mLabel, mHash, /* fromPrecompile= */ false, mData);
                break;
            case ActionType::kUsePipeline:
                handler->add(kPipelineFound, mLabel, mHash, /* fromPrecompile= */ false, nullptr);
                break;
            case ActionType::kCheckCacheStats: {
                PipelineCallbackHandlerTest::CacheStats stats = handler->getCacheStats();
                ASSERT_EQ(stats.mTotNumPipelines, mExpectedCacheStats.mTotNumPipelines);
                ASSERT_EQ(stats.mNumPipelinesWithData, mExpectedCacheStats.mNumPipelinesWithData);
                ASSERT_EQ(stats.mNumPipelinesWithOutData,
                          mExpectedCacheStats.mNumPipelinesWithOutData);
                ASSERT_EQ(stats.mTotalMemInSerializedKeys,
                          mExpectedCacheStats.mTotalMemInSerializedKeys);
            } break;
            case ActionType::kCheckPipeline: {
                PipelineCallbackHandlerTest::PipelineStats stats =
                        handler->getPipelineStats(mLabel, mHash);
                ASSERT_EQ(stats.mUses, mExpectedPipelineStats.mUses);
                ASSERT_EQ(stats.mHasData, mExpectedPipelineStats.mHasData);
            } break;
        }
    }

private:
    Action(ActionType type) : mType(type) {}

    const ActionType mType;
    // kAddPipeline and kUsePipeline
    const char* mLabel = nullptr;
    uint32_t mHash = 0;
    // kAddPipeline
    sk_sp<SkData> mData = nullptr;

    // kCheckCacheStats
    PipelineCallbackHandlerTest::CacheStats mExpectedCacheStats;

    // kCheckPipeline
    PipelineCallbackHandlerTest::PipelineStats mExpectedPipelineStats;
};

void run_test(SkSpan<const Action> actions) {
    std::string log;

    PipelineCallbackHandlerTest handler;

    for (const Action& action : actions) {
        action.execute(&handler, log);
    }
}

} // anonymous namespace

// Just add a Pipeline and check that it got added
TEST(PipelineCallbackHandlerTest, smokeTest0) {
    const Action kActions[] = {
            Action::AddPipeline("1", 1, /* hasData= */ false, 0),
            Action::CheckCacheStats({/* tot# */ 1, /* #withData */ 0, /* #withOut */ 1,
                                     /* totDataSize */ 0}),
    };

    run_test(kActions);
}

// Add a Pipeline and then add a usage
TEST(PipelineCallbackHandlerTest, smokeTest1) {
    const Action kActions[] = {
            Action::AddPipeline("1", 1, /* hasData= */ false, 0),
            Action::UsePipeline("1", 1),
            Action::CheckCacheStats({/* tot# */ 1, /* #withData */ 0, /* #withOut */ 1,
                                     /* totDataSize */ 0}),
            Action::CheckPipeline("1", 1, {/* uses=*/2, /* mHasData */ false}),
    };

    run_test(kActions);
}

// Only the label and hash value are used for keying. Check that a hash collision works correctly.
TEST(PipelineCallbackHandlerTest, smokeTest2) {
    const Action kActions[] = {
            // Note that the hash values are the same (i.e., 1)
            Action::AddPipeline("1", 1, /* hasData= */ false, 0),
            Action::AddPipeline("2", 1, /* hasData= */ false, 0),
            Action::CheckCacheStats({/* tot# */ 2, /* #withData */ 0, /* #withOut */ 2,
                                     /* totDataSize */ 0}),
    };

    run_test(kActions);
}

// Add one key w/ data and w/o to check that the counting is correct.
TEST(PipelineCallbackHandlerTest, smokeTest3) {
    const Action kActions[] = {
            // Note that the hash values are the same (i.e., 1)
            Action::AddPipeline("1", 1, /* hasData= */ true, 1),
            Action::AddPipeline("2", 2, /* hasData= */ false, 0),
            Action::CheckCacheStats({/* tot# */ 2, /* #withData */ 1, /* #withOut */ 1,
                                     /* totDataSize */ 4}),
    };

    run_test(kActions);
}

} // namespace android::renderengine
