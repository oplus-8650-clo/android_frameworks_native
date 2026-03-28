/*
 * Copyright 2023 The Android Open Source Project
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

#pragma once

#include "InputListener.h"
#include "NotifyArgs.h"
#include "PointerChoreographerPolicyInterface.h"

#include <android-base/thread_annotations.h>
#include <gui/WindowInfosListener.h>
#include <input/DisplayTopologyGraph.h>
#include <type_traits>
#include <unordered_set>

namespace android {

struct SpriteIcon;

/**
 * A helper class that wraps a factory method that acts as a constructor for the type returned
 * by the factory method.
 */
template <typename Factory>
struct ConstructorDelegate {
    constexpr ConstructorDelegate(Factory&& factory) : mFactory(std::move(factory)) {}

    using ConstructedType = std::invoke_result_t<const Factory&>;
    constexpr operator ConstructedType() const { return mFactory(); }

    Factory mFactory;
};

/**
 * PointerChoreographer manages the icons shown by the system for input interactions.
 * This includes showing the mouse cursor, stylus hover icons, and touch spots.
 * It is responsible for accumulating the location of the mouse cursor, and populating
 * the cursor position for incoming events, if necessary.
 */
class PointerChoreographerInterface : public InputListenerInterface {
public:
    virtual void setDisplayViewports(const std::vector<DisplayViewport>& viewports) = 0;
    virtual std::optional<DisplayViewport> getViewportForPointerDevice(
            ui::LogicalDisplayId associatedDisplayId = ui::LogicalDisplayId::INVALID) = 0;
    /**
     * Gets the current position of the mouse cursor on the specified display in the display
     * physical coordinates.
     *
     * Returns optional.empty if no cursor is available, or if existing cursor is not on
     * supplied `displayId`.
     *
     * This method is inherently racy, and should only be used for test purposes.
     */
    virtual std::optional<vec2> getMouseCursorPosition(ui::LogicalDisplayId displayId) = 0;
    /**
     * Gets the current position of the mouse cursor on the specified display in the display logical
     * coordinates.
     *
     * Returns optional.empty if no cursor is available, or if existing cursor is not on
     * supplied `displayId`.
     *
     * This method is inherently racy, and should only be used for test purposes.
     */
    virtual std::optional<vec2> getMouseCursorPositionInLogicalDisplay(
            ui::LogicalDisplayId displayId) = 0;
    /**
     * Sets whether to show the positions of the touches on all displays (false by default).
     */
    virtual void setShowTouchesEnabled(bool enabled) = 0;
    /**
     * Sets whether to show the positions of the touches on the given display, even if we currently
     * don't show touches on all displays (i.e., if setShowTouchesEnabled(true) was not called).
     *
     * No-op if no display with the given id exists.
     */
    virtual void setForceShowTouchesOnDisplay(ui::LogicalDisplayId displayId, bool enabled) = 0;
    virtual void setStylusPointerIconEnabled(bool enabled) = 0;
    /**
     * Set the icon that is shown for the given pointer. The request may fail in some cases, such
     * as if the device or display was removed, or if the cursor was moved to a different display.
     * Returns true if the icon was changed successfully, false otherwise.
     */
    virtual bool setPointerIcon(std::variant<std::unique_ptr<SpriteIcon>, PointerIconStyle> icon,
                                ui::LogicalDisplayId displayId, DeviceId deviceId) = 0;
    /**
     * Set whether pointer icons for mice, touchpads, and styluses should be visible on the
     * given display.
     */
    virtual void setPointerIconVisibility(ui::LogicalDisplayId displayId, bool visible) = 0;

    /**
     * Used by Dispatcher to notify changes in the current focused display.
     */
    virtual void setFocusedDisplay(ui::LogicalDisplayId displayId) = 0;

    /*
     * Used by InputManager to notify changes in the DisplayTopology
     */
    virtual void setDisplayTopology(const DisplayTopologyGraph& displayTopologyGraph) = 0;

    /**
     * This method may be called on any thread (usually by the input manager on a binder thread).
     */
    virtual void dump(std::string& dump) = 0;

    /**
     * Enables motion event filter before pointer coordinates are determined.
     */
    virtual void setAccessibilityPointerMotionFilterEnabled(bool enabled) = 0;
};

class PointerChoreographer : public PointerChoreographerInterface {
public:
    explicit PointerChoreographer(InputListenerInterface& listener,
                                  PointerChoreographerPolicyInterface&);

    void setDisplayViewports(const std::vector<DisplayViewport>& viewports) override;
    std::optional<DisplayViewport> getViewportForPointerDevice(
            ui::LogicalDisplayId associatedDisplayId) override;
    std::optional<vec2> getMouseCursorPosition(ui::LogicalDisplayId displayId) override;
    std::optional<vec2> getMouseCursorPositionInLogicalDisplay(
            ui::LogicalDisplayId displayId) override;
    void setShowTouchesEnabled(bool enabled) override;
    void setForceShowTouchesOnDisplay(ui::LogicalDisplayId displayId, bool enabled) override;
    void setStylusPointerIconEnabled(bool enabled) override;
    bool setPointerIcon(std::variant<std::unique_ptr<SpriteIcon>, PointerIconStyle> icon,
                        ui::LogicalDisplayId displayId, DeviceId deviceId) override;
    void setPointerIconVisibility(ui::LogicalDisplayId displayId, bool visible) override;
    void setFocusedDisplay(ui::LogicalDisplayId displayId) override;
    void setDisplayTopology(const DisplayTopologyGraph& displayTopologyGraph);
    void setAccessibilityPointerMotionFilterEnabled(bool enabled) override;

    void notifyInputDevicesChanged(const NotifyInputDevicesChangedArgs& args) override;
    void notifyWindowInfos(const NotifyWindowInfosArgs& args) override;
    void notifyKey(const NotifyKeyArgs& args) override;
    void notifyMotion(const NotifyMotionArgs& args) override;
    void notifySwitch(const NotifySwitchArgs& args) override;
    void notifySensor(const NotifySensorArgs& args) override;
    void notifyVibratorState(const NotifyVibratorStateArgs& args) override;
    void notifyDeviceReset(const NotifyDeviceResetArgs& args) override;
    void notifyPointerCaptureChanged(const NotifyPointerCaptureChangedArgs& args) override;

    void dump(std::string& dump) override;

private:
    using PointerDisplayChange =
            std::optional<std::tuple<ui::LogicalDisplayId /*displayId*/, vec2 /*cursorPosition*/>>;

    [[nodiscard]] PointerDisplayChange updatePointerControllersLocked() REQUIRES(mLock);
    [[nodiscard]] PointerDisplayChange calculatePointerDisplayChangeToNotify() REQUIRES(mLock);
    const DisplayViewport* findViewportByIdLocked(ui::LogicalDisplayId displayId) const
            REQUIRES(mLock);
    ui::LogicalDisplayId getTargetMouseDisplayLocked(ui::LogicalDisplayId associatedDisplayId) const
            REQUIRES(mLock);
    std::pair<ui::LogicalDisplayId /*displayId*/, PointerControllerInterface&>
    ensureMouseControllerLocked(ui::LogicalDisplayId associatedDisplayId) REQUIRES(mLock);
    InputDeviceInfo* findInputDeviceLocked(DeviceId deviceId) REQUIRES(mLock);
    bool canUnfadeOnDisplay(ui::LogicalDisplayId displayId) REQUIRES(mLock);
    bool shouldShowTouchesOnDisplay(ui::LogicalDisplayId displayId) REQUIRES(mLock);

    void fadeMouseCursorOnKeyPress(const NotifyKeyArgs& args);
    NotifyMotionArgs processMotion(const NotifyMotionArgs& args);
    NotifyMotionArgs processMouseEventLocked(const NotifyMotionArgs& args) REQUIRES(mLock);
    NotifyMotionArgs processTouchpadEventLocked(const NotifyMotionArgs& args) REQUIRES(mLock);
    void processDrawingTabletEventLocked(const NotifyMotionArgs& args) REQUIRES(mLock);
    void processTouchscreenAndStylusEventLocked(const NotifyMotionArgs& args) REQUIRES(mLock);
    void processStylusHoverEventLocked(const NotifyMotionArgs& args) REQUIRES(mLock);
    void processPointerDeviceMotionEventLocked(NotifyMotionArgs& newArgs,
                                               PointerControllerInterface& pc) REQUIRES(mLock);
    void processDeviceReset(const NotifyDeviceResetArgs& args);
    void onControllerAddedOrRemovedLocked() REQUIRES(mLock);
    void onWindowInfosChanged(const gui::WindowInfosUpdate& update);
    void onPrivacySensitiveDisplaysChangedLocked(
            const std::unordered_set<ui::LogicalDisplayId>& privacySensitiveDisplays)
            REQUIRES(mLock);

    void handleUnconsumedDeltaLocked(PointerControllerInterface& pc, const vec2& unconsumedDelta)
            REQUIRES(mLock);

    std::optional<std::pair<const DisplayViewport*, float /*offsetPx*/>>
    findDestinationDisplayLocked(const ui::LogicalDisplayId sourceDisplayId,
                                 const DisplayTopologyPosition sourceBoundary,
                                 int32_t sourceCursorOffsetPx) const REQUIRES(mLock);

    vec2 filterPointerMotionForAccessibilityLocked(const PointerControllerInterface& pc,
                                                   const vec2& delta) REQUIRES(mLock);

    /* Topology is initialized with default-constructed value, which is an empty topology. Till we
     * receive setDisplayTopology call.
     * Meanwhile Choreographer will treat every display as independent disconnected display.
     */
    DisplayTopologyGraph mTopology GUARDED_BY(mLock);

    mutable std::mutex mLock;
    std::unordered_set<ui::LogicalDisplayId /*displayId*/> mPrivacySensitiveDisplays
            GUARDED_BY(mLock);

    using ControllerConstructor =
            ConstructorDelegate<std::function<std::shared_ptr<PointerControllerInterface>()>>;
    ControllerConstructor mTouchControllerConstructor GUARDED_BY(mLock);
    ControllerConstructor getMouseControllerConstructor(ui::LogicalDisplayId displayId)
            REQUIRES(mLock);
    ControllerConstructor getStylusControllerConstructor(ui::LogicalDisplayId displayId)
            REQUIRES(mLock);

    InputListenerInterface& mNextListener;
    PointerChoreographerPolicyInterface& mPolicy;

    std::map<ui::LogicalDisplayId, std::shared_ptr<PointerControllerInterface>>
            mMousePointersByDisplay GUARDED_BY(mLock);
    std::map<DeviceId, std::shared_ptr<PointerControllerInterface>> mTouchPointersByDevice
            GUARDED_BY(mLock);
    std::map<DeviceId, std::shared_ptr<PointerControllerInterface>> mStylusPointersByDevice
            GUARDED_BY(mLock);
    std::map<DeviceId, std::shared_ptr<PointerControllerInterface>> mDrawingTabletPointersByDevice
            GUARDED_BY(mLock);

    // In connected displays scenario, this tracks the latest display the cursor is at, within the
    // DisplayTopology. By default, this will be set to topology primary display, and updated when
    // mouse crossed to another display.
    // In non-connected displays scenario, this will be treated as the default display cursor
    // will be on, when mouse doesn't have associated display.
    ui::LogicalDisplayId mCurrentMouseDisplayId GUARDED_BY(mLock);
    ui::LogicalDisplayId mNotifiedPointerDisplayId GUARDED_BY(mLock);
    std::vector<InputDeviceInfo> mInputDeviceInfos GUARDED_BY(mLock);
    std::set<DeviceId> mMouseDevices GUARDED_BY(mLock);
    std::vector<DisplayViewport> mViewports GUARDED_BY(mLock);
    bool mShowTouchesEnabled GUARDED_BY(mLock);
    std::set<ui::LogicalDisplayId> mDisplaysWithShowTouchesForceEnabled GUARDED_BY(mLock);
    bool mStylusPointerIconEnabled GUARDED_BY(mLock);
    bool mPointerMotionFilterEnabled GUARDED_BY(mLock);
    std::set<ui::LogicalDisplayId /*displayId*/> mDisplaysWithPointersHidden;
    ui::LogicalDisplayId mCurrentFocusedDisplay GUARDED_BY(mLock);
};

} // namespace android
