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
#define LOG_TAG "libbinder.BinderStatsPusher"

#include "BinderStatsPusher.h"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/os/binder/BinderCallsStats.h>
#include <android/os/binder/BinderSpamStats.h>
#include <android/os/binder/IBinderStatsConsumerService.h>
#include <binder/Functional.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <utils/SystemClock.h>
#include <charconv>
#include "BinderStatsUtils.h"
#include "JvmUtils.h"

namespace android {
[[clang::no_destroy]] static const StaticString16 kBinderStatsServiceName(u"binder_stats_consumer");

sp<os::binder::IBinderStatsConsumerService> BinderStatsPusher::getBinderStatsServiceLocked(
        const int64_t nowSec) {
    // When this is removed, the device does not get past the boot animation
    // TODO(b/299356196): This might result in dropped stats for high usage apps like
    // servicemanager.
    if (!mLastServiceCheckSucceeded && (mServiceCheckTimeSec + kCheckServiceTimeoutSec > nowSec)) {
        return nullptr;
    };
    auto sm = defaultServiceManager();
    if (!sm) {
        LOG_ALWAYS_FATAL("defaultServiceManager() returned nullptr.");
    }
    auto service = interface_cast<os::binder::IBinderStatsConsumerService>(
            defaultServiceManager()->checkService(kBinderStatsServiceName));
    mServiceCheckTimeSec = nowSec;
    if (service == nullptr) {
        mLastServiceCheckSucceeded = false;
    } else {
        mLastServiceCheckSucceeded = true;
    }
    return service;
}

String16 BinderStatsPusher::convertTxnCodeToString(uint32_t txnCode) {
    // Size: 1 for '#', 10 for max uint32_t digits.
    char buffer[11];
    buffer[0] = '#';
    auto result = std::to_chars(buffer + 1, buffer + std::size(buffer), txnCode);
    if (result.ec != std::errc()) {
        LOG_FATAL("Error converting txnCode to String16");
    }
    // Construct the String16 from the char array and its length.
    return String16(buffer, result.ptr - buffer);
}

__attribute__((no_sanitize("signed-integer-overflow"))) void
BinderStatsPusher::aggregateStatsLocked(const std::vector<BinderCallData>& data,
                                        const sp<os::binder::IBinderStatsConsumerService>& service,
                                        const int64_t nowSec) {
    for (const auto& datum : data) {
        int64_t startTimeSec = datum.startTimeNanos / 1000'000'000;
        // Check if the buffer period has passed.
        auto [it, inserted] = mStatsBuffer[datum].try_emplace(startTimeSec, AidlTargetMetrics());
        it->second.totalCalls++;
        if (datum.hasLatencyData()) {
            it->second.callsWithLatency++;
            it->second.durationSumMicros += (datum.endTimeNanos - datum.startTimeNanos) / 1000;
        }
    }
    if (!service) return;
    // Ensure that if this is a local binder and this thread isn't attached
    // to the VM then skip pushing. This is required since StatsBootstrap is
    // a Java service and needs a JNI interface to be called from native code.
    bool isProcessSystemServer = IInterface::asBinder(service)->localBinder() != nullptr;
    if (isProcessSystemServer) {
        if (!isThreadAttachedToJVM()) {
            return;
        }
    }
    // Clear calling identity if this is called from system server. This
    // will allow libStatsBootstrap to verify calling uid correctly.
    int64_t callingIdentity;
    if (isProcessSystemServer) {
        callingIdentity = IPCThreadState::self()->clearCallingIdentity();
    }
    auto callingIdentityGuard = binder::impl::make_scope_guard([&] {
        if (isProcessSystemServer) {
            IPCThreadState::self()->restoreCallingIdentity(callingIdentity);
        }
    });

    for (auto outerIt = mStatsBuffer.begin(); outerIt != mStatsBuffer.end();
         /* no increment */) {
        int32_t secondsWithAtLeast125Calls = 0;
        int32_t secondsWithAtLeast250Calls = 0;

        uint64_t callsWithLatency = 0;
        uint64_t durationSumMicros = 0;
        uint32_t secondsWithAtLeast10Calls = 0;
        uint32_t secondsWithAtLeast50Calls = 0;
        for (auto innerIt = outerIt->second.begin(); innerIt != outerIt->second.end();
             /* no increment */) {
            // Check if the buffer period has passed.
            int64_t startTimeSec = innerIt->first;
            if (nowSec - startTimeSec >= kAggregationWindowSec) {
                uint64_t totalCalls = innerIt->second.totalCalls;
                if (totalCalls > kSpamFirstWatermark) {
                    secondsWithAtLeast125Calls++;
                    if (totalCalls > kSpamSecondWatermark) {
                        secondsWithAtLeast250Calls++;
                    }
                }
                if (innerIt->second.callsWithLatency > 0) {
                    callsWithLatency += innerIt->second.callsWithLatency;
                    durationSumMicros += innerIt->second.durationSumMicros;
                    if (innerIt->second.callsWithLatency >= kLatencyCountFirstWatermark) {
                        secondsWithAtLeast10Calls++;
                        if (innerIt->second.callsWithLatency >= kLatencyCountSecondWatermark) {
                            secondsWithAtLeast50Calls++;
                        }
                    }
                }
                // Erase the datum from the buffer so we don't aggregate it again
                innerIt = outerIt->second.erase(innerIt);
            } else {
                ++innerIt;
            }
        }
        if (mCallStats.size() >= kMaxStatsCount) {
            service->reportCallStats(mCallStats);
            mCallStats.clear();
        }
        if (callsWithLatency > 0) {
            auto datum = outerIt->first;
            mCallStats.emplace_back(os::binder::BinderCallsStats());
            auto& callsStats = mCallStats.back();
            callsStats.clientUid = static_cast<int32_t>(datum.senderUid);
            callsStats.interfaceDescriptor = datum.interfaceDescriptor;
            // TODO(b/299356196): use actual method name when available.
            callsStats.aidlMethod = convertTxnCodeToString(datum.transactionCode);
            callsStats.callCount = static_cast<int64_t>(callsWithLatency);
            callsStats.durationSumMicros = static_cast<int64_t>(durationSumMicros);
            callsStats.secondsWithAtLeast10Calls = secondsWithAtLeast10Calls;
            callsStats.secondsWithAtLeast50Calls = secondsWithAtLeast50Calls;
        }

        if (mSpamStats.size() >= kMaxStatsCount) {
            service->reportSpamStats(mSpamStats);
            mSpamStats.clear();
        }
        if (secondsWithAtLeast125Calls > 0) {
            auto datum = outerIt->first;
            mSpamStats.emplace_back(os::binder::BinderSpamStats());
            auto& spamStats = mSpamStats.back();
            spamStats.clientUid = static_cast<int32_t>(datum.senderUid);
            spamStats.interfaceDescriptor = datum.interfaceDescriptor;
            // TODO(b/299356196): replace with actual method name.
            spamStats.aidlMethod = convertTxnCodeToString(datum.transactionCode);
            spamStats.secondsWithAtLeast125Calls = secondsWithAtLeast125Calls;
            spamStats.secondsWithAtLeast250Calls = secondsWithAtLeast250Calls;
        }

        if (outerIt->second.empty()) {
            outerIt = mStatsBuffer.erase(outerIt);
        } else {
            ++outerIt;
        }
    }
    if (!mCallStats.empty()) {
        service->reportCallStats(mCallStats);
        mCallStats.clear();
    }
    if (!mSpamStats.empty()) {
        service->reportSpamStats(mSpamStats);
        mSpamStats.clear();
    }
}

void BinderStatsPusher::pushLocked(const std::vector<BinderCallData>& data, const int64_t nowSec) {
    auto service = getBinderStatsServiceLocked(nowSec);
    aggregateStatsLocked(data, service, nowSec);
}

} // namespace android
