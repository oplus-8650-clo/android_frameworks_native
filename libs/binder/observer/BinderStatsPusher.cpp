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
#include <algorithm>
#include <charconv>
#include "../BuildFlags.h"
#include "../JvmUtils.h"
#include "BinderStatsUtils.h"

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

void BinderStatsPusher::appendCallStatsToReportLocked(const BinderCallData& chunk,
                                                      const CallAggregation& agg) {
    if (agg.callsWithLatency > 0 || agg.cpuTimeCount > 0) {
        mCallStats.emplace_back(os::binder::BinderCallsStats());
        auto& callsStats = mCallStats.back();
        callsStats.clientUid = static_cast<int32_t>(chunk.senderUid);
        callsStats.interfaceDescriptor = chunk.interfaceDescriptor;
        callsStats.aidlMethod = chunk.aidlMethodName;
        callsStats.callCount = static_cast<int32_t>(agg.callsWithLatency);
        callsStats.durationSumMicros = agg.durationSumMicros;
        if (kBinderObserverV2Enabled) {
            callsStats.callDurationSumSquaredMicros = agg.callDurationSumSquaredMicros;
        }
        callsStats.secondsWithAtLeast10Calls = agg.secondsWithAtLeast10Calls;
        callsStats.secondsWithAtLeast50Calls = agg.secondsWithAtLeast50Calls;
        callsStats.cpuTimeCount = static_cast<int32_t>(agg.cpuTimeCount);
        callsStats.cpuTimeSumMicros = agg.cpuTimeSumMicros;
        callsStats.cpuTimeSumSquaredMicros = agg.cpuTimeSumSquaredMicros;
    }
}

__attribute__((no_sanitize("signed-integer-overflow"))) void
BinderStatsPusher::aggregateStatsLocked(const int64_t nowSec) {
    auto service = getBinderStatsServiceLocked(nowSec);
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
    auto& callsBuffer = mCallsAggregation.getBufferLocked();

    for (auto outerIt = callsBuffer.begin(); outerIt != callsBuffer.end();
         /* no increment */) {
        int32_t secondsWithAtLeast125Calls = 0;
        int32_t secondsWithAtLeast250Calls = 0;
        uint32_t peakCallCountPerSecond = 0;

        CallAggregation agg;

        for (auto innerIt = outerIt->second.begin(); innerIt != outerIt->second.end();
             /* no increment */) {
            // Check if the buffer period has passed.
            int64_t startTimeSec = innerIt->first;
            if (nowSec - startTimeSec >= kAggregationWindowSec) {
                uint32_t totalCalls = innerIt->second.totalCalls;
                if (totalCalls > kSpamFirstWatermark) {
                    secondsWithAtLeast125Calls++;
                    if (totalCalls > kSpamSecondWatermark) {
                        secondsWithAtLeast250Calls++;
                    }
                    peakCallCountPerSecond = std::max(peakCallCountPerSecond, totalCalls);
                }
                if (innerIt->second.callsWithLatency > 0) {
                    agg.callsWithLatency += innerIt->second.callsWithLatency;
                    agg.durationSumMicros += innerIt->second.durationSumMicros;
                    agg.callDurationSumSquaredMicros +=
                            innerIt->second.callDurationSumSquaredMicros;
                    if (innerIt->second.callsWithLatency >= kLatencyCountFirstWatermark) {
                        agg.secondsWithAtLeast10Calls++;
                        if (innerIt->second.callsWithLatency >= kLatencyCountSecondWatermark) {
                            agg.secondsWithAtLeast50Calls++;
                        }
                    }
                }
                if (innerIt->second.cpuTimeCount > 0) {
                    agg.cpuTimeCount += innerIt->second.cpuTimeCount;
                    agg.cpuTimeSumMicros += innerIt->second.cpuTimeSumMicros;
                    agg.cpuTimeSumSquaredMicros += innerIt->second.cpuTimeSumSquaredMicros;
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
        appendCallStatsToReportLocked(outerIt->first, agg);
        agg.reset();

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
            spamStats.aidlMethod = datum.aidlMethodName;
            spamStats.secondsWithAtLeast125Calls = secondsWithAtLeast125Calls;
            spamStats.secondsWithAtLeast250Calls = secondsWithAtLeast250Calls;
            spamStats.peakCallCountPerSecond = peakCallCountPerSecond;
        }

        if (outerIt->second.empty()) {
            outerIt = callsBuffer.erase(outerIt);
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

void BinderStatsPusher::AddCallDataFunctor::operator()(BinderCallData&& b) const {
    mPusher->mCallsAggregation.addCallStatsLocked(std::move(b));
}

void BinderStatsPusher::pushLocked(const int64_t nowSec) {
    aggregateStatsLocked(nowSec);
}

BinderStatsPusher::AddCallDataFunctor BinderStatsPusher::getAddCallDataToBufferLockedFunction() {
    return AddCallDataFunctor(this);
}

} // namespace android
