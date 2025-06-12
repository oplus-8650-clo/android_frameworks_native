/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#define LOG_NDEBUG 0
#include "QtiHidlComposerHalExtension.h"

namespace android::surfaceflingerextension {

QtiHidlComposerHalExtension::QtiHidlComposerHalExtension(Hwc2::Composer* composerHal) {
    if (!composerHal) {
        ALOGW("Passed an invalid pointer to composer hal");
        return;
    }

    mQtiHidlComposer = static_cast<Hwc2::HidlComposer*>(composerHal);
    mClient_3_1 = mQtiHidlComposer->mClient_3_1;
    if (mClient_3_1 == nullptr) {
        ALOGW("mClient_3_1 is null");
        return;
    }
    ALOGV("Successfully created QtiHidlComposerHalExtension %p", mQtiHidlComposer);
}

Error QtiHidlComposerHalExtension::qtiSetDisplayElapseTime(Display display, uint64_t timeStamp) {
    mQtiHidlComposer->mWriter.selectDisplay(display);
    mQtiHidlComposer->mWriter.qtiSetDisplayElapseTime(timeStamp);
    return Error::NONE;
}

Error QtiHidlComposerHalExtension::qtiSetLayerType(Display display, V2_1_Layer layer,
                                                   uint32_t type) {
    if (mQtiHidlComposer->mClient_2_4) {
        mQtiHidlComposer->mWriter.selectDisplay(display);
        mQtiHidlComposer->mWriter.selectLayer(layer);
        mQtiHidlComposer->mWriter.qtiSetLayerType(type);
    }
    return Error::NONE;
}

Error QtiHidlComposerHalExtension::qtiTryDrawMethod(Display display,
                                                    uint32_t drawMethod) {
    if (mClient_3_1) {
        return mClient_3_1->tryDrawMethod(display,
                                          static_cast<IQtiComposerClient::DrawMethod>(drawMethod));
    }

    return Error::NO_RESOURCES;
}

Error QtiHidlComposerHalExtension::qtiSetClientTarget_3_1(Display display, int32_t slot,
                                                          int acquireFence,
                                                          uint32_t dataspace) {
    mQtiHidlComposer->mWriter.selectDisplay(display);
    mQtiHidlComposer->mWriter.qtiSetClientTarget_3_1(slot, acquireFence,
                                                     static_cast<ui::Dataspace>(dataspace));
    return Error::NONE;
}

Error QtiHidlComposerHalExtension::qtiSetLayerFlag(Display display, V2_1_Layer layer,
                                                   uint32_t layerFlag) {
    if (mClient_3_1 == nullptr) {
        return Error::NO_RESOURCES;
    }

    mQtiHidlComposer->mWriter.selectDisplay(display);
    mQtiHidlComposer->mWriter.selectLayer(layer);
    mQtiHidlComposer->mWriter.qtiSetLayerFlag(layerFlag);
    return Error::NONE;
}

Error QtiHidlComposerHalExtension::qtiSetCornerRadius(Display display, V2_1_Layer layer, float x,
                                                      float y) {
    ALOGW("Not supported!");
    return Error::UNSUPPORTED;
}

Error QtiHidlComposerHalExtension::qtiSetPrivacyRegions(Display display, V2_1_Layer layer,
                                                        const std::vector<Rect>& rectList,
                                                        const std::vector<float>& radiusList,
                                                        const std::vector<uint32_t>& indexList) {
    ALOGW("Not supported!");
    return Error::UNSUPPORTED;
}

} // namespace android::surfaceflingerextension
