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

#include <chrono>
#include <initializer_list>
#include <map>
#include <string>

#include <fmt/chrono.h>  // NOLINT(misc-include-cleaner)
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>

#include "test_framework/core/DisplayConfiguration.h"
#include "test_framework/core/TimeInterval.h"
#include "test_framework/hwc3/MultiDisplayRefreshEventGenerator.h"
#include "test_framework/hwc3/SingleDisplayRefreshEventGenerator.h"
#include "test_framework/hwc3/SingleDisplayRefreshSchedule.h"

namespace android::surfaceflinger::tests::end2end {
namespace {

using TimePoint = std::chrono::steady_clock::time_point;
using TimeInterval = test_framework::core::TimeInterval;
using SingleDisplayRefreshSchedule = test_framework::hwc3::SingleDisplayRefreshSchedule;
using SingleDisplayRefreshEventGenerator = test_framework::hwc3::SingleDisplayRefreshEventGenerator;
using MultiDisplayRefreshEventGenerator = test_framework::hwc3::MultiDisplayRefreshEventGenerator;

// Helper to compute a monotonic clock time point from a integer value for the count of
// nanoseconds since the epoch.
constexpr auto operator""_ticks(unsigned long long value) -> TimePoint {
    return TimePoint{std::chrono::nanoseconds(value)};
}

constexpr auto operator""_ms(unsigned long long value) -> std::chrono::milliseconds {
    return std::chrono::milliseconds(value);
}

auto toString(const SingleDisplayRefreshEventGenerator::GenerateResult& result) -> std::string {
    return fmt::format("Result{{nextRefreshAt: {}, events:\n     {}\n  }}",
                       result.nextRefreshAt.time_since_epoch(),
                       fmt::join(result.events, ",\n     "));
}

auto toString(const MultiDisplayRefreshEventGenerator::GenerateResult& result) -> std::string {
    return fmt::format("Result{{nextRefreshAt: {}, events:\n     {}\n  }}",
                       result.nextRefreshAt.time_since_epoch(),
                       fmt::join(result.events, ",\n     "));
}

TEST(SingleDisplayRefreshSchedule, NextRefreshEventComputesNext) {
    struct TestCase {
        TimePoint input;
        TimePoint expected;
    };

    // The implementation assumes that it is called with increasing input clock times.
    const auto testCases = std::initializer_list<TestCase>{
            // Clock times 1ms apart should always round up to an event time after the input time.
            // These values are to show that the rounding occurs to the 5ms refresh period.
            {.input = 12'000'000_ticks, .expected = 17'000'000_ticks},
            {.input = 13'000'000_ticks, .expected = 17'000'000_ticks},
            {.input = 14'000'000_ticks, .expected = 17'000'000_ticks},
            {.input = 15'000'000_ticks, .expected = 17'000'000_ticks},
            {.input = 16'000'000_ticks, .expected = 17'000'000_ticks},
            {.input = 17'000'000_ticks, .expected = 22'000'000_ticks},
            {.input = 18'000'000_ticks, .expected = 22'000'000_ticks},
            {.input = 19'000'000_ticks, .expected = 22'000'000_ticks},
            {.input = 20'000'000_ticks, .expected = 22'000'000_ticks},
            {.input = 21'000'000_ticks, .expected = 22'000'000_ticks},  // ^ Past events
            {.input = 22'000'000_ticks, .expected = 27'000'000_ticks},  // = At Base tme
            {.input = 23'000'000_ticks, .expected = 27'000'000_ticks},  // v Future events
            {.input = 24'000'000_ticks, .expected = 27'000'000_ticks},
            {.input = 25'000'000_ticks, .expected = 27'000'000_ticks},
            {.input = 26'000'000_ticks, .expected = 27'000'000_ticks},
            {.input = 27'000'000_ticks, .expected = 32'000'000_ticks},
            {.input = 28'000'000_ticks, .expected = 32'000'000_ticks},
            {.input = 29'000'000_ticks, .expected = 32'000'000_ticks},
            {.input = 30'000'000_ticks, .expected = 32'000'000_ticks},
            {.input = 31'000'000_ticks, .expected = 32'000'000_ticks},

            // Past clock times near the rounding point should correctly round up.
            {.input = 11'999'000_ticks, .expected = 12'000'000_ticks},
            {.input = 12'000'000_ticks, .expected = 17'000'000_ticks},
            {.input = 12'001'000_ticks, .expected = 17'000'000_ticks},

            // clock times near the base time should correctly round up.
            {.input = 21'999'000_ticks, .expected = 22'000'000_ticks},
            {.input = 22'000'000_ticks, .expected = 27'000'000_ticks},
            {.input = 22'001'000_ticks, .expected = 27'000'000_ticks},

            // Future clock times near the rounding point should correctly round up.
            {.input = 31'999'000_ticks, .expected = 32'000'000_ticks},
            {.input = 32'000'000_ticks, .expected = 37'000'000_ticks},
            {.input = 32'001'000_ticks, .expected = 37'000'000_ticks},

    };

    // Set up a fixed refresh schedule where events happen every 5ms, anchored to a clock time of
    // 22ms. So refresh events happen at [..., 12ms, 17ms, 22ms, 27ms, 32ms, ...]
    static constexpr SingleDisplayRefreshSchedule schedule{.base = 22'000'000_ticks,
                                                           .period = 5_ms};

    for (const auto& testCase : testCases) {
        const auto actual = schedule.nextEvent(testCase.input);
        if (actual != testCase.expected) {
            FAIL() << fmt::format("For input: {} expected: {}, actual: {})",
                                  testCase.input.time_since_epoch(),
                                  testCase.expected.time_since_epoch(), actual.time_since_epoch());
        }
    }
}

TEST(SingleDisplayRefreshEventGenerator, GeneratesEvents) {
    using GenerateResult = SingleDisplayRefreshEventGenerator::GenerateResult;
    struct TestCase {
        std::string name;
        SingleDisplayRefreshEventGenerator generator;
        TimeInterval input;
        GenerateResult expected;
    };
    // The implementation assumes that it is called with increasing input clock times.
    const auto testCases = std::initializer_list<TestCase>{
            {
                    // The first request should yield a single event for the first
                    // refresh before the input time.
                    .name = "First",
                    .generator = {.id = 0, .schedule = {.base = 22'000'000_ticks, .period = 5_ms}},
                    .input = {.begin = TimePoint::min(), .end = 10'000'000_ticks},
                    .expected = {.nextRefreshAt = 12'000'000_ticks,
                                 .events = {{
                                         .displayId = 0,
                                         .expectedAt = 7'000'000_ticks,
                                         .expectedPeriod = 5_ms,
                                         .receivedAt = 10'000'000_ticks,
                                 }}},
            },
            {
                    // A typical incremental update would just yield a single event for one refresh
                    // in the elapsed time since the last time the generator is invoked and the
                    // input time.
                    .name = "Incremental",
                    .generator = {.id = 0, .schedule = {.base = 22'000'000_ticks, .period = 5_ms}},
                    .input = {.begin = 10'000'000_ticks, .end = 12'100'000_ticks},
                    .expected = {.nextRefreshAt = 17'000'000_ticks,
                                 .events = {{
                                         .displayId = 0,
                                         .expectedAt = 12'000'000_ticks,
                                         .expectedPeriod = 5_ms,
                                         .receivedAt = 12'100'000_ticks,
                                 }}},
            },
            {
                    // If the generator is invoked early, no events should be generated, but the
                    // next refresh time should still refer to the correct time for the next event.
                    .name = "Early",
                    .generator = {.id = 0, .schedule = {.base = 22'000'000_ticks, .period = 5_ms}},
                    .input = {.begin = 12'100'000_ticks, .end = 16'100'000_ticks},
                    .expected = {.nextRefreshAt = 17'000'000_ticks, .events = {}},
            },
            {
                    // If the generator is invoked with an input time more than one period in the
                    // future (but not too far), the returned interval will cover all the missed
                    // events.
                    .name = "Small jump",
                    .generator = {.id = 0, .schedule = {.base = 22'000'000_ticks, .period = 5_ms}},
                    .input = {.begin = 16'100'000_ticks, .end = 40'000'000_ticks},
                    .expected = {.nextRefreshAt = 42'000'000_ticks,
                                 .events = {{
                                                    .displayId = 0,
                                                    .expectedAt = 17'000'000_ticks,
                                                    .expectedPeriod = 5_ms,
                                                    .receivedAt = 40'000'000_ticks,
                                            },
                                            {
                                                    .displayId = 0,
                                                    .expectedAt = 22'000'000_ticks,
                                                    .expectedPeriod = 5_ms,
                                                    .receivedAt = 40'000'000_ticks,
                                            },
                                            {
                                                    .displayId = 0,
                                                    .expectedAt = 27'000'000_ticks,
                                                    .expectedPeriod = 5_ms,
                                                    .receivedAt = 40'000'000_ticks,
                                            },
                                            {
                                                    .displayId = 0,
                                                    .expectedAt = 32'000'000_ticks,
                                                    .expectedPeriod = 5_ms,
                                                    .receivedAt = 40'000'000_ticks,
                                            },
                                            {
                                                    .displayId = 0,
                                                    .expectedAt = 37'000'000_ticks,
                                                    .expectedPeriod = 5_ms,
                                                    .receivedAt = 40'000'000_ticks,
                                            }}},
            },
            {
                    // If the generator is invoked with an input time substantially in the future,
                    // rather than returning an interval with all the missed events, the interval
                    // will only contain the most recent event.
                    .name = "Large jump",
                    .generator = {.id = 0, .schedule = {.base = 22'000'000_ticks, .period = 5_ms}},
                    .input = {.begin = 40'000'000_ticks, .end = 4'002'100'000_ticks},
                    .expected = {.nextRefreshAt = 4'007'000'000_ticks,
                                 .events = {{
                                         .displayId = 0,
                                         .expectedAt = 4'002'000'000_ticks,
                                         .expectedPeriod = 5_ms,
                                         .receivedAt = 4'002'100'000_ticks,
                                 }}},
            },
    };

    for (const auto& testCase : testCases) {
        const auto actual = testCase.generator.generateEventsFor(testCase.input);
        if (actual != testCase.expected) {
            FAIL() << fmt::format("Case name: {}: input: {},\n  expected: {},\n  actual: {})",
                                  testCase.name, toString(testCase.input),
                                  toString(testCase.expected), toString(actual));
        }
    }
}

TEST(MultiDisplayRefreshEventGenerator, GeneratesEvents) {
    using DisplayId = test_framework::core::DisplayConfiguration::Id;
    using GenerateResult = MultiDisplayRefreshEventGenerator::GenerateResult;
    struct TestCase {
        std::string name;
        std::map<DisplayId, SingleDisplayRefreshSchedule> displays;
        TimeInterval input;
        GenerateResult expected;
    };
    // The implementation assumes that it is called with increasing input clock times.
    const auto testCases = std::initializer_list<TestCase>{
            {
                    .name = "2x @5ms in-phase",
                    .displays = {{0, {.base = 0_ticks, .period = 5_ms}},
                                 {1, {.base = 0_ticks, .period = 5_ms}}},
                    .input = {.begin = 0'000'000_ticks, .end = 20'100'000_ticks},
                    .expected =
                            {
                                    .nextRefreshAt = 25'000'000_ticks,
                                    .events = {{
                                                       .displayId = 0,
                                                       .expectedAt = 5'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 0,
                                                       .expectedAt = 10'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 0,
                                                       .expectedAt = 15'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 0,
                                                       .expectedAt = 20'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 1,
                                                       .expectedAt = 5'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 1,
                                                       .expectedAt = 10'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 1,
                                                       .expectedAt = 15'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 1,
                                                       .expectedAt = 20'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               }},
                            },
            },
            {
                    .name = "2x @5ms out-of-phase",
                    .displays =
                            {
                                    {0, {.base = 0'000'000_ticks, .period = 5_ms}},
                                    {1, {.base = 2'500'000_ticks, .period = 5_ms}},
                            },
                    .input = {.begin = 0'000'000_ticks, .end = 20'100'000_ticks},
                    .expected =
                            {
                                    .nextRefreshAt = 22'500'000_ticks,
                                    .events = {{
                                                       .displayId = 0,
                                                       .expectedAt = 5'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 0,
                                                       .expectedAt = 10'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 0,
                                                       .expectedAt = 15'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 0,
                                                       .expectedAt = 20'000'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 1,
                                                       .expectedAt = 2'500'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 1,
                                                       .expectedAt = 7'500'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 1,
                                                       .expectedAt = 12'500'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               },
                                               {
                                                       .displayId = 1,
                                                       .expectedAt = 17'500'000_ticks,
                                                       .expectedPeriod = 5_ms,
                                                       .receivedAt = 20'100'000_ticks,
                                               }},
                            },
            },
    };

    for (const auto& testCase : testCases) {
        MultiDisplayRefreshEventGenerator generator;
        for (const auto& [displayId, schedule] : testCase.displays) {
            generator.addDisplay(displayId, schedule);
        }

        const auto actual = generator.generateEventsFor(testCase.input);

        if (actual != testCase.expected) {
            FAIL() << fmt::format("Case name: {}: input: {},\n  expected: {},\n  actual: {})",
                                  testCase.name, toString(testCase.input),
                                  toString(testCase.expected), toString(actual));
        }
    }
}

}  // namespace
}  // namespace android::surfaceflinger::tests::end2end
