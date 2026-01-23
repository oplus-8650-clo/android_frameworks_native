/* Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <utils/Timers.h>

namespace android {

namespace surfaceflingerextension {

class QtiDolphinWrapper {
public:
    QtiDolphinWrapper(int width, int height);
    ~QtiDolphinWrapper();
    bool (*qtiDolphinInit)(int width, int height) = nullptr;
    void (*qtiDolphinSetVsyncPeriod)(nsecs_t vsyncPeriod) = nullptr;
    void (*qtiDolphinTrackBufferIncrement)(const char* name, bool isAutoTimestamp,
                                           uint32_t flags, nsecs_t desiredPresentTime) = nullptr;
    void (*qtiDolphinTrackBufferDecrement)(const char* name, int counter, int width, int height) = nullptr;
    void (*qtiDolphinTrackVsyncSignal)() = nullptr;
    void (*qtiDolphinUnblockPendingBuffer)() = nullptr;
    bool (*qtiDolphinIsTargetFpsActive)() = nullptr;

private:
    void *mQtiDolphinHandle = nullptr;
};

} // namespace surfaceflingerextension
} // namespace android
