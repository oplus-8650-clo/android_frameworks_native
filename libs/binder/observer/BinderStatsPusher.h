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
#pragma once

#include <android/os/binder/IBinderStatsConsumerService.h>
#include <vector>
#include "BinderStatsUtils.h"

#include "BinderCallsMapAggregation.h"

class BinderStatsPusherTest_GetBinderStatsService_Test;
class BinderAllocation_BinderStatsPusher_aggregateStatsLocked_Test;

namespace android {
/**
 * Processes and pushes binder transaction statistics to the StatsBootstrapAtomService.
 *
 * This class is responsible for aggregating collected BinderCallData
 * such as binder spam which are then reported as atoms.
 * It manages the interaction with the StatsBootstrapAtomService, including
 * handling boot completion and service availability checks.
 *
 * This class is not Thread-safe.
 */
class BinderStatsPusher {
public:
    /**
     * Functor to add BinderCallData to the internal aggregation buffer.
     */
    struct AddCallDataFunctor {
        AddCallDataFunctor(BinderStatsPusher* pusher) : mPusher(pusher) {}
        BinderStatsPusher* mPusher;
        void operator()(BinderCallData&& b) const;
    };

    /**
     * Pushes binder transaction data to the IBinderStatsConsumerService.
     */
    void pushLocked(const int64_t nowSec);

    /**
     * Get a function which adds the BinderCallData to the internal Buffer.
     */
    AddCallDataFunctor getAddCallDataToBufferLockedFunction();

private:
    /**
     * Aggregates statistics for a chunk of binder call data.
     * A chunk is defined as a set of binder calls having the same
     * start time second, interface descriptor, transaction code and sender uid
     */
    struct CallAggregation {
        uint64_t numOfCalls = 0;
        uint64_t callsWithLatency = 0;
        int64_t durationSumMicros = 0;
        int64_t callDurationSumSquaredMicros = 0;
        uint64_t cpuTimeCount = 0;
        int64_t cpuTimeSumMicros = 0;
        int64_t cpuTimeSumSquaredMicros = 0;
        int32_t secondsWithAtLeast10Calls = 0;
        int32_t secondsWithAtLeast50Calls = 0;

        void reset() {
            numOfCalls = 0;
            callsWithLatency = 0;
            durationSumMicros = 0;
            callDurationSumSquaredMicros = 0;
            cpuTimeCount = 0;
            cpuTimeSumMicros = 0;
            cpuTimeSumSquaredMicros = 0;
        }
    };

    friend ::BinderStatsPusherTest_GetBinderStatsService_Test;
    friend ::BinderAllocation_BinderStatsPusher_aggregateStatsLocked_Test;
    sp<os::binder::IBinderStatsConsumerService> getBinderStatsServiceLocked(const int64_t nowSec);
    /**
     * Appends aggregated call statistics to the mCallStats report.
     */
    void appendCallStatsToReportLocked(const BinderCallData& chunk, const CallAggregation& agg);
    /**
     * Aggregates binder transaction data into Binder Report objects.
     */
    void aggregateStatsLocked(int64_t nowSec);

    // Class used to store and aggregate data.
    BinderCallsMapAggregation mCallsAggregation;
    // Vectors to temporarily store the data before sending to BinderStatsConsumer.
    // They are member variables to reduce allocations across multiple calls.
    std::vector<os::binder::BinderCallsStats> mCallStats;
    std::vector<os::binder::BinderSpamStats> mSpamStats;

    /**
     * Time window (in seconds) for aggregating per-second call counts.
     * Data for a specific second (startTimeSec) is processed for spam detection
     * and to update latency-related 'secondsWithAtLeastXCalls' counts
     * once 'nowSec - startTimeSec >= kAggregationWindowSec'.
     */
    static const int64_t kAggregationWindowSec = 5;
    /**
     * Time of the last check for the binder stats consumer service.
     */
    int64_t mServiceCheckTimeSec = -kCheckServiceTimeoutSec - 1;
    /**
     * Timeout for checking the binder stats consumer service.
     */
    static const int32_t kCheckServiceTimeoutSec = 5;
    static const int32_t kLatencyCountFirstWatermark = 10;
    static const int32_t kLatencyCountSecondWatermark = 50;
    static const int32_t kSpamFirstWatermark = 125;
    static const int32_t kSpamSecondWatermark = 250;
    static const int32_t kMaxStatsCount = 64;

    /**
     * Flag indicating if the last check for the binder stats consumer service was successful.
     */
    bool mLastServiceCheckSucceeded = true;
};

} // namespace android
