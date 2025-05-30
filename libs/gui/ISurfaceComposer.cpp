/*
 * Copyright (C) 2007 The Android Open Source Project
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

// tag as surfaceflinger
#define LOG_TAG "SurfaceFlinger"

#include <android/gui/IDisplayEventConnection.h>
#include <android/gui/IRegionSamplingListener.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <gui/IGraphicBufferProducer.h>
#include <gui/ISurfaceComposer.h>
#include <gui/LayerState.h>
#include <gui/SchedulingPolicy.h>
#include <gui/SimpleTransactionState.h>
#include <gui/TransactionState.h>
#include <private/gui/ParcelUtils.h>
#include <stdint.h>
#include <sys/types.h>
#include <system/graphics.h>
#include <ui/DisplayMode.h>
#include <ui/DisplayStatInfo.h>
#include <ui/DisplayState.h>
#include <ui/DynamicDisplayInfo.h>
#include <ui/HdrCapabilities.h>
#include <utils/Log.h>

// ---------------------------------------------------------------------------

using namespace aidl::android::hardware::graphics;

namespace android {

using gui::DisplayCaptureArgs;
using gui::IDisplayEventConnection;
using gui::IRegionSamplingListener;
using gui::IWindowInfosListener;
using gui::LayerCaptureArgs;
using ui::ColorMode;

class BpSurfaceComposer : public BpInterface<ISurfaceComposer> {
public:
    explicit BpSurfaceComposer(const sp<IBinder>& impl) : BpInterface<ISurfaceComposer>(impl) {}

    virtual ~BpSurfaceComposer();

    status_t setTransactionState(
            const SimpleTransactionState simpleState, const FrameTimelineInfo& frameTimelineInfo,
            Vector<ComposerState>& state, Vector<DisplayState>& displays,
            const sp<IBinder>& applyToken, const std::vector<client_cache_t>& uncacheBuffers,
            const TransactionListenerCallbacks& listenerCallbacks,
            const std::vector<uint64_t>& mergedTransactionIds,
            const std::vector<gui::EarlyWakeupInfo>& earlyWakeupInfos) override {
        Parcel data, reply;
        data.writeInterfaceToken(ISurfaceComposer::getInterfaceDescriptor());

        frameTimelineInfo.writeToParcel(&data);

        SAFE_PARCEL(data.writeUint32, static_cast<uint32_t>(state.size()));
        for (const auto& s : state) {
            SAFE_PARCEL(s.write, data);
        }

        SAFE_PARCEL(data.writeUint32, static_cast<uint32_t>(displays.size()));
        for (const auto& d : displays) {
            SAFE_PARCEL(d.write, data);
        }

        SAFE_PARCEL(simpleState.writeToParcel, &data);
        SAFE_PARCEL(data.writeStrongBinder, applyToken);
        SAFE_PARCEL(data.writeUint32, static_cast<uint32_t>(uncacheBuffers.size()));
        for (const client_cache_t& uncacheBuffer : uncacheBuffers) {
            SAFE_PARCEL(data.writeStrongBinder, uncacheBuffer.token.promote());
            SAFE_PARCEL(data.writeUint64, uncacheBuffer.id);
        }

        SAFE_PARCEL(listenerCallbacks.writeToParcel, &data);

        SAFE_PARCEL(data.writeUint32, static_cast<uint32_t>(mergedTransactionIds.size()));
        for (auto mergedTransactionId : mergedTransactionIds) {
            SAFE_PARCEL(data.writeUint64, mergedTransactionId);
        }

        SAFE_PARCEL(data.writeUint32, static_cast<uint32_t>(earlyWakeupInfos.size()));
        for (const auto& e : earlyWakeupInfos) {
            e.writeToParcel(&data);
        }

        if (simpleState.mFlags & ISurfaceComposer::eOneWay) {
            return remote()->transact(BnSurfaceComposer::SET_TRANSACTION_STATE, data, &reply,
                                      IBinder::FLAG_ONEWAY);
        } else {
            return remote()->transact(BnSurfaceComposer::SET_TRANSACTION_STATE, data, &reply);
        }
    }
};

// Out-of-line virtual method definition to trigger vtable emission in this
// translation unit (see clang warning -Wweak-vtables)
BpSurfaceComposer::~BpSurfaceComposer() {}

IMPLEMENT_META_INTERFACE(SurfaceComposer, "android.ui.ISurfaceComposer");

// ----------------------------------------------------------------------

status_t BnSurfaceComposer::onTransact(uint32_t code, const Parcel& data, Parcel* reply,
                                       uint32_t flags) {
    switch (code) {
        case SET_TRANSACTION_STATE: {
            CHECK_INTERFACE(ISurfaceComposer, data, reply);

            FrameTimelineInfo frameTimelineInfo;
            frameTimelineInfo.readFromParcel(&data);

            uint32_t count = 0;
            SAFE_PARCEL_READ_SIZE(data.readUint32, &count, data.dataSize());
            Vector<ComposerState> state;
            state.setCapacity(count);
            for (size_t i = 0; i < count; i++) {
                ComposerState s;
                SAFE_PARCEL(s.read, data);
                state.add(s);
            }

            SAFE_PARCEL_READ_SIZE(data.readUint32, &count, data.dataSize());
            DisplayState d;
            Vector<DisplayState> displays;
            displays.setCapacity(count);
            for (size_t i = 0; i < count; i++) {
                SAFE_PARCEL(d.read, data);
                displays.add(d);
            }

            SimpleTransactionState simpleState;
            SAFE_PARCEL(simpleState.readFromParcel, &data);

            sp<IBinder> applyToken;
            SAFE_PARCEL(data.readStrongBinder, &applyToken);

            SAFE_PARCEL_READ_SIZE(data.readUint32, &count, data.dataSize());
            std::vector<client_cache_t> uncacheBuffers(count);
            sp<IBinder> tmpBinder;
            for (size_t i = 0; i < count; i++) {
                SAFE_PARCEL(data.readNullableStrongBinder, &tmpBinder);
                uncacheBuffers[i].token = tmpBinder;
                SAFE_PARCEL(data.readUint64, &uncacheBuffers[i].id);
            }

            TransactionListenerCallbacks listenerCallbacks;
            SAFE_PARCEL(listenerCallbacks.readFromParcel, &data);

            SAFE_PARCEL_READ_SIZE(data.readUint32, &count, data.dataSize());
            std::vector<uint64_t> mergedTransactions(count);
            for (size_t i = 0; i < count; i++) {
                SAFE_PARCEL(data.readUint64, &mergedTransactions[i]);
            }

            count = 0;
            SAFE_PARCEL_READ_SIZE(data.readUint32, &count, data.dataSize());
            std::vector<gui::EarlyWakeupInfo> earlyWakeupInfos;
            earlyWakeupInfos.reserve(count);
            for (size_t i = 0; i < count; i++) {
                gui::EarlyWakeupInfo e;
                e.readFromParcel(&data);
                earlyWakeupInfos.push_back(std::move(e));
            }

            return setTransactionState(simpleState, frameTimelineInfo, state, displays, applyToken,
                                       uncacheBuffers, listenerCallbacks, mergedTransactions,
                                       earlyWakeupInfos);
        }
        case GET_SCHEDULING_POLICY: {
            gui::SchedulingPolicy policy;
            const auto status = gui::getSchedulingPolicy(&policy);
            if (!status.isOk()) {
                return status.exceptionCode();
            }

            SAFE_PARCEL(reply->writeInt32, policy.policy);
            SAFE_PARCEL(reply->writeInt32, policy.priority);
            return NO_ERROR;
        }

        default: {
            return BBinder::onTransact(code, data, reply, flags);
        }
    }
}

} // namespace android
