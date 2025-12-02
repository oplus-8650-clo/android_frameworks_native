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

#include "TouchpadInputMapper.h"

#include <android-base/logging.h>
#include <gtest/gtest.h>
#include <input/AccelerationCurve.h>
#include <input/Input.h>

#include <log/log.h>
#include <list>
#include <thread>
#include "InputMapperTest.h"
#include "NotifyArgs.h"
#include "TestConstants.h"
#include "TestEventMatchers.h"

#define TAG "TouchpadInputMapper_test"

namespace android {

using testing::AllOf;
using testing::Contains;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::Return;
using testing::VariantWith;
constexpr auto ACTION_DOWN = AMOTION_EVENT_ACTION_DOWN;
constexpr auto ACTION_UP = AMOTION_EVENT_ACTION_UP;
constexpr auto BUTTON_PRESS = AMOTION_EVENT_ACTION_BUTTON_PRESS;
constexpr auto BUTTON_RELEASE = AMOTION_EVENT_ACTION_BUTTON_RELEASE;
constexpr auto HOVER_MOVE = AMOTION_EVENT_ACTION_HOVER_MOVE;
constexpr auto HOVER_ENTER = AMOTION_EVENT_ACTION_HOVER_ENTER;
constexpr auto HOVER_EXIT = AMOTION_EVENT_ACTION_HOVER_EXIT;
constexpr ui::LogicalDisplayId DISPLAY_ID = ui::LogicalDisplayId::DEFAULT;
constexpr int32_t DISPLAY_WIDTH = 480;
constexpr int32_t DISPLAY_HEIGHT = 800;
constexpr std::optional<uint8_t> NO_PORT = std::nullopt; // no physical port is specified

/**
 * Unit tests for TouchpadInputMapper.
 */
class TouchpadInputMapperTest : public VerifyingInputMapperUnitTest {
protected:
    void SetUp() override {
        VerifyingInputMapperUnitTest::SetUp();

        // Present scan codes: BTN_TOUCH and BTN_TOOL_FINGER
        expectScanCodes(/*present=*/true,
                        {BTN_LEFT, BTN_RIGHT, BTN_TOOL_FINGER, BTN_TOOL_QUINTTAP, BTN_TOUCH,
                         BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP, BTN_TOOL_QUADTAP});
        // Missing scan codes that the mapper checks for.
        expectScanCodes(/*present=*/false,
                        {BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOOL_BRUSH, BTN_TOOL_PENCIL,
                         BTN_TOOL_AIRBRUSH});

        // Current scan code state - all keys are UP by default
        setScanCodeState(KeyState::UP, {BTN_TOUCH,          BTN_STYLUS,
                                        BTN_STYLUS2,        BTN_0,
                                        BTN_TOOL_FINGER,    BTN_TOOL_PEN,
                                        BTN_TOOL_RUBBER,    BTN_TOOL_BRUSH,
                                        BTN_TOOL_PENCIL,    BTN_TOOL_AIRBRUSH,
                                        BTN_TOOL_MOUSE,     BTN_TOOL_LENS,
                                        BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP,
                                        BTN_TOOL_QUADTAP,   BTN_TOOL_QUINTTAP,
                                        BTN_LEFT,           BTN_RIGHT,
                                        BTN_MIDDLE,         BTN_BACK,
                                        BTN_SIDE,           BTN_FORWARD,
                                        BTN_EXTRA,          BTN_TASK});

        setKeyCodeState(KeyState::UP,
                        {AKEYCODE_STYLUS_BUTTON_PRIMARY, AKEYCODE_STYLUS_BUTTON_SECONDARY});

        // Key mappings
        EXPECT_CALL(mMockEventHub,
                    mapKey(EVENTHUB_ID, BTN_LEFT, /*usageCode=*/0, /*metaState=*/0, testing::_,
                           testing::_, testing::_))
                .WillRepeatedly(Return(NAME_NOT_FOUND));

        // Input properties - only INPUT_PROP_BUTTONPAD
        EXPECT_CALL(mMockEventHub, hasInputProperty(EVENTHUB_ID, INPUT_PROP_BUTTONPAD))
                .WillRepeatedly(Return(true));
        EXPECT_CALL(mMockEventHub, hasInputProperty(EVENTHUB_ID, INPUT_PROP_SEMI_MT))
                .WillRepeatedly(Return(false));

        // Axes that the device has
        setupAxis(ABS_MT_SLOT, /*valid=*/true, /*min=*/0, /*max=*/4, /*resolution=*/0);
        setupAxis(ABS_MT_POSITION_X, /*valid=*/true, /*min=*/0, /*max=*/2000, /*resolution=*/24);
        setupAxis(ABS_MT_POSITION_Y, /*valid=*/true, /*min=*/0, /*max=*/1000, /*resolution=*/24);
        setupAxis(ABS_MT_PRESSURE, /*valid=*/true, /*min*/ 0, /*max=*/255, /*resolution=*/0);
        // Axes that the device does not have
        setupAxis(ABS_MT_ORIENTATION, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TOUCH_MAJOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TOUCH_MINOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_WIDTH_MAJOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_WIDTH_MINOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TRACKING_ID, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_DISTANCE, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TOOL_TYPE, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);

        EXPECT_CALL(mMockEventHub, getAbsoluteAxisValue(EVENTHUB_ID, ABS_MT_SLOT))
                .WillRepeatedly(Return(0));
        EXPECT_CALL(mMockEventHub, getMtSlotValues(EVENTHUB_ID, testing::_, testing::_))
                .WillRepeatedly([]() -> base::Result<std::vector<int32_t>> {
                    return base::ResultError("Axis not supported", NAME_NOT_FOUND);
                });

        // User's settings - enable pointer acceleration
        mReaderConfiguration.touchpadAccelerationEnabled = true;

        mMapper = createInputMapper<TouchpadInputMapper>(*mDeviceContext, mReaderConfiguration);
    }

    void assertAccelCurvePropEquals(const std::string& propName,
                                    const std::vector<AccelerationCurveSegment>& expectedSegments) {
        SCOPED_TRACE(testing::Message() << "Gesture property \"" << propName << "\"");
        auto* touchpadMapper = static_cast<TouchpadInputMapper*>(mMapper.get());

        const auto prop = touchpadMapper->getGesturePropertyForTesting(propName);
        ASSERT_TRUE(prop.has_value());

        std::vector<double> actualValues = prop.value().getRealValues();

        for (size_t i = 0; i < expectedSegments.size(); ++i) {
            SCOPED_TRACE(testing::Message() << "i = " << i);
            if (std::isinf(expectedSegments[i].maxPointerSpeedMmPerS)) {
                ASSERT_TRUE(std::isinf(actualValues[i * 4 + 0]));
            } else {
                ASSERT_NEAR(actualValues[i * 4 + 0], expectedSegments[i].maxPointerSpeedMmPerS,
                            EPSILON);
            }

            ASSERT_NEAR(actualValues[i * 4 + 1], 0, EPSILON);
            ASSERT_NEAR(actualValues[i * 4 + 2], expectedSegments[i].baseGain, EPSILON);
            ASSERT_NEAR(actualValues[i * 4 + 3], expectedSegments[i].reciprocal, EPSILON);
        }
    }

    void assertBothCurvesEqual(const std::vector<AccelerationCurveSegment>& expectedSegments) {
        assertAccelCurvePropEquals("Pointer Accel Curve", expectedSegments);
        assertAccelCurvePropEquals("Scroll Accel Curve", expectedSegments);
    }

    void assertRelativeModeCurvesInUse() {
        auto* touchpadMapper = static_cast<TouchpadInputMapper*>(mMapper.get());

        // The pointer curve should be flat (i.e. a single segment with infinite maximum speed).
        const auto pointerCurveProp =
                touchpadMapper->getGesturePropertyForTesting("Pointer Accel Curve");
        ASSERT_TRUE(pointerCurveProp.has_value());

        std::vector<double> pointerCurveValues = pointerCurveProp.value().getRealValues();
        ASSERT_TRUE(std::isinf(pointerCurveValues[0]));
        ASSERT_NEAR(pointerCurveValues[1], 0, EPSILON);
        // What exact gain it has is an implementation detail subject to change, so we don't want to
        // assert an exact value.
        double pointerCurveGain = pointerCurveValues[2];
        ASSERT_GT(pointerCurveGain, 0);
        ASSERT_NEAR(pointerCurveValues[3], 0, EPSILON);

        // The scroll curve should also be flat.
        const auto scrollCurveProp =
                touchpadMapper->getGesturePropertyForTesting("Scroll Accel Curve");
        ASSERT_TRUE(scrollCurveProp.has_value());

        std::vector<double> scrollCurveValues = scrollCurveProp.value().getRealValues();
        ASSERT_TRUE(std::isinf(scrollCurveValues[0]));
        ASSERT_NEAR(scrollCurveValues[1], 0, EPSILON);
        // Again, we don't want to specify the exact gain, but it should be positive and smaller
        // than the pointer curve's gain (since a scroll wheel tick is a much larger movement than a
        // single-pixel move on a mouse, so the scroll numbers reported in relative mode should be
        // much smaller than the cursor movement numbers for the same physical distance moved).
        double scrollCurveGain = scrollCurveValues[2];
        ASSERT_GT(scrollCurveGain, 0);
        ASSERT_LT(scrollCurveGain, pointerCurveGain);
        ASSERT_NEAR(scrollCurveValues[3], 0, EPSILON);
    }
};

/**
 * Start moving the finger and then click the left touchpad button. Check whether HOVER_EXIT is
 * generated when hovering stops. Currently, it is not.
 * In the current implementation, HOVER_MOVE and ACTION_DOWN events are not sent out right away,
 * but only after the button is released.
 */
TEST_F(TouchpadInputMapperTest, HoverAndLeftButtonPress) {
    mFakePolicy->setDefaultPointerDisplayId(DISPLAY_ID);
    DisplayViewport viewport =
            createViewport(DISPLAY_ID, DISPLAY_WIDTH, DISPLAY_HEIGHT, ui::ROTATION_0,
                           /*isActive=*/true, "local:0", NO_PORT, ViewportType::INTERNAL);
    mFakePolicy->addDisplayViewport(viewport);
    std::list<NotifyArgs> args;

    args += reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                              InputReaderConfiguration::Change::DISPLAY_INFO);
    ASSERT_THAT(args, testing::IsEmpty());

    args += process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    args += process(EV_KEY, BTN_TOUCH, 1);
    setScanCodeState(KeyState::DOWN, {BTN_TOOL_FINGER});
    args += process(EV_KEY, BTN_TOOL_FINGER, 1);
    args += process(EV_ABS, ABS_MT_POSITION_X, 50);
    args += process(EV_ABS, ABS_MT_POSITION_Y, 50);
    args += process(EV_ABS, ABS_MT_PRESSURE, 1);
    args += process(EV_SYN, SYN_REPORT, 0);
    ASSERT_THAT(args, testing::IsEmpty());

    // Without this sleep, the test fails.
    // TODO(b/284133337): Figure out whether this can be removed
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    args += process(EV_KEY, BTN_LEFT, 1);
    setScanCodeState(KeyState::DOWN, {BTN_LEFT});
    args += process(EV_SYN, SYN_REPORT, 0);

    args += process(EV_KEY, BTN_LEFT, 0);
    setScanCodeState(KeyState::UP, {BTN_LEFT});
    args += process(EV_SYN, SYN_REPORT, 0);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(WithMotionAction(HOVER_ENTER)),
                            VariantWith<NotifyMotionArgs>(WithMotionAction(HOVER_MOVE)),
                            VariantWith<NotifyMotionArgs>(WithMotionAction(HOVER_EXIT)),
                            VariantWith<NotifyMotionArgs>(WithMotionAction(ACTION_DOWN)),
                            VariantWith<NotifyMotionArgs>(WithMotionAction(BUTTON_PRESS)),
                            VariantWith<NotifyMotionArgs>(WithMotionAction(BUTTON_RELEASE)),
                            VariantWith<NotifyMotionArgs>(WithMotionAction(ACTION_UP)),
                            VariantWith<NotifyMotionArgs>(WithMotionAction(HOVER_ENTER))));

    // Liftoff
    args.clear();
    args += process(EV_ABS, ABS_MT_PRESSURE, 0);
    args += process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    args += process(EV_KEY, BTN_TOUCH, 0);
    setScanCodeState(KeyState::UP, {BTN_TOOL_FINGER});
    args += process(EV_KEY, BTN_TOOL_FINGER, 0);
    args += process(EV_SYN, SYN_REPORT, 0);
    ASSERT_THAT(args, testing::IsEmpty());
}

// Regression test for b/458469793, where a HOVER_EXIT event was incorrectly reported after
// resetting the touchpad when the last gesture was a scroll.
TEST_F(TouchpadInputMapperTest, ScrollThenResetThenHoverMoveIsConsistent) {
    mFakePolicy->setDefaultPointerDisplayId(DISPLAY_ID);
    DisplayViewport viewport =
            createViewport(DISPLAY_ID, DISPLAY_WIDTH, DISPLAY_HEIGHT, ui::ROTATION_0,
                           /*isActive=*/true, "local:0", NO_PORT, ViewportType::INTERNAL);
    mFakePolicy->addDisplayViewport(viewport);
    std::list<NotifyArgs> args;

    args += reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                              InputReaderConfiguration::Change::DISPLAY_INFO);
    ASSERT_THAT(args, testing::IsEmpty());

    // Scroll on the touchpad.
    int32_t scrollY = 300;
    args += process(EV_ABS, ABS_MT_SLOT, 0);
    args += process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    args += process(EV_ABS, ABS_MT_POSITION_X, 500);
    args += process(EV_ABS, ABS_MT_POSITION_Y, scrollY);
    args += process(EV_ABS, ABS_MT_PRESSURE, 50);
    args += process(EV_ABS, ABS_MT_SLOT, 1);
    args += process(EV_ABS, ABS_MT_TRACKING_ID, 2);
    args += process(EV_ABS, ABS_MT_POSITION_X, 800);
    args += process(EV_ABS, ABS_MT_POSITION_Y, scrollY);
    args += process(EV_ABS, ABS_MT_PRESSURE, 50);
    args += process(EV_KEY, BTN_TOUCH, 1);
    args += process(EV_KEY, BTN_TOOL_DOUBLETAP, 1);
    setScanCodeState(KeyState::DOWN, {BTN_TOUCH, BTN_TOOL_DOUBLETAP});
    args += process(EV_SYN, SYN_REPORT, 0);
    ASSERT_THAT(args, testing::IsEmpty());

    for (int32_t i = 0; i < 5; i++) {
        // Without these sleeps, the test fails.
        // TODO(b/284133337): remove the dependency of these tests on "real time"
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        scrollY += 50;

        args += process(EV_ABS, ABS_MT_SLOT, 0);
        args += process(EV_ABS, ABS_MT_POSITION_Y, scrollY);
        args += process(EV_ABS, ABS_MT_SLOT, 1);
        args += process(EV_ABS, ABS_MT_POSITION_Y, scrollY);
        args += process(EV_SYN, SYN_REPORT, 0);
    }

    // Lift the fingers.
    args += process(EV_ABS, ABS_MT_SLOT, 0);
    args += process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    args += process(EV_ABS, ABS_MT_SLOT, 1);
    args += process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    args += process(EV_KEY, BTN_TOUCH, 0);
    args += process(EV_KEY, BTN_TOOL_DOUBLETAP, 0);
    setScanCodeState(KeyState::UP, {BTN_TOUCH, BTN_TOOL_DOUBLETAP});
    args += process(EV_SYN, SYN_REPORT, 0);
    // We don't care exactly what events are output here (as we're just concerned with consistency
    // enforced by the verifier), but we want to check that the events above did actually trigger a
    // complete two-finger swipe.
    ASSERT_THAT(args,
                Contains(VariantWith<NotifyMotionArgs>(
                        AllOf(WithMotionAction(ACTION_UP),
                              WithMotionClassification(MotionClassification::TWO_FINGER_SWIPE)))));

    // This sleep must be longer than the "Change Timeout" gesture property, to prevent
    // ImmediateInterpreter from suppressing the following finger motions.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // Reset the mapper.
    resetMapper(systemTime(SYSTEM_TIME_MONOTONIC));

    // Move a finger on the touchpad.
    args.clear();
    int32_t moveX = 500;
    args += process(EV_ABS, ABS_MT_SLOT, 0);
    args += process(EV_ABS, ABS_MT_TRACKING_ID, 3);
    args += process(EV_KEY, BTN_TOUCH, 1);
    args += process(EV_KEY, BTN_TOOL_FINGER, 1);
    setScanCodeState(KeyState::DOWN, {BTN_TOUCH, BTN_TOOL_FINGER});
    args += process(EV_ABS, ABS_MT_POSITION_X, moveX);
    args += process(EV_ABS, ABS_MT_POSITION_Y, 500);
    args += process(EV_ABS, ABS_MT_PRESSURE, 25);
    args += process(EV_SYN, SYN_REPORT, 0);
    for (int32_t i = 0; i < 3; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        moveX += 100;

        args += process(EV_ABS, ABS_MT_POSITION_X, moveX);
        args += process(EV_SYN, SYN_REPORT, 0);
    }
    // Again, we don't care about the detailed events, but check a cursor movement was detected.
    ASSERT_THAT(args, Contains(VariantWith<NotifyMotionArgs>(WithMotionAction(HOVER_MOVE))));
}

TEST_F(TouchpadInputMapperTest, ThreeFingerSwipeDisabledDuringRelativeCapture) {
    auto* touchpadMapper = static_cast<TouchpadInputMapper*>(mMapper.get());
    ASSERT_THAT(touchpadMapper->getGesturePropertyForTesting("Three Finger Swipe Enable")
                        ->getBoolValues(),
                ElementsAre(true));

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::RELATIVE;
    std::list<NotifyArgs> args;
    args = reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                             InputReaderConfiguration::Change::POINTER_CAPTURE);

    ASSERT_THAT(touchpadMapper->getGesturePropertyForTesting("Three Finger Swipe Enable")
                        ->getBoolValues(),
                ElementsAre(false));

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::UNCAPTURED;
    args = reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                             InputReaderConfiguration::Change::POINTER_CAPTURE);

    ASSERT_THAT(touchpadMapper->getGesturePropertyForTesting("Three Finger Swipe Enable")
                        ->getBoolValues(),
                ElementsAre(true));
}

TEST_F(TouchpadInputMapperTest, TouchpadHardwareState) {
    mReaderConfiguration.shouldNotifyTouchpadHardwareState = true;
    std::list<NotifyArgs> args =
            reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                              InputReaderConfiguration::Change::TOUCHPAD_SETTINGS);

    args += process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    args += process(EV_KEY, BTN_TOUCH, 1);
    setScanCodeState(KeyState::DOWN, {BTN_TOOL_FINGER});
    args += process(EV_KEY, BTN_TOOL_FINGER, 1);
    args += process(EV_ABS, ABS_MT_POSITION_X, 50);
    args += process(EV_ABS, ABS_MT_POSITION_Y, 50);
    args += process(EV_ABS, ABS_MT_PRESSURE, 1);
    args += process(EV_SYN, SYN_REPORT, 0);

    mFakePolicy->assertTouchpadHardwareStateNotified();
}

TEST_F(TouchpadInputMapperTest, TouchpadAccelerationDisabled) {
    mReaderConfiguration.touchpadAccelerationEnabled = false;
    mReaderConfiguration.touchpadPointerSpeed = 3;

    std::list<NotifyArgs> args =
            reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                              InputReaderConfiguration::Change::TOUCHPAD_SETTINGS);

    ASSERT_NO_FATAL_FAILURE(assertBothCurvesEqual(
            createFlatAccelerationCurve(mReaderConfiguration.touchpadPointerSpeed)));
}

TEST_F(TouchpadInputMapperTest, TouchpadAccelerationEnabled) {
    // Enable touchpad acceleration.
    mReaderConfiguration.touchpadAccelerationEnabled = true;
    mReaderConfiguration.touchpadPointerSpeed = 3;

    std::list<NotifyArgs> args =
            reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                              InputReaderConfiguration::Change::TOUCHPAD_SETTINGS);
    ASSERT_THAT(args, testing::IsEmpty());

    // Use createAccelerationCurveForPointerSensitivity to get expected curve segments.
    ASSERT_NO_FATAL_FAILURE(assertBothCurvesEqual(createAccelerationCurveForPointerSensitivity(
            mReaderConfiguration.touchpadPointerSpeed)));
}

TEST_F(TouchpadInputMapperTest, AccelerationDisabledDuringCapture) {
    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::RELATIVE;
    std::list<NotifyArgs> args =
            reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                              InputReaderConfiguration::Change::POINTER_CAPTURE);
    ASSERT_NO_FATAL_FAILURE(assertRelativeModeCurvesInUse());

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::UNCAPTURED;
    args = reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                             InputReaderConfiguration::Change::POINTER_CAPTURE);
    ASSERT_NO_FATAL_FAILURE(assertBothCurvesEqual(createAccelerationCurveForPointerSensitivity(
            mReaderConfiguration.touchpadPointerSpeed)));
}

TEST_F(TouchpadInputMapperTest, ExitingCaptureWithAccelerationDisabledDoesntReenable) {
    mReaderConfiguration.touchpadAccelerationEnabled = false;
    mReaderConfiguration.touchpadPointerSpeed = 3;
    std::list<NotifyArgs> args =
            reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                              InputReaderConfiguration::Change::TOUCHPAD_SETTINGS);

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::RELATIVE;
    args = reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                             InputReaderConfiguration::Change::POINTER_CAPTURE);
    ASSERT_NO_FATAL_FAILURE(assertRelativeModeCurvesInUse());

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::UNCAPTURED;
    args = reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                             InputReaderConfiguration::Change::POINTER_CAPTURE);

    ASSERT_NO_FATAL_FAILURE(assertBothCurvesEqual(
            createFlatAccelerationCurve(mReaderConfiguration.touchpadPointerSpeed)));
}

TEST_F(TouchpadInputMapperTest, ChangingSettingsDuringCaptureDoesntReenableAcceleration) {
    constexpr int32_t NEW_POINTER_SPEED = 3;
    ASSERT_NE(mReaderConfiguration.touchpadPointerSpeed, NEW_POINTER_SPEED);

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::RELATIVE;
    std::list<NotifyArgs> args =
            reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                              InputReaderConfiguration::Change::POINTER_CAPTURE);
    ASSERT_NO_FATAL_FAILURE(assertRelativeModeCurvesInUse());

    // Changing the pointer speed settings shouldn't result in an immediate change to the curve,
    // since we're in pointer capture.
    mReaderConfiguration.touchpadAccelerationEnabled = true;
    mReaderConfiguration.touchpadPointerSpeed = NEW_POINTER_SPEED;
    args = reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                             InputReaderConfiguration::Change::TOUCHPAD_SETTINGS);
    ASSERT_NO_FATAL_FAILURE(assertRelativeModeCurvesInUse());

    // ...but once we leave capture, the new speed should take effect.
    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::UNCAPTURED;
    args = reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                             InputReaderConfiguration::Change::POINTER_CAPTURE);
    ASSERT_NO_FATAL_FAILURE(
            assertBothCurvesEqual(createAccelerationCurveForPointerSensitivity(NEW_POINTER_SPEED)));
}

} // namespace android
