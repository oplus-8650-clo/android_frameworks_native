/*
 * Copyright 2026 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <gui/WindowInfo.h>
#include <gui/WindowInfosUpdate.h>
#include <utils/StrongPointer.h>

#include <memory>
#include <vector>

#include "FakeInputTracingBackend.h"
#include "InputReaderTracer.h"
#include "InputTracingBackendInterface.h"

using ::testing::Field;

namespace android {

class InputReaderTracerTest : public testing::Test {
protected:
    std::shared_ptr<input_trace::VerifyingTrace> mTrace;
    std::shared_ptr<input_trace::FakeInputTracingBackend> mBackend;
    // A non-secure window, to be added at the start of all tests. This checks that the code under
    // test isn't just looking for the presence of any windows at all, without checking for them
    // being secure.
    gui::WindowInfo mInitialWindow;
    std::unique_ptr<InputReaderTracer> mTracer;

    void SetUp() override {
        mTrace = std::make_shared<input_trace::VerifyingTrace>();
        mBackend = std::make_shared<input_trace::FakeInputTracingBackend>(mTrace);
        mTracer = std::make_unique<InputReaderTracer>(mBackend);
        mInitialWindow.name = "Initial window";
    }
};

TEST_F(InputReaderTracerTest, InitialSecureWindowVisible) {
    gui::WindowInfo secureWindow;
    secureWindow.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;

    mTracer->onWindowInfosChanged({{mInitialWindow, secureWindow}, {}, 0, 0});

    mTracer->traceRawEvent({.when = 123});

    mTrace->expectAllRawEventsAtTimestampToMatchMetadata(123,
                                                         Field(&input_trace::TracedEventMetadata::
                                                                       isSecure,
                                                               true));
    mTrace->verifyExpectedEventsTraced();
}

TEST_F(InputReaderTracerTest, SecureWindowAppearsAfterInitialization) {
    mTracer->onWindowInfosChanged({{mInitialWindow}, {}, 0, 0});
    mTracer->traceRawEvent({.when = 123});

    mTrace->expectAllRawEventsAtTimestampToMatchMetadata(123,
                                                         Field(&input_trace::TracedEventMetadata::
                                                                       isSecure,
                                                               false));

    gui::WindowInfo secureWindow;
    secureWindow.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    mTracer->onWindowInfosChanged({{mInitialWindow, secureWindow}, {}, 0, 0});

    mTracer->traceRawEvent({.when = 456});

    mTrace->expectAllRawEventsAtTimestampToMatchMetadata(456,
                                                         Field(&input_trace::TracedEventMetadata::
                                                                       isSecure,
                                                               true));
    mTrace->verifyExpectedEventsTraced();
}

TEST_F(InputReaderTracerTest, IgnoresInvisibleOrNoInputChannelSecureWindows) {
    mTracer->onWindowInfosChanged({{mInitialWindow}, {}, 0, 0});

    gui::WindowInfo secureInvisibleWindow;
    secureInvisibleWindow.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    secureInvisibleWindow.inputConfig |= gui::WindowInfo::InputConfig::NOT_VISIBLE;

    gui::WindowInfo secureNoInputChannelWindow;
    secureNoInputChannelWindow.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    secureNoInputChannelWindow.inputConfig |= gui::WindowInfo::InputConfig::NO_INPUT_CHANNEL;

    mTracer->onWindowInfosChanged(
            {{mInitialWindow, secureInvisibleWindow, secureNoInputChannelWindow}, {}, 0, 0});

    mTracer->traceRawEvent({.when = 123});

    mTrace->expectAllRawEventsAtTimestampToMatchMetadata(123,
                                                         Field(&input_trace::TracedEventMetadata::
                                                                       isSecure,
                                                               false));
    mTrace->verifyExpectedEventsTraced();
}

} // namespace android
