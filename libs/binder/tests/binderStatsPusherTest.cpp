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

#include <../BinderStatsPusher.h>
#include <../BinderStatsUtils.h>
#include <../JvmUtils.h>
#include "fakeservicemanager/FakeServiceManager.h"

#include <jni.h>

using android::FakeServiceManager;
using namespace android;
using namespace testing;
using os::binder::BinderCallsStats;
using os::binder::BinderSpamStats;

// --- Mocks ---
constexpr int64_t kSpamAggregationWindowSec = 5;
constexpr int64_t kLatencyAggregationWindowSec = 5; // Same as spam for now
// Mock for IBinderStatsConsumerService
class MockBinderStatsConsumerService : public os::binder::BnBinderStatsConsumerService {
public:
    MOCK_METHOD(binder::Status, reportSpamStats, (const std::vector<BinderSpamStats>&), (override));
    MOCK_METHOD(binder::Status, reportCallStats, (const std::vector<BinderCallsStats>&),
                (override));
    MOCK_METHOD(BBinder*, localBinder, (), (override));
};

// Mock for IServiceManager to control service lookup
class MockServiceManager : public FakeServiceManager {
public:
    MOCK_METHOD(sp<IBinder>, checkService, (const String16& name), (const, override));
};

// --- Test Fixture ---
void initServiceManagerOnce() {
    static std::once_flag gSmOnce;
    std::call_once(gSmOnce, [] {
        sp<NiceMock<MockServiceManager>> mockServiceManager =
                sp<NiceMock<MockServiceManager>>::make();
        setDefaultServiceManager(mockServiceManager);
    });
}

// Helper function to create a BinderSpamStats for spam comparison
BinderSpamStats createExpectedSpamStats(const BinderCallData& datum, int count125, int count250) {
    auto stats = BinderSpamStats();
    stats.clientUid = datum.senderUid;
    stats.interfaceDescriptor = datum.interfaceDescriptor;
    stats.aidlMethod = datum.aidlMethodName;

    stats.secondsWithAtLeast125Calls = count125;
    stats.secondsWithAtLeast250Calls = count250;
    return stats;
}

// Helper function to create a BinderCallsStats for latency comparison
BinderCallsStats createExpectedLatencyStats(const BinderCallData& datum, int64_t callCount,
                                            int64_t durationSumMicros,
                                            int32_t secondsWithAtLeast10Calls,
                                            int32_t secondsWithAtLeast50Calls) {
    auto stats = BinderCallsStats();
    stats.clientUid = datum.senderUid;
    stats.interfaceDescriptor = datum.interfaceDescriptor;
    stats.aidlMethod = datum.aidlMethodName;
    stats.callCount = callCount;
    stats.durationSumMicros = durationSumMicros;
    stats.secondsWithAtLeast10Calls = secondsWithAtLeast10Calls;
    stats.secondsWithAtLeast50Calls = secondsWithAtLeast50Calls;
    return stats;
}

MATCHER_P(SpamStatsEq, expectedStats, "") {
    return arg.clientUid == expectedStats.clientUid &&
            arg.interfaceDescriptor == expectedStats.interfaceDescriptor &&
            arg.aidlMethod == expectedStats.aidlMethod &&
            arg.secondsWithAtLeast125Calls == expectedStats.secondsWithAtLeast125Calls &&
            arg.secondsWithAtLeast250Calls == expectedStats.secondsWithAtLeast250Calls;
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

class BinderStatsPusherTest : public Test {
protected:
    sp<StrictMock<MockBinderStatsConsumerService>> mockStatsService;
    sp<NiceMock<MockServiceManager>> mockServiceManager;
    BinderStatsPusher pusher;

    void SetUp() override {
        mockStatsService = sp<StrictMock<MockBinderStatsConsumerService>>::make();
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

// Unit Test
TEST_F(BinderStatsPusherTest, GetBinderStatsService) {
    EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
            .Times(1)
            .WillOnce(Return(IInterface::asBinder(mockStatsService)));

    auto service = pusher.getBinderStatsServiceLocked(15);
    ASSERT_EQ(service, mockStatsService);
}

TEST_F(BinderStatsPusherTest, AggregateSpamNoSpamBelowThreshold) {
    std::vector<BinderCallData> data;
    int64_t currentTimeSec = 14;
    // Create data within the delay window (kDelaySeconds = 2)
    for (int i = 0; i < 50; ++i) { // Less than kMinSpamCount (125)
        data.push_back({
                .startTimeNanos = (currentTimeSec - 5) * 1000000000LL,
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

    pusher.pushLocked(data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamOneSecondSpam) {
    std::vector<BinderCallData> data;

    int64_t currentTimeNanos = 9'100'000'000;
    // Create enough data in the *same second* to trigger spam, far enough in the past
    for (int i = 0; i < 150; ++i) { // More than kMinSpamCount (125)
        data.push_back({
                .startTimeNanos = currentTimeNanos - 8000'000'000,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IFoo"),
                .aidlMethodName = String16("MethodName2"),
                .transactionCode = 1,
                .senderUid = 1001,
        });
    }

    auto expectedStats = createExpectedSpamStats(data[0], 1, 0);
    EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    pusher.pushLocked(data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateSpamDelayedSpam) {
    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;

    // Create spam data within the delay window (kDelaySeconds = 2)
    for (int i = 0; i < 150; ++i) {
        data.push_back({
                .interfaceDescriptor = String16("IBar"),
                .transactionCode = 2,
                .startTimeNanos = currentTimeNanos - 1000'000'000,
                .endTimeNanos = 0,
                .senderUid = 1002,
                .aidlMethodName = String16("MethodName2"),
        });
    }

    // Expect no calls, data is delayed
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
    pusher.pushLocked(data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateSpamMixedOlderAndDelayed) {
    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;

    for (int i = 0; i < 130; ++i) {
        data.push_back({
                .interfaceDescriptor = String16("IBaz"),
                .transactionCode = 3,
                .startTimeNanos = currentTimeNanos - 8000'000'000,
                .endTimeNanos = 0,
                .senderUid = 1003,
                .aidlMethodName = String16("MethodName3"),
        });
    }
    // Delayed spam data (within kDelaySeconds)
    for (int i = 0; i < 140; ++i) {
        data.push_back({
                .interfaceDescriptor = String16("IQux"),
                .transactionCode = 4,
                .startTimeNanos = currentTimeNanos - 1000'000'000,
                .endTimeNanos = 0,
                .senderUid = 1004,
                .aidlMethodName = String16("MethodName4"),
        });
    }

    // Expect immediate spam to be reported now
    auto expectedImmediateStats = createExpectedSpamStats(data[0], 1, 0);
    EXPECT_CALL(*mockStatsService,
                reportSpamStats(ElementsAre(SpamStatsEq(expectedImmediateStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
    pusher.pushLocked(data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateSpamSecondWatermark) {
    std::vector<BinderCallData> data;
    int64_t spamTimeNanos = 2'000'000'000LL;
    int64_t currentTimeSec = (spamTimeNanos / 1000'000'000LL) + kSpamAggregationWindowSec + 1;

    // Create data exceeding the second watermark (250 calls/sec)
    for (int i = 0; i < 300; ++i) {
        data.push_back({
                .interfaceDescriptor = String16("IHighVolume"),
                .transactionCode = 5,
                .startTimeNanos = spamTimeNanos,
                .endTimeNanos = 0,
                .senderUid = 1005,
                .aidlMethodName = String16("MethodName5"),
        });
    }

    auto expectedStats = createExpectedSpamStats(data[0], 1, 1);
    EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    pusher.pushLocked(data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamAcrossMultipleSeconds) {
    std::vector<BinderCallData> data;
    int64_t firstSpamSecondNanos = 2'000'000'000LL;  // 2s
    int64_t secondSpamSecondNanos = 3'000'000'000LL; // 3s
    int64_t currentTimeSec = (secondSpamSecondNanos / 1000'000'000LL) + kSpamAggregationWindowSec +
            1; // 3 + 5 + 1 = 9s

    // Spam for the first second
    for (int i = 0; i < 150; ++i) {
        data.push_back({
                .interfaceDescriptor = String16("IMultiSecond"),
                .transactionCode = 6,
                .startTimeNanos = firstSpamSecondNanos,
                .endTimeNanos = 0,
                .senderUid = 1006,
                .aidlMethodName = String16("MethodName6"),
        });
    }
    // Spam for the second second
    for (int i = 0; i < 160; ++i) {
        // Use the same UID, code, desc for aggregation
        data.push_back({
                .interfaceDescriptor = String16("IMultiSecond"),
                .transactionCode = 6,
                .startTimeNanos = secondSpamSecondNanos,
                .endTimeNanos = 0,
                .senderUid = 1006,
                .aidlMethodName = String16("MethodName6"),
        });
    }

    // Expect one atom representing spam across 2 seconds
    auto expectedStats = createExpectedSpamStats(data[0], 2, 0);
    EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    pusher.pushLocked(data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamProcessesDelayedDataOnSubsequentCall) {
    std::vector<BinderCallData> callData1;
    int64_t callTimeSec1 = 10;
    int64_t spamDataTimeNanos = (callTimeSec1 - 2) * 1000'000'000LL; // 8s, will be delayed

    for (int i = 0; i < 150; ++i) {
        callData1.push_back({
                .interfaceDescriptor = String16("IDelayed"),
                .transactionCode = 7,
                .startTimeNanos = spamDataTimeNanos,
                .endTimeNanos = 0,
                .senderUid = 1007,
                .aidlMethodName = String16("MethodName7"),
        });
    }

    // First push: data should be buffered as it's too recent
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1); // For the first push
    pusher.pushLocked(callData1, callTimeSec1);

    // Second push: advance time so the previous data is now outside the aggregation window
    std::vector<BinderCallData> callData2; // Can be empty or contain new data
    int64_t call2_time_sec = callTimeSec1 + kSpamAggregationWindowSec + 1; // 10 + 5 + 1 = 16s

    auto expectedStats = createExpectedSpamStats(callData1[0], 1, 0);
    EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1); // For the second push

    pusher.pushLocked(callData2, call2_time_sec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamForDifferentMethodsSimultaneously) {
    std::vector<BinderCallData> data;
    int64_t spamTimeNanos = 4'000'000'000LL; // 4s
    int64_t currentTimeSec =
            (spamTimeNanos / 1000'000'000LL) + kSpamAggregationWindowSec + 1; // 4 + 5 + 1 = 10s

    // Spam for method 1
    BinderCallData method1Spam = {
            .interfaceDescriptor = String16("IMultiMethod"),
            .transactionCode = 8,
            .startTimeNanos = spamTimeNanos,
            .endTimeNanos = 0,
            .senderUid = 1008,
            .aidlMethodName = String16("MethodName8"),
    };
    for (int i = 0; i < 200; ++i) {
        data.push_back(method1Spam);
    }

    // Spam for method 2 (different transaction code)
    BinderCallData method2Spam = {
            .interfaceDescriptor = String16("IMultiMethod"),
            .transactionCode = 9,
            .startTimeNanos = spamTimeNanos,
            .endTimeNanos = 0,
            .senderUid = 1008,
            .aidlMethodName = String16("MethodName9"),
    };
    for (int i = 0; i < 200; ++i) {
        data.push_back(method2Spam);
    }

    auto expectedStats1 = createExpectedSpamStats(method1Spam, 1, 0);
    auto expectedStats2 = createExpectedSpamStats(method2Spam, 1, 0);

    EXPECT_CALL(*mockStatsService,
                reportSpamStats(UnorderedElementsAre(SpamStatsEq(expectedStats1),
                                                     SpamStatsEq(expectedStats2))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    pusher.pushLocked(data, currentTimeSec);
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
        data.push_back({
                .interfaceDescriptor = String16("ILocalSkipped"),
                .transactionCode = 10,
                .startTimeNanos = spamTimeNanos,
                .endTimeNanos = 0,
                .senderUid = 1009,
                .aidlMethodName = String16("MethodName10"),
        });
    }

    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    pusher.pushLocked(data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, DataNotDroppedWhenPushIsSkippedThenSucceeds) {
    std::vector<BinderCallData> spamCallData1;
    int64_t timeSec1 = 20;
    // Data old enough to be processed immediately
    int64_t spamDataTimeNanos = (timeSec1 - kSpamAggregationWindowSec) * 1000'000'000LL;

    for (int i = 0; i < 150; ++i) { // Enough to trigger kSpamFirstWatermark
        spamCallData1.push_back({
                .interfaceDescriptor = String16("IServiceSkipped"),
                .transactionCode = 11,
                .startTimeNanos = spamDataTimeNanos,
                .endTimeNanos = 0,
                .senderUid = 1010,
                .aidlMethodName = String16("MethodName11"),
        });
    }

    // First push: Service is unavailable
    EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
            .Times(1)
            .WillOnce(Return(nullptr));
    // localBinder() shouldn't be called if service is null in aggregateBinderSpamLocked
    EXPECT_CALL(*mockStatsService, localBinder()).Times(0);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);

    pusher.pushLocked(spamCallData1, timeSec1);
    Mock::VerifyAndClearExpectations(mockServiceManager.get());
    Mock::VerifyAndClearExpectations(mockStatsService.get());

    // Second push: Service becomes available. Advance time beyond service check timeout.
    std::vector<BinderCallData> spamCallData2; // Empty data for the second call
    int64_t timeSec2 = timeSec1 + 6;

    EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
            .Times(1)
            .WillOnce(Return(IInterface::asBinder(mockStatsService)));
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1); // Called when service is not null
    auto expectedStats = createExpectedSpamStats(spamCallData1[0], 1, 0);
    EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);

    pusher.pushLocked(spamCallData2, timeSec2);
}

TEST_F(BinderStatsPusherTest, sizeOfStruct) {
#ifdef __LP64__
    EXPECT_EQ(sizeof(android::BinderCallData), 40);
#else
    EXPECT_EQ(sizeof(android::BinderCallData), 32);
#endif
}
// --- Latency Tests ---

TEST_F(BinderStatsPusherTest, AggregateLatencyNoLatencyBelowThreshold) {
    std::vector<BinderCallData> data;
    int64_t currentTimeSec = 14;
    // Create data within the delay window
    for (int i = 0; i < 5; ++i) { // Less than kLatencyCountFirstWatermark (10)
        data.push_back({
                .interfaceDescriptor = String16("ILatencyFoo"),
                .transactionCode = 1,
                .startTimeNanos =
                        (currentTimeSec - kLatencyAggregationWindowSec + 1) * 1000000000LL,
                .endTimeNanos = (currentTimeSec - kLatencyAggregationWindowSec + 1) * 1000000000LL +
                        1000000 /* 1ms */,
                .senderUid = 2001,
                .aidlMethodName = String16("MethodName1"),
        });
    }

    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    pusher.pushLocked(data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyOneSecondLatency) {
    std::vector<BinderCallData> data;
    int64_t callTimeNanos = 2'000'000'000LL; // 2s
    int64_t currentTimeSec =
            (callTimeNanos / 1000'000'000LL) + kLatencyAggregationWindowSec + 1; // 2 + 5 + 1 = 8s
    int64_t totalDurationMicros = 0;

    // Create enough data in the *same second* to trigger latency reporting
    for (int i = 0; i < 15; ++i) {              // More than kLatencyCountFirstWatermark (10)
        int64_t durationMicros = (i + 1) * 100; // e.g. 100us, 200us ..
        data.push_back({
                .interfaceDescriptor = String16("ILatencyBar"),
                .transactionCode = 2,
                .startTimeNanos = callTimeNanos,
                .endTimeNanos = callTimeNanos + durationMicros * 1000,
                .senderUid = 2002,
                .aidlMethodName = String16("MethodName2"),
        });
        totalDurationMicros += durationMicros;
    }

    auto expectedStats = createExpectedLatencyStats(data[0], 15, totalDurationMicros, 1, 0);
    EXPECT_CALL(*mockStatsService, reportCallStats(ElementsAre(CallStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    pusher.pushLocked(data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyDelayedLatency) {
    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;

    // Create latency data within the aggregation window
    for (int i = 0; i < 15; ++i) {
        data.push_back({
                .interfaceDescriptor = String16("ILatencyBaz"),
                .transactionCode = 3,
                .startTimeNanos = currentTimeNanos - 1000'000'000,
                .endTimeNanos = currentTimeNanos - 1000'000'000 + 500000 /* 0.5ms */,
                .senderUid = 2003,
                .aidlMethodName = String16("MethodName3"),
        });
    }

    // Expect no calls, data is delayed
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
    pusher.pushLocked(data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyProcessesDelayedDataOnSubsequentCall) {
    std::vector<BinderCallData> callData1;
    int64_t callTimeSec1 = 10;
    int64_t latencyDataTimeNanos = (callTimeSec1 - 2) * 1000'000'000LL; // 8s, will be delayed
    int64_t totalDurationMicros1 = 0;

    for (int i = 0; i < 15; ++i) {
        int64_t durationMicros = (i + 1) * 150;
        callData1.push_back({
                .interfaceDescriptor = String16("ILatencyDelayed"),
                .transactionCode = 4,
                .startTimeNanos = latencyDataTimeNanos,
                .endTimeNanos = latencyDataTimeNanos + durationMicros * 1000,
                .senderUid = 2004,
                .aidlMethodName = String16("MethodName4"),
        });
        totalDurationMicros1 += durationMicros;
    }

    // First push: data should be buffered as it's too recent
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
    pusher.pushLocked(callData1, callTimeSec1);

    // Second push: advance time so the previous data is now outside the aggregation window
    std::vector<BinderCallData> callData2;                                    // Can be empty
    int64_t call2_time_sec = callTimeSec1 + kLatencyAggregationWindowSec + 1; // 10 + 5 + 1 = 16s

    auto expectedStats = createExpectedLatencyStats(callData1[0], 15, totalDurationMicros1, 1, 0);
    EXPECT_CALL(*mockStatsService, reportCallStats(ElementsAre(CallStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    pusher.pushLocked(callData2, call2_time_sec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyMultipleSeconds) {
    std::vector<BinderCallData> data;
    int64_t firstCallSecondNanos = 2'000'000'000LL;  // 2s
    int64_t secondCallSecondNanos = 3'000'000'000LL; // 3s
    int64_t currentTimeSec = (secondCallSecondNanos / 1000'000'000LL) +
            kLatencyAggregationWindowSec + 1; // 3 + 5 + 1 = 9s
    int64_t totalDurationMicros = 0;

    // Data for the first second
    for (int i = 0; i < 12; ++i) { // 12 calls
        int64_t durationMicros = (i + 1) * 100;
        data.push_back({
                .interfaceDescriptor = String16("ILatencyMultiSec"),
                .transactionCode = 5,
                .startTimeNanos = firstCallSecondNanos,
                .endTimeNanos = firstCallSecondNanos + durationMicros * 1000,
                .senderUid = 2005,
                .aidlMethodName = String16("MethodName5"),
        });
        totalDurationMicros += durationMicros;
    }
    // Data for the second second
    for (int i = 0; i < 8; ++i) { // 8 calls
        int64_t durationMicros = (i + 1) * 120;
        // Use the same UID, code, desc for aggregation
        data.push_back({
                .interfaceDescriptor = String16("ILatencyMultiSec"),
                .transactionCode = 5,
                .startTimeNanos = secondCallSecondNanos,
                .endTimeNanos = secondCallSecondNanos + durationMicros * 1000,
                .senderUid = 2005,
                .aidlMethodName = String16("MethodName5"),
        });
        totalDurationMicros += durationMicros;
    }

    // Expect one atom representing aggregated data across 2 seconds
    // Total calls = 12 + 8 = 20
    // secondsWithAtLeast10Calls = 1 (only the first second has >= 10 calls)
    // secondsWithAtLeast50Calls = 0
    auto expectedStats = createExpectedLatencyStats(data[0], 20, totalDurationMicros, 1, 0);
    EXPECT_CALL(*mockStatsService, reportCallStats(ElementsAre(CallStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    pusher.pushLocked(data, currentTimeSec);
}