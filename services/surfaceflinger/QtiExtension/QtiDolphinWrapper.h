/* Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <utils/Timers.h>
#include <ui/Rect.h>

namespace android {

namespace surfaceflingerextension {

class QtiDolphinWrapper {
public:
    QtiDolphinWrapper(int width, int height);
    ~QtiDolphinWrapper();
    bool (*qtiDolphinInit)(int width, int height) = nullptr;
    void (*qtiDolphinSetVsyncPeriod)(nsecs_t vsyncPeriod) = nullptr;
    void (*qtiDolphinTrackBufferIncrement)(const char* name, int32_t layerId, bool isAutoTimestamp,
                                           uint32_t flags, nsecs_t desiredPresentTime) = nullptr;
    void (*qtiDolphinTrackBufferDecrement)(const char* name, int32_t layerId, int counter,
                                           const Rect& bounds, bool focused,
                                           bool isVisible) = nullptr;
    void (*qtiDolphinTrackVsyncSignal)() = nullptr;
    void (*qtiDolphinUnblockPendingBuffer)() = nullptr;
    bool (*qtiDolphinIsTargetFpsActive)() = nullptr;
    void (*qtiDolphinNotifyGpuFenceUnsignaled)(int fenceFd, int32_t layerId) = nullptr;
    void (*qtiDolphinTrackLatchUnsignaledGpuFence)(int fenceFd, int32_t layerId) = nullptr;
    void (*qtiDolphinFlushLatchUnsignaledGpuFences)() = nullptr;

private:
    void *mQtiDolphinHandle = nullptr;
};

} // namespace surfaceflingerextension
} // namespace android
