/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <utils/Timers.h>

namespace android {

class QtiDolphinWrapper {
public:
    static bool sQuickTouch; // the flag to mark QuickTouch V1 state
    static QtiDolphinWrapper* qtiGetDolphinWrapper();
    static QtiDolphinWrapper* qtiGetInstanceForGame();
    QtiDolphinWrapper();
    ~QtiDolphinWrapper();
    void (*qtiDolphinAppInit)() = nullptr;
    void (*qtiDolphinSetVsyncTime)(nsecs_t vsyncTimestamp) = nullptr;
    bool (*qtiDolphinSmartTouchActive)() = nullptr;
    void (*qtiDolphinQueueBuffer)(bool) = nullptr;
    void (*qtiDolphinFilterBuffer)(bool& isAutoTimestamp, nsecs_t& desiredPresentTime,
                                   uint32_t& flags) = nullptr;

private:
    void *mQtiDolphinHandle = nullptr;
    static QtiDolphinWrapper* sInstance;
};

} // namespace android
