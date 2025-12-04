/*
 * Copyright 2025 The Android Open Source Project
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
#include "common/Panopticon.h"
#include <string_view>
#include "common/trace.h"

namespace android::panopticon {

namespace {

std::string_view validateAndStringify(SliceType type) {
    auto name = ftl::enum_name(type);
    LOG_ALWAYS_FATAL_IF(!name, "Invalid type: %d", type);
    return *name;
}

// lol imagine having to debug this
thread_local ftl::SmallMap<std::string, std::shared_ptr<Panopticon>, 10> sPanopticons;
// Stack of IDs to exclusively trace.
// When this is empty, then all panopticons are traced.
thread_local Ids sExclusiveIdStack;

std::optional<SliceToken> slice(std::string id, SliceType sliceType) {
    if (auto panopticon = sPanopticons.get(id.c_str()); panopticon) {
        return panopticon->get()->makeSlice(sliceType);
    }

    return std::nullopt;
}

} // namespace

Panopticon::Panopticon(Source source, const char* suffix) : mSource(source) {
    static std::atomic_int64_t sSeed = 0;
    mBaseCookie = static_cast<int16_t>(sSeed++) << 16;
    mTrack = std::string(
            ftl::Concat(ftl::truncated<32>(getSourceName()), " ", ftl::truncated<32>(suffix))
                    .c_str());
    mKey = std::string(suffix);
    sliceForType(topSlice()).init(getTrackName(), topSlice(), mBaseCookie + 1);
    mTracingCookie = mBaseCookie + 1;
}

Panopticon::~Panopticon() {
    SFTRACE_CALL();
    // TODO(alecmouri): Also package this up into a bow and upload to statsd
    for (auto& slice : mSlices) {
        slice.terminate(getTrackName());
    }
}

void Slice::init(std::string_view track, SliceType slice, int32_t tracingCookie) {
    startTime = systemTime();
    isTracing = true;
    cookie = tracingCookie;
    SFTRACE_ASYNC_FOR_TRACK_BEGIN(track.data(), validateAndStringify(slice).data(), tracingCookie);
}

void Slice::terminate(std::string_view track) {
    if (isTracing.exchange(false)) {
        endTime = systemTime();
        SFTRACE_ASYNC_FOR_TRACK_END(track.data(), cookie);
    }
}

SliceToken::SliceToken(SliceType type, const std::shared_ptr<Panopticon>& parent)
      : mType(type), mKey(parent->getKey()), mSliceCookie(++parent->mTracingCookie) {
    parent->sliceForType(mType).init(parent->getTrackName(), mType, mSliceCookie);
}

SliceToken::~SliceToken() {
    if (mKey != "") {
        if (auto panopticon = sPanopticons.get(mKey.c_str()); panopticon) {
            panopticon->get()->sliceForType(mType).terminate(panopticon->get()->getTrackName());
        }
    }
}

void PanopticonRegistration::start() {
    if (mPanopticon) {
        sPanopticons.try_emplace(mId, std::move(mPanopticon));
    }
}

PanopticonRegistration::~PanopticonRegistration() {
    sPanopticons.erase(mId);
}

ExclusiveToken::ExclusiveToken(std::string id) {
    sExclusiveIdStack.emplace_back(std::move(id));
}

ExclusiveToken::~ExclusiveToken() {
    sExclusiveIdStack.pop_back();
}

void make(std::string id, Source source) {
    if (auto panopticon = sPanopticons.get(id.c_str()); panopticon) {
        ALOGV("Clobbering metrics for: %s, %s", id.c_str(),
              panopticon->get()->getSourceName().data());
    }

    auto panopticon = Panopticon::make(source, id.c_str());
    sPanopticons.try_emplace(std::move(id), std::move(panopticon));
}

std::shared_ptr<PanopticonRegistration> share() {
    std::string view = "";

    if (sExclusiveIdStack.size() > 0) {
        view = sExclusiveIdStack.back();
    } else if (sPanopticons.size() == 1) {
        view = sPanopticons.begin()->first;
    }

    if (view != "") {
        if (auto panopticon = sPanopticons.get(view); panopticon) {
            return std::make_shared<PanopticonRegistration>(std::move(view), panopticon->get());
        }
    }
    return std::make_shared<PanopticonRegistration>("", nullptr);
}

PanopticonRegistrations share(Ids ids) {
    PanopticonRegistrations registrations;
    for (const auto& id : ids) {
        if (auto panopticon = sPanopticons.get(id); panopticon) {
            registrations.emplace_back(
                    std::make_shared<PanopticonRegistration>(id, panopticon->get()));
        }
    }
    return registrations;
}

void terminate(std::string id) {
    sPanopticons.erase(std::move(id));
}

void terminate() {
    sPanopticons.clear();
}

ExclusiveToken exclusive(std::string id) {
    return ExclusiveToken(std::move(id));
}

SliceTokens slice(SliceType sliceType) {
    SliceTokens tokens;
    if (sExclusiveIdStack.size() > 0) {
        auto token = slice(sExclusiveIdStack.back(), sliceType);
        if (token) {
            tokens.push_back(std::move(*token));
        }
    } else {
        for (const auto& [id, _] : sPanopticons) {
            auto token = slice(id, sliceType);
            if (token) {
                tokens.push_back(std::move(*token));
            }
        }
    }
    return tokens;
}

void make(Ids ids, Source source) {
    for (const auto& id : ids) {
        make(id, source);
    }
}
} // namespace android::panopticon
