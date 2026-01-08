/*
 * Copyright 2024 The Android Open Source Project
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

#include "JoystickInputMapper.h"

#include <list>
#include <optional>

#include <EventHub.h>
#include <NotifyArgs.h>
#include <ftl/flags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <input/DisplayViewport.h>
#include <linux/input-event-codes.h>
#include <ui/LogicalDisplayId.h>

#include "InputMapperTest.h"
#include "TestConstants.h"
#include "TestEventMatchers.h"

namespace android {

using namespace ftl::flag_operators;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::Return;
using testing::VariantWith;

class JoystickInputMapperTest : public InputMapperUnitTest {
protected:
    void SetUp() override {
        InputMapperUnitTest::SetUp();
        EXPECT_CALL(mMockEventHub, getDeviceClasses(EVENTHUB_ID))
                .WillRepeatedly(Return(InputDeviceClass::JOYSTICK | InputDeviceClass::EXTERNAL));

        // The mapper requests info on all ABS axis IDs, including ones which aren't actually used
        // (e.g. in the range from 0x0b (ABS_BRAKE) to 0x0f (ABS_HAT0X)), so just return nullopt for
        // all axes we don't explicitly set up below.
        EXPECT_CALL(mMockEventHub, getAbsoluteAxisInfo(EVENTHUB_ID, testing::_))
                .WillRepeatedly(Return(std::nullopt));

        setupAxis(ABS_X, /*valid=*/true, /*min=*/-127, /*max=*/127, /*resolution=*/0);
        setupAxis(ABS_Y, /*valid=*/true, /*min=*/-127, /*max=*/127, /*resolution=*/0);
    }
};

TEST_F(JoystickInputMapperTest, Configure_AssignsDisplayUniqueId) {
    DisplayViewport viewport;
    viewport.displayId = ui::LogicalDisplayId{1};
    EXPECT_CALL((*mDevice), getAssociatedViewport).WillRepeatedly(Return(viewport));
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext,
                                                     mFakePolicy->getReaderConfiguration());

    std::list<NotifyArgs> out;

    // Send an axis event
    process(EV_ABS, ABS_X, 100);
    mFakeListener.assertNoEvents();
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithDisplayId(viewport.displayId));

    // Send another axis event
    process(EV_ABS, ABS_Y, 100);
    mFakeListener.assertNoEvents();
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithDisplayId(viewport.displayId));
}

TEST_F(JoystickInputMapperTest, MappedAxes_PrioritizesMostRecentUpdate) {
    // Two raw axes, ABS_X and ABS_Y, will be mapped to the same logical axis,
    // AMOTION_EVENT_AXIS_X. We need to ensure that the most recent event is the one
    // that gets reported.
    AxisInfo axisInfo;
    axisInfo.axis = AMOTION_EVENT_AXIS_X;
    EXPECT_CALL(mMockEventHub, mapAxis(EVENTHUB_ID, ABS_X, testing::_))
            .WillRepeatedly(testing::DoAll(testing::SetArgPointee<2>(axisInfo), Return(false)));
    EXPECT_CALL(mMockEventHub, mapAxis(EVENTHUB_ID, ABS_Y, testing::_))
            .WillRepeatedly(testing::DoAll(testing::SetArgPointee<2>(axisInfo), Return(false)));

    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext,
                                                     mFakePolicy->getReaderConfiguration());
    nsecs_t eventTime = ARBITRARY_TIME;

    // Send an event for ABS_X with a value of 0 (maps to 0).
    process(eventTime, EV_ABS, ABS_X, 0);
    // Send an event for ABS_Y at a later time with a value of 127 (maps to 1.0).
    eventTime++;
    process(eventTime, EV_ABS, ABS_Y, 127);

    // Sync and verify: ABS_Y value should be taken
    process(eventTime, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(testing::AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                              WithAxes({{AMOTION_EVENT_AXIS_X, 1.0f}})));

    // Reset and test reverse order of events
    ASSERT_THAT(mMapper->reset(eventTime), IsEmpty());
    // Send an event for ABS_Y with a value of 127 (maps to 1.0).
    process(eventTime, EV_ABS, ABS_Y, 127);
    // Send an event for ABS_X at a later time with a value of 0 (maps to 0).
    eventTime++;
    process(eventTime, EV_ABS, ABS_X, 0);

    // Sync and verify: ABS_X value should be taken
    process(eventTime, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(testing::AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                              WithAxes({{AMOTION_EVENT_AXIS_X, 0}})));
}

} // namespace android
