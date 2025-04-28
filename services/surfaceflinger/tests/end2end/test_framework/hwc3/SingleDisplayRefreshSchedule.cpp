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
#include <string>

#include <fmt/chrono.h>  // NOLINT(misc-include-cleaner)
#include <fmt/format.h>

#include "test_framework/hwc3/SingleDisplayRefreshSchedule.h"

namespace android::surfaceflinger::tests::end2end::test_framework::hwc3 {

[[nodiscard]] auto SingleDisplayRefreshSchedule::nextEvent(TimePoint forTime) const -> TimePoint {
    constexpr auto zero = TimePoint::duration(0);
    constexpr auto one = TimePoint::duration(1);
    auto delta = (forTime - base);

    // Ensure we will round "up" to the next event time. When forTime is greater than or equal
    // to the base time (equivalently delta >=0), the modulus operation rounds down towards
    // zero, and we just need to add the period to round to the next multiple. However when it
    // is less, and delta is negative, the rounding is up towards zero, except at the negative
    // multiples. When it is less, we instead need to add one to achieve the achieve the same
    // result, since the modulus done next rounds towards zero.
    delta += ((delta >= zero) ? period : one);

    // Compute first event time after forTime.
    return base + delta - delta % period;
}

auto toString(const SingleDisplayRefreshSchedule& schedule) -> std::string {
    return fmt::format("SingleDisplayRefreshSchedule{{base: {}, period: {}}}",
                       schedule.base.time_since_epoch(), schedule.period);
}

}  // namespace android::surfaceflinger::tests::end2end::test_framework::hwc3
