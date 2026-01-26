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
#include <android/os/binder/BinderCallsStats.h>
#include <android/os/binder/BinderSpamStats.h>
#include <android/os/binder/BnBinderStatsConsumerService.h>
#include <dlfcn.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <utils/SystemClock.h>

#include <../BuildFlags.h>
#include <../JvmUtils.h>
#include <../observer/BinderStatsPusher.h>
#include <../observer/BinderStatsUtils.h>
#include <jni.h>
#include "fakeservicemanager/FakeServiceManager.h"

using android::FakeServiceManager;
using namespace android;
using namespace testing;
using os::binder::BinderCallsStats;
using os::binder::BinderSpamStats;

#ifdef LIBBINDER_BINDER_OBSERVER_V2
constexpr int64_t kSpamAggregationWindowSec = 1;
constexpr int64_t kLatencyAggregationWindowSec = 1; // Same as spam for now
#else
constexpr int64_t kSpamAggregationWindowSec = 5;
constexpr int64_t kLatencyAggregationWindowSec = 5; // Same as spam for now
#endif

// --- Mocks ---
/**
 * Mock for IBinderStatsConsumerService to intercept calls for reporting stats.
 */
class MockBinderStatsConsumerService : public os::binder::BnBinderStatsConsumerService {
public:
    MOCK_METHOD(binder::Status, reportSpamStats, (const std::vector<BinderSpamStats>&), (override));
    MOCK_METHOD(binder::Status, reportCallStats, (const std::vector<BinderCallsStats>&),
                (override));
    MOCK_METHOD(binder::Status, reportSecondGranularityStats,
                (const std::vector<os::binder::SingleSecondBinderStats>&), (override));
    MOCK_METHOD(BBinder*, localBinder, (), (override));
};

/**
 * Mock for IServiceManager to control service lookup.
 */
class MockServiceManager : public FakeServiceManager {
public:
    MOCK_METHOD(sp<IBinder>, checkService, (const String16& name), (const, override));
};

// --- Test Fixture ---

/**
 * Initializes the default service manager with a mock implementation once per test run.
 */
void initServiceManagerOnce() {
    static std::once_flag gSmOnce;
    std::call_once(gSmOnce, [] {
        sp<NiceMock<MockServiceManager>> mockServiceManager =
                sp<NiceMock<MockServiceManager>>::make();
        setDefaultServiceManager(mockServiceManager);
    });
}
/**
 * Helper function to create a BinderSpamStats for spam comparison.
 */
BinderSpamStats createExpectedSpamStats(const BinderCallData& datum, int count125, int count250,
                                        int peakCount) {
    auto stats = BinderSpamStats();
    stats.clientUid = datum.senderUid;
    stats.interfaceDescriptor = datum.interfaceDescriptor;
    stats.aidlMethod = datum.aidlMethodName;
    stats.secondsWithAtLeast125Calls = count125;
    stats.secondsWithAtLeast250Calls = count250;
    stats.peakCallCountPerSecond = peakCount;
    return stats;
}

/**
 * Helper function to create a BinderCallsStats for latency comparison.
 */
BinderCallsStats createExpectedLatencyStats(const BinderCallData& datum, int64_t callCount,
                                            int64_t durationSumMicros,
                                            int64_t callDurationSumSquaredMicros,
                                            int32_t secondsWithAtLeast10Calls,
                                            int32_t secondsWithAtLeast50Calls) {
    auto stats = BinderCallsStats();
    stats.clientUid = datum.senderUid;
    stats.interfaceDescriptor = datum.interfaceDescriptor;
    stats.aidlMethod = datum.aidlMethodName;
    stats.callCount = callCount;
    stats.durationSumMicros = durationSumMicros;
    stats.callDurationSumSquaredMicros = callDurationSumSquaredMicros;
    stats.secondsWithAtLeast10Calls = secondsWithAtLeast10Calls;
    stats.secondsWithAtLeast50Calls = secondsWithAtLeast50Calls;
    return stats;
}

/**
 * Helper function to create a BinderCallsStats for latency comparison, including CPU data.
 */
BinderCallsStats createExpectedLatencyStatsWithCpu(const BinderCallData& datum, int64_t callCount,
                                                   int64_t durationSumMicros,
                                                   int64_t callDurationSumSquaredMicros,
                                                   int32_t secondsWithAtLeast10Calls,
                                                   int32_t secondsWithAtLeast50Calls,
                                                   int64_t cpuTimeCount, int64_t cpuTimeSumMicros,
                                                   int64_t cpuTimeSumSquaredMicros) {
    auto stats = BinderCallsStats();
    stats.clientUid = datum.senderUid;
    stats.interfaceDescriptor = datum.interfaceDescriptor;
    stats.aidlMethod = datum.aidlMethodName;
    stats.callCount = callCount;
    stats.durationSumMicros = durationSumMicros;
    stats.callDurationSumSquaredMicros = callDurationSumSquaredMicros;
    stats.secondsWithAtLeast10Calls = secondsWithAtLeast10Calls;
    stats.secondsWithAtLeast50Calls = secondsWithAtLeast50Calls;
    stats.cpuTimeCount = cpuTimeCount;
    stats.cpuTimeSumMicros = cpuTimeSumMicros;
    stats.cpuTimeSumSquaredMicros = cpuTimeSumSquaredMicros;
    return stats;
}

os::binder::SingleSecondBinderStats createExpectedSingleSecondBinderStats(
        int32_t clientUid, const String16& interfaceDescriptor, const String16& aidlMethod,
        int64_t callCount, int64_t durationSumMicros, int64_t callDurationSumSquaredMicros,
        int64_t cpuTimeCount, int64_t cpuTimeSumMicros, int64_t cpuTimeSumSquaredMicros) {
    auto stats = os::binder::SingleSecondBinderStats();
    stats.clientUid = clientUid;
    stats.interfaceDescriptor = interfaceDescriptor;
    stats.aidlMethod = aidlMethod.empty() ? String16(u"") : aidlMethod;
    stats.durationCount = callCount;
    stats.callCount = callCount;
    stats.durationMicrosSum = durationSumMicros;
    stats.durationMicrosSquaredSum = callDurationSumSquaredMicros;
    stats.cpuTimeCount = cpuTimeCount;
    stats.cpuTimeMicrosSum = cpuTimeSumMicros;
    stats.cpuTimeMicrosSquaredSum = cpuTimeSumSquaredMicros;
    return stats;
}

MATCHER_P(SpamStatsEq, expectedStats, "") {
    return arg.clientUid == expectedStats.clientUid &&
            arg.interfaceDescriptor == expectedStats.interfaceDescriptor &&
            arg.aidlMethod == expectedStats.aidlMethod &&
            arg.secondsWithAtLeast125Calls == expectedStats.secondsWithAtLeast125Calls &&
            arg.secondsWithAtLeast250Calls == expectedStats.secondsWithAtLeast250Calls &&
            arg.peakCallCountPerSecond == expectedStats.peakCallCountPerSecond;
}

MATCHER_P(CallStatsEq, expectedStats, "") {
    return arg.clientUid == expectedStats.clientUid &&
            arg.interfaceDescriptor == expectedStats.interfaceDescriptor &&
            arg.aidlMethod == expectedStats.aidlMethod &&
            arg.callCount == expectedStats.callCount &&
            arg.durationSumMicros == expectedStats.durationSumMicros &&
            arg.secondsWithAtLeast10Calls == expectedStats.secondsWithAtLeast10Calls &&
            arg.secondsWithAtLeast50Calls == expectedStats.secondsWithAtLeast50Calls;
}

MATCHER_P(CallStatsWithCpuEq, expectedStats, "") {
    return arg.clientUid == expectedStats.clientUid &&
            arg.interfaceDescriptor == expectedStats.interfaceDescriptor &&
            arg.aidlMethod == expectedStats.aidlMethod &&
            arg.callCount == expectedStats.callCount &&
            arg.durationSumMicros == expectedStats.durationSumMicros &&
            arg.secondsWithAtLeast10Calls == expectedStats.secondsWithAtLeast10Calls &&
            arg.secondsWithAtLeast50Calls == expectedStats.secondsWithAtLeast50Calls &&
            arg.cpuTimeCount == expectedStats.cpuTimeCount &&
            arg.cpuTimeSumMicros == expectedStats.cpuTimeSumMicros &&
            arg.cpuTimeSumSquaredMicros == expectedStats.cpuTimeSumSquaredMicros;
}

class BinderStatsPusherHelper {
public:
    static void doPush(BinderStatsPusher& pusher, std::vector<BinderCallData> data,
                       const int64_t nowSec) {
        auto addCallDataFn = pusher.getAddCallDataToBufferLockedFunction();
        for (auto& datum : data) {
            addCallDataFn(std::move(datum));
        }
#if defined(LIBBINDER_BINDER_OBSERVER_V2)
        // For V2, aggregation is triggered by the data stream.
        // To trigger processing of the pushed data, we need to add data points
        // for subsequent seconds. We add two points to be sure to flush the
        // previous second buffer.
        int64_t maxEndTimeSec = 0;
        if (!data.empty()) {
            for (const auto& d : data) {
                if (d.endTimeNanos > 0) {
                    maxEndTimeSec =
                            std::max((long long)maxEndTimeSec, d.endTimeNanos / 1000'000'000LL);
                }
            }
        }

        if (maxEndTimeSec == 0) {
            // Fallback for tests that don't set endTimeNanos
            maxEndTimeSec = nowSec + 1;
        }

        BinderCallData flush_datum1 = {.endTimeNanos = (maxEndTimeSec + 1) * 1000'000'000LL};
        addCallDataFn(std::move(flush_datum1));
        BinderCallData flush_datum2 = {.endTimeNanos = (maxEndTimeSec + 2) * 1000'000'000LL};
        addCallDataFn(std::move(flush_datum2));

#else
        pusher.pushLocked(nowSec);
#endif
    }
};
class BinderStatsPusherTest : public Test {
protected:
    sp<NiceMock<MockBinderStatsConsumerService>> mockStatsService;
    sp<NiceMock<MockServiceManager>> mockServiceManager;
    BinderStatsPusher pusher;
    void SetUp() override {
        mockStatsService = sp<NiceMock<MockBinderStatsConsumerService>>::make();
        initServiceManagerOnce();
        mockServiceManager = sp<NiceMock<MockServiceManager>>::cast(defaultServiceManager());
        ASSERT_NE(mockServiceManager, nullptr)
                << "Default service manager is not the expected mock type";
        // Default behavior: Service Manager returns the mock Stats Service
        ON_CALL(*mockServiceManager.get(), checkService(String16("binder_stats_consumer")))
                .WillByDefault(Return(IInterface::asBinder(mockStatsService)));
        ON_CALL(*mockStatsService.get(), localBinder()).WillByDefault(Return(nullptr));
    }
    void TearDown() override { testing::Mock::VerifyAndClear(defaultServiceManager().get()); }
};
// --- Test Cases ---

/**
 * Unit Test
 */
TEST_F(BinderStatsPusherTest, GetBinderStatsService) {
    EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
            .Times(1)
            .WillOnce(Return(IInterface::asBinder(mockStatsService)));
    auto service = pusher.getBinderStatsServiceLocked(15);
    ASSERT_EQ(service, mockStatsService);
}

TEST_F(BinderStatsPusherTest, AggregateSpamNoSpamBelowThreshold) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Spam stats are not supported with V2 observer";
    }
    std::vector<BinderCallData> data;
    int64_t currentTimeSec = 14;
    // Create data within the delay window (kDelaySeconds = 2)
    for (int i = 0; i < 50; ++i) { // Less than kMinSpamCount (125)
        data.push_back(BinderCallData{
                .startTimeNanos = (currentTimeSec - 5) * 1000'000'000LL,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IFoo"),
                .aidlMethodName = String16("MethodName1"),
                .transactionCode = 1,
                .senderUid = 1001,
        });
    }
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamOneSecondSpam) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Spam stats are not supported with V2 observer";
    }

    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;
    int32_t totalCalls = 150;
    // Create enough data in the *same second* to trigger spam, far enough in the past
    for (int i = 0; i < totalCalls; ++i) { // More than kMinSpamCount (125)
        data.push_back(BinderCallData{
                .startTimeNanos = currentTimeNanos - 8000'000'000,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IFoo"),
                .aidlMethodName = String16("MethodName2"),
                .transactionCode = 1,
                .senderUid = 1001,
        });
    }
    auto expectedStats = createExpectedSpamStats(data[0], 1, 0, totalCalls);
    EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateSpamDelayedSpam) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Spam stats are not supported with V2 observer";
    }
    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;
    // Create spam data within the delay window (kDelaySeconds = 2)
    for (int i = 0; i < 150; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = currentTimeNanos - 1000'000'000,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IBar"),
                .aidlMethodName = String16("MethodName2"),
                .transactionCode = 2,
                .senderUid = 1002,
        });
    }
    // Expect no calls, data is delayed
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateSpamMixedOlderAndDelayed) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is enabled";
    }

    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;
    int32_t totalCalls = 130;
    for (int i = 0; i < totalCalls; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = currentTimeNanos - 8000'000'000,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IBaz"),
                .aidlMethodName = String16("MethodName3"),
                .transactionCode = 3,
                .senderUid = 1003,
        });
    }
    // Delayed spam data (within kDelaySeconds)
    for (int i = 0; i < 140; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = currentTimeNanos - 1000'000'000,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IQux"),
                .aidlMethodName = String16("MethodName4"),
                .transactionCode = 4,
                .senderUid = 1004,
        });
    }
    // Expect immediate spam to be reported now
    auto expectedImmediateStats = createExpectedSpamStats(data[0], 1, 0, totalCalls);
    EXPECT_CALL(*mockStatsService,
                reportSpamStats(ElementsAre(SpamStatsEq(expectedImmediateStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateSpamSecondWatermark) {
    std::vector<BinderCallData> data;
    int64_t spamTimeNanos = 2'000'000'000LL;
    int64_t currentTimeSec = (spamTimeNanos / 1000'000'000LL) + kSpamAggregationWindowSec + 1;
    int32_t totalCalls = 300;
    // Create data exceeding the second watermark (250 calls/sec)
    for (int i = 0; i < totalCalls; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = spamTimeNanos,
                .endTimeNanos = spamTimeNanos + 1,
                .interfaceDescriptor = String16("IHighVolume"),
                .aidlMethodName = String16("MethodName5"),
                .transactionCode = 5,
                .senderUid = 1005,
        });
    }
    if (kBinderObserverV2Enabled) {
        auto expectedStats = createExpectedSingleSecondBinderStats(data[0].senderUid,
                                                                   data[0].interfaceDescriptor,
                                                                   data[0].aidlMethodName,
                                                                   totalCalls, 0, 0, 0, 0, 0);
        EXPECT_CALL(*mockStatsService, reportSecondGranularityStats(ElementsAre(expectedStats)))
                .Times(1);
        EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
        EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
    } else {
        auto expectedStats = createExpectedSpamStats(data[0], 1, 1, totalCalls);
        EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
                .Times(1);
        EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
    }

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamAcrossMultipleSeconds) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Spam stats across multiple seconds aren't supported with V2 observer";
    }
    std::vector<BinderCallData> data;
    int64_t firstSpamSecondNanos = 2'000'000'000LL;  // 2s
    int64_t secondSpamSecondNanos = 3'000'000'000LL; // 3s
    int64_t currentTimeSec = (secondSpamSecondNanos / 1000'000'000LL) + kSpamAggregationWindowSec +
            1; // 3 + 5 + 1 = 9s
    // Spam for the first second
    int32_t totalCalls1 = 150;
    for (int i = 0; i < totalCalls1; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = firstSpamSecondNanos,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IMultiSecond"),
                .aidlMethodName = String16("MethodName6"),
                .transactionCode = 6,
                .senderUid = 1006,
        });
    }
    // Spam for the second second
    int32_t totalCalls2 = 160;
    for (int i = 0; i < totalCalls2; ++i) {
        // Use the same UID, code, desc for aggregation
        data.push_back(BinderCallData{
                .startTimeNanos = secondSpamSecondNanos,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IMultiSecond"),
                .aidlMethodName = String16("MethodName6"),
                .transactionCode = 6,
                .senderUid = 1006,
        });
    }

    if (kBinderObserverV2Enabled) {
        auto expectedStats1 = createExpectedSpamStats(data[0], 1, 0, totalCalls1);
        auto expectedStats2 = createExpectedSpamStats(data[0], 1, 0, totalCalls2);
        EXPECT_CALL(*mockStatsService,
                    reportSpamStats(UnorderedElementsAre(SpamStatsEq(expectedStats1),
                                                         SpamStatsEq(expectedStats2))))
                .Times(1);
    } else {
        // Expect one atom representing spam across 2 seconds
        auto expectedStats =
                createExpectedSpamStats(data[0], 2, 0, std::max(totalCalls1, totalCalls2));
        EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
                .Times(1);
    }
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamProcessesDelayedDataOnSubsequentCall) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is enabled";
    }

    std::vector<BinderCallData> callData1;
    int64_t callTimeSec1 = 10;
    int64_t spamDataTimeNanos = callTimeSec1 * 1000'000'000LL;
    int32_t totalCalls = 150;
    for (int i = 0; i < totalCalls; ++i) {
        callData1.push_back(BinderCallData{
                .startTimeNanos = spamDataTimeNanos,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IDelayed"),
                .aidlMethodName = String16("MethodName7"),
                .transactionCode = 7,
                .senderUid = 1007,
        });
    }
    auto expectedStats = createExpectedSpamStats(callData1[0], 1, 0, totalCalls);
    // First push: data should be buffered as it's too recent
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1); // For the first push

    BinderStatsPusherHelper::doPush(pusher, callData1, callTimeSec1);

    // Second push: advance time so the previous data is now outside the aggregation window
    std::vector<BinderCallData> callData2; // Can be empty or contain new data
    int64_t call2_time_sec = callTimeSec1 + kSpamAggregationWindowSec + 1; // 10 + 5 + 1 = 16s
    EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1); // For the second push

    BinderStatsPusherHelper::doPush(pusher, callData2, call2_time_sec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamForDifferentMethodsSimultaneously) {
    std::vector<BinderCallData> data;
    int64_t spamTimeNanos = 4'000'000'000LL; // 4s
    int64_t currentTimeSec =
            (spamTimeNanos / 1000'000'000LL) + kSpamAggregationWindowSec + 1; // 4 + 5 + 1 = 10s
    // Spam for method 1
    int32_t totalCalls1 = 200;
    for (int i = 0; i < totalCalls1; ++i) {
        data.push_back({.startTimeNanos = spamTimeNanos,
                        .endTimeNanos = spamTimeNanos + 1,
                        .interfaceDescriptor = String16("IMultiMethod"),
                        .aidlMethodName = String16("MethodName9"),
                        .transactionCode = 9,
                        .senderUid = 1008});
    }

    // Spam for method 2
    int32_t totalCalls2 = 220;
    for (int i = 0; i < totalCalls2; ++i) {
        data.push_back({.startTimeNanos = spamTimeNanos,
                        .endTimeNanos = spamTimeNanos + 1,
                        .interfaceDescriptor = String16("IMultiMethod"),
                        .aidlMethodName = String16("MethodName8"),
                        .transactionCode = 8,
                        .senderUid = 1008});
    }

    if (kBinderObserverV2Enabled) {
        auto expectedStats1 = createExpectedSingleSecondBinderStats(1008, String16("IMultiMethod"),
                                                                    String16("MethodName9"),
                                                                    totalCalls1, 0, 0, 0, 0, 0);
        auto expectedStats2 = createExpectedSingleSecondBinderStats(1008, String16("IMultiMethod"),
                                                                    String16("MethodName8"),
                                                                    totalCalls2, 0, 0, 0, 0, 0);
        EXPECT_CALL(*mockStatsService,
                    reportSecondGranularityStats(
                            UnorderedElementsAre(expectedStats1, expectedStats2)))
                .Times(1);
    } else {
        auto expectedStats1 = createExpectedSpamStats(data[0], 1, 0, totalCalls1); // MethodName9
        auto expectedStats2 =
                createExpectedSpamStats(data[totalCalls1], 1, 0, totalCalls2); // MethodName8
        EXPECT_CALL(*mockStatsService,
                    reportSpamStats(UnorderedElementsAre(SpamStatsEq(expectedStats1),
                                                         SpamStatsEq(expectedStats2))))
                .Times(1);
    }
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, SkipPushForLocalBinderWithoutJvm) {
    // Simulate a local binder service
    sp<BBinder> localBinderInstance = sp<BBinder>::make();
    ON_CALL(*mockStatsService.get(), localBinder())
            .WillByDefault(Return(localBinderInstance.get()));
    // getJavaVM() is expected to return nullptr in the test environment (JvmUtils.h)
    std::vector<BinderCallData> data;
    int64_t spamTimeNanos = 2'000'000'000LL;                                                   // 2s
    int64_t currentTimeSec = (spamTimeNanos / 1000'000'000LL) + kSpamAggregationWindowSec + 1; // 8s
    for (int i = 0; i < 150; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = spamTimeNanos,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("ILocalSkipped"),
                .aidlMethodName = String16("MethodName10"),
                .transactionCode = 10,
                .senderUid = 1009,
        });
    }
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, DataNotDroppedWhenPushIsSkippedThenSucceeds) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Not supported for Binder Observer V2";
    }
    std::vector<BinderCallData> latencyCallData1;
    int64_t timeSec1 = 20;
    // Data old enough to be processed immediately
    int64_t latencyDataTimeNanos = (timeSec1 - kLatencyAggregationWindowSec) * 1000'000'000LL;
    int64_t totalDurationMicros = 0;
    int64_t durationSumSquaredMicros = 0;
    int32_t totalCalls = 15; // More than kLatencyCountFirstWatermark
    for (int i = 0; i < totalCalls; ++i) {
        int64_t durationMicros = (i + 1) * 100;
        latencyCallData1.push_back(BinderCallData{
                .startTimeNanos = latencyDataTimeNanos,
                .endTimeNanos = latencyDataTimeNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("IServiceSkipped"),
                .aidlMethodName = String16("MethodName11"),
                .transactionCode = 11,
                .senderUid = 1010,
        });
        totalDurationMicros += durationMicros;
        durationSumSquaredMicros += durationMicros * durationMicros;
    }
    auto expectedStats =
            createExpectedLatencyStats(latencyCallData1[0], totalCalls, totalDurationMicros,
                                       durationSumSquaredMicros, 1, 0);
    // First push: Service is unavailable
    EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
            .WillOnce(Return(nullptr));
    // localBinder() shouldn't be called if service is null
    EXPECT_CALL(*mockStatsService, localBinder()).Times(0);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportSecondGranularityStats(_)).Times(0);

    BinderStatsPusherHelper::doPush(pusher, latencyCallData1, timeSec1);

    Mock::VerifyAndClearExpectations(mockServiceManager.get());
    Mock::VerifyAndClearExpectations(mockStatsService.get());

    // Second push: Service becomes available. Advance time beyond service check timeout.
    if (kBinderObserverV2Enabled) {
        int64_t timeSec2 = timeSec1 + 1;
        auto expectedV2Stats =
                createExpectedSingleSecondBinderStats(latencyCallData1[0].senderUid,
                                                      latencyCallData1[0].interfaceDescriptor,
                                                      String16(""), 15, totalDurationMicros,
                                                      durationSumSquaredMicros, 0, 0, 0);
        EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
                .WillOnce(Return(IInterface::asBinder(mockStatsService)));
        EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
        EXPECT_CALL(*mockStatsService, reportSecondGranularityStats(ElementsAre(expectedV2Stats)))
                .Times(1);
        BinderStatsPusherHelper::doPush(pusher, {}, timeSec2);
    } else {
        int64_t timeSec2 = timeSec1 + 6;
        EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
                .WillOnce(Return(IInterface::asBinder(mockStatsService)));
        EXPECT_CALL(*mockStatsService, localBinder()).Times(1); // Called when service is not null
        EXPECT_CALL(*mockStatsService,
                    reportCallStats(ElementsAre(CallStatsWithCpuEq(expectedStats))))
                .Times(1);
        EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
        pusher.pushLocked(timeSec2);
    }
}

TEST_F(BinderStatsPusherTest, sizeOfStruct) {
#ifdef __LP64__
    EXPECT_EQ(sizeof(android::BinderCallData), 48);
#else
    EXPECT_EQ(sizeof(android::BinderCallData), 36);
#endif
}

// --- Latency Tests ---
TEST_F(BinderStatsPusherTest, AggregateLatencyNoLatencyBelowThreshold) {
    std::vector<BinderCallData> data;
    int64_t currentTimeSec = 14;
    // Create data within the delay window
    for (int i = 0; i < 5; ++i) { // Less than kLatencyCountFirstWatermark (10)
        data.push_back(BinderCallData{
                .startTimeNanos =
                        (currentTimeSec - kLatencyAggregationWindowSec + 1) * 1000'000'000LL,
                .endTimeNanos =
                        (currentTimeSec - kLatencyAggregationWindowSec + 1) * 1000'000'000LL +
                        1000000 /* 1ms */,
                .interfaceDescriptor = String16("ILatencyFoo"),
                .aidlMethodName = String16("MethodName1"),
                .transactionCode = 1,
                .senderUid = 2001,
        });
    }
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyOneSecondLatency) {
    std::vector<BinderCallData> data;
    int64_t callTimeNanos = 2'000'000'000LL; // 2s
    int64_t currentTimeSec =
            (callTimeNanos / 1000'000'000LL) + kLatencyAggregationWindowSec + 1; // 2 + 5 + 1 = 8s
    int64_t totalDurationMicros = 0;
    int64_t durationSumSquaredMicros = 0;
    // Create enough data in the *same second* to trigger latency reporting
    for (int i = 0; i < 15; ++i) {              // More than kLatencyCountFirstWatermark (10)
        int64_t durationMicros = (i + 1) * 100; // e.g. 100us, 200us ..
        data.push_back(BinderCallData{
                .startTimeNanos = callTimeNanos,
                .endTimeNanos = callTimeNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyBar"),
                .aidlMethodName = String16("MethodName2"),
                .transactionCode = 2,
                .senderUid = 2002,
        });
        totalDurationMicros += durationMicros;
        durationSumSquaredMicros += durationMicros * durationMicros;
    }

    if (kBinderObserverV2Enabled) {
        auto expectedStats =
                createExpectedSingleSecondBinderStats(data[0].senderUid,
                                                      data[0].interfaceDescriptor,
                                                      data[0].aidlMethodName, 15,
                                                      totalDurationMicros, durationSumSquaredMicros,
                                                      0, 0, 0);
        EXPECT_CALL(*mockStatsService, reportSecondGranularityStats(ElementsAre(expectedStats)))
                .Times(1);
    } else {
        auto expectedStats =
                createExpectedLatencyStatsWithCpu(data[0], 15, totalDurationMicros,
                                                  durationSumSquaredMicros, 1, 0, 0, 0, 0);
        EXPECT_CALL(*mockStatsService,
                    reportCallStats(ElementsAre(CallStatsWithCpuEq(expectedStats))))
                .Times(1);
    }
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyDelayedLatency) {
    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;
    // Create latency data within the aggregation window
    for (int i = 0; i < 15; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = currentTimeNanos - 1000'000'000,
                .endTimeNanos = currentTimeNanos - 1000'000'000 + 500000 /* 0.5ms */,
                .interfaceDescriptor = String16("ILatencyBaz"),
                .aidlMethodName = String16("MethodName3"),
                .transactionCode = 3,
                .senderUid = 2003,
        });
    }
    // Expect no calls, data is delayed
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyProcessesDelayedDataOnSubsequentCall) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is enabled";
    }
    std::vector<BinderCallData> callData1;
    int64_t callTimeSec1 = 10;
    int64_t latencyDataTimeNanos = callTimeSec1 * 1000'000'000LL; // 10s, will be delayed
    int64_t totalDurationMicros1 = 0;
    int64_t durationSumSquaredMicros1 = 0;
    for (int i = 0; i < 15; ++i) {
        int64_t durationMicros = (i + 1) * 150;
        callData1.push_back(BinderCallData{
                .startTimeNanos = latencyDataTimeNanos,
                .endTimeNanos = latencyDataTimeNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyDelayed"),
                .aidlMethodName = String16("MethodName4"),
                .transactionCode = 4,
                .senderUid = 2004,
        });
        totalDurationMicros1 += durationMicros;
        durationSumSquaredMicros1 += durationMicros * durationMicros;
    }

    // First push: data should be buffered as it's too recent
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, callData1, callTimeSec1);

    // Second push: advance time so the previous data is now outside the aggregation window
    std::vector<BinderCallData> callData2;                                    // Can be empty
    int64_t call2_time_sec = callTimeSec1 + kLatencyAggregationWindowSec + 1; // 10 + 5 + 1 = 16s

    auto expectedStats =
            createExpectedLatencyStatsWithCpu(callData1[0], 15, totalDurationMicros1,
                                              durationSumSquaredMicros1, 1, 0, 0, 0, 0);
    EXPECT_CALL(*mockStatsService, reportCallStats(ElementsAre(CallStatsWithCpuEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, callData2, call2_time_sec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyWithCpu) {
    std::vector<BinderCallData> data;
    int64_t callTimeNanos = 2'000'000'000LL; // 2s
    int64_t currentTimeSec =
            (callTimeNanos / 1000'000'000LL) + kLatencyAggregationWindowSec + 1; // 2 + 5 + 1 = 8s
    int64_t totalDurationMicros = 0;
    int64_t durationSumSquaredMicros = 0;
    int64_t totalCpuTimeMicros = 0;
    int64_t totalcpuTimeSumSquaredMicros = 0;
    // Create enough data in the *same second* to trigger latency reporting
    for (int i = 0; i < 15; ++i) {              // More than kLatencyCountFirstWatermark (10)
        int64_t durationMicros = (i + 1) * 100; // e.g. 100us, 200us ..
        int64_t cpuTimeMicros = (i + 1) * 50;
        data.push_back(BinderCallData{
                .startTimeNanos = callTimeNanos,
                .endTimeNanos = callTimeNanos + durationMicros * 1000,
                .cpuTimeNanos = cpuTimeMicros * 1000,
                .interfaceDescriptor = String16("ILatencyCpu"),
                .aidlMethodName = String16("MethodName2"),
                .transactionCode = 2,
                .senderUid = 2002,
        });
        totalDurationMicros += durationMicros;
        durationSumSquaredMicros += durationMicros * durationMicros;
        totalCpuTimeMicros += cpuTimeMicros;
        totalcpuTimeSumSquaredMicros += cpuTimeMicros * cpuTimeMicros;
    }
    if (kBinderObserverV2Enabled) {
        auto expectedStats =
                createExpectedSingleSecondBinderStats(data[0].senderUid,
                                                      data[0].interfaceDescriptor,
                                                      data[0].aidlMethodName, 15,
                                                      totalDurationMicros, durationSumSquaredMicros,
                                                      15, totalCpuTimeMicros,
                                                      totalcpuTimeSumSquaredMicros);
        EXPECT_CALL(*mockStatsService, reportSecondGranularityStats(ElementsAre(expectedStats)))
                .Times(1);
    } else {
        auto expectedStats =
                createExpectedLatencyStatsWithCpu(data[0], 15, totalDurationMicros,
                                                  durationSumSquaredMicros, 1, 0, 15,
                                                  totalCpuTimeMicros, totalcpuTimeSumSquaredMicros);
        EXPECT_CALL(*mockStatsService,
                    reportCallStats(ElementsAre(CallStatsWithCpuEq(expectedStats))))
                .Times(1);
    }
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyMultipleSeconds) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "This test is for Binder Observer V1 only.";
    }
    std::vector<BinderCallData> data;
    int64_t firstCallSecondNanos = 2'000'000'000LL;  // 2s
    int64_t secondCallSecondNanos = 3'000'000'000LL; // 3s
    int64_t currentTimeSec = (secondCallSecondNanos / 1000'000'000LL) +
            kLatencyAggregationWindowSec + 1; // 3 + 5 + 1 = 9s
    int64_t totalDurationMicros1 = 0;
    int64_t durationSumSquaredMicros1 = 0;
    const int totalCalls1 = 12;
    // Data for the first second
    for (int i = 0; i < totalCalls1; ++i) { // 12 calls
        int64_t durationMicros = (i + 1) * 100;
        data.push_back(BinderCallData{
                .startTimeNanos = firstCallSecondNanos,
                .endTimeNanos = firstCallSecondNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyMultiSec"),
                .aidlMethodName = String16("MethodName5"),
                .transactionCode = 5,
                .senderUid = 2005,
        });
        totalDurationMicros1 += durationMicros;
        durationSumSquaredMicros1 += durationMicros * durationMicros;
    }
    int64_t totalDurationMicros2 = 0;
    int64_t durationSumSquaredMicros2 = 0;
    const int totalCalls2 = 8;
    // Data for the second second
    for (int i = 0; i < totalCalls2; ++i) { // 8 calls
        int64_t durationMicros = (i + 1) * 120;
        // Use the same UID, code, desc for aggregation
        data.push_back(BinderCallData{
                .startTimeNanos = secondCallSecondNanos,
                .endTimeNanos = secondCallSecondNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyMultiSec"),
                .aidlMethodName = String16("MethodName5"),
                .transactionCode = 5,
                .senderUid = 2005,
        });
        totalDurationMicros2 += durationMicros;
        durationSumSquaredMicros2 += durationMicros * durationMicros;
    }
    if (kBinderObserverV2Enabled) {
        auto expectedStats1 =
                createExpectedSingleSecondBinderStats(data[0].senderUid,
                                                      data[0].interfaceDescriptor,
                                                      data[0].aidlMethodName, totalCalls1,
                                                      totalDurationMicros1,
                                                      durationSumSquaredMicros1, 0, 0, 0);
        auto expectedStats2 =
                createExpectedSingleSecondBinderStats(data[0].senderUid,
                                                      data[0].interfaceDescriptor,
                                                      data[0].aidlMethodName, totalCalls2,
                                                      totalDurationMicros2,
                                                      durationSumSquaredMicros2, 0, 0, 0);
        EXPECT_CALL(*mockStatsService,
                    reportSecondGranularityStats(
                            UnorderedElementsAre(expectedStats1, expectedStats2)))
                .Times(1);
        EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
        EXPECT_CALL(*mockStatsService, localBinder()).Times(2);
    } else {
        // Expect one atom representing aggregated data across 2 seconds
        // Total calls = 12 + 8 = 20
        // secondsWithAtLeast10Calls = 1 (only the first second has >= 10 calls)
        // secondsWithAtLeast50Calls = 0
        auto expectedStats =
                createExpectedLatencyStats(data[0], totalCalls1 + totalCalls2,
                                           totalDurationMicros1 + totalDurationMicros2,
                                           durationSumSquaredMicros1 + durationSumSquaredMicros2, 1,
                                           0);
        EXPECT_CALL(*mockStatsService, reportCallStats(ElementsAre(CallStatsEq(expectedStats))))
                .Times(1);
        EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
        EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
    }

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyMultipleSecondsV2) {
    if (!kBinderObserverV2Enabled) {
        GTEST_SKIP() << "This test is for Binder Observer V2 only.";
    }

    std::vector<BinderCallData> data;
    int64_t firstCallSecondNanos = 2'100'000'000LL;  // 2.1s
    int64_t secondCallSecondNanos = 3'100'000'000LL; // 3.1s

    int64_t totalDurationMicros1 = 0;
    int64_t durationSumSquaredMicros1 = 0;

    const int totalCalls1 = 12;
    // Data for the first second
    for (int i = 0; i < totalCalls1; ++i) { // 12 calls
        int64_t durationMicros = (i + 1) * 100;
        data.push_back(BinderCallData{
                .startTimeNanos = firstCallSecondNanos,
                .endTimeNanos = firstCallSecondNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyMultiSec"),
                .aidlMethodName = String16("MethodName5"),
                .transactionCode = 5,
                .senderUid = 2005,
        });
        totalDurationMicros1 += durationMicros;
        durationSumSquaredMicros1 += durationMicros * durationMicros;
    }

    int64_t totalDurationMicros2 = 0;
    int64_t durationSumSquaredMicros2 = 0;
    const int totalCalls2 = 8;
    // Data for the second second
    for (int i = 0; i < totalCalls2; ++i) { // 8 calls
        int64_t durationMicros = (i + 1) * 120;
        data.push_back(BinderCallData{
                .startTimeNanos = secondCallSecondNanos,
                .endTimeNanos = secondCallSecondNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyMultiSec"),
                .aidlMethodName = String16("MethodName5"),
                .transactionCode = 5,
                .senderUid = 2005,
        });
        totalDurationMicros2 += durationMicros;
        durationSumSquaredMicros2 += durationMicros * durationMicros;
    }

    // Expect two separate reports for each second.
    auto expectedStats1 =
            createExpectedSingleSecondBinderStats(data[0].senderUid, data[0].interfaceDescriptor,
                                                  data[0].aidlMethodName, totalCalls1,
                                                  totalDurationMicros1, durationSumSquaredMicros1,
                                                  0, 0, 0);
    auto expectedStats2 =
            createExpectedSingleSecondBinderStats(data[0].senderUid, data[0].interfaceDescriptor,
                                                  data[0].aidlMethodName, totalCalls2,
                                                  totalDurationMicros2, durationSumSquaredMicros2,
                                                  0, 0, 0);

    EXPECT_CALL(*mockStatsService, reportSecondGranularityStats(ElementsAre(expectedStats1)));
    EXPECT_CALL(*mockStatsService, reportSecondGranularityStats(ElementsAre(expectedStats2)));

    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(2);

    BinderStatsPusherHelper::doPush(pusher, data, 0 /* nowSec not used */);
}
