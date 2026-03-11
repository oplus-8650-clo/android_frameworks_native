
/* Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <dlfcn.h>

#include <log/log.h>

#include "QtiDolphinWrapper.h"


namespace android::surfaceflingerextension {

QtiDolphinWrapper::QtiDolphinWrapper(int width, int height) {
    mQtiDolphinHandle = dlopen("libdolphin.so", RTLD_NOW);
    if (!mQtiDolphinHandle) {
        ALOGW("Unable to open libdolphin.so: %s.", dlerror());
    } else {
        qtiDolphinInit = (bool (*) (int, int))dlsym(mQtiDolphinHandle, "dolphinInit");
        qtiDolphinSetVsyncPeriod = (void (*) (nsecs_t)) dlsym(mQtiDolphinHandle,
                "dolphinSetVsyncPeriod");
        qtiDolphinTrackBufferIncrement =
                (void (*) (const char*, int32_t, bool, uint32_t, nsecs_t))dlsym(
                mQtiDolphinHandle, "dolphinTrackBufferIncrement");
        qtiDolphinTrackBufferDecrement =
                (void (*) (const char*, int32_t, int, const Rect&, bool, bool))dlsym(
                mQtiDolphinHandle, "dolphinTrackBufferDecrement");
        qtiDolphinTrackVsyncSignal = (void (*) ())dlsym(mQtiDolphinHandle,
                "dolphinTrackVsyncSignal");
        qtiDolphinUnblockPendingBuffer = (void (*) ())dlsym(mQtiDolphinHandle,
                "dolphinUnblockPendingBuffer");
        qtiDolphinIsTargetFpsActive = (bool (*) ())dlsym(mQtiDolphinHandle,
                "dolphinIsTargetFpsActive");
        bool functionsFound = qtiDolphinInit && qtiDolphinSetVsyncPeriod &&
                              qtiDolphinTrackBufferIncrement && qtiDolphinTrackBufferDecrement &&
                              qtiDolphinTrackVsyncSignal && qtiDolphinUnblockPendingBuffer &&
                              qtiDolphinIsTargetFpsActive;
        if (functionsFound) {
            qtiDolphinInit(width, height);
        } else {
            ALOGW("Unable to find dolphin functions!");
            dlclose(mQtiDolphinHandle);
            qtiDolphinInit = nullptr;
            qtiDolphinSetVsyncPeriod = nullptr;
            qtiDolphinTrackBufferIncrement = nullptr;
            qtiDolphinTrackBufferDecrement = nullptr;
            qtiDolphinTrackVsyncSignal = nullptr;
            qtiDolphinUnblockPendingBuffer = nullptr;
            qtiDolphinIsTargetFpsActive = nullptr;
        }
    }
}

QtiDolphinWrapper::~QtiDolphinWrapper() {
    if (mQtiDolphinHandle) {
        dlclose(mQtiDolphinHandle);
    }
}

} // namespace android::surfaceflingerextension
