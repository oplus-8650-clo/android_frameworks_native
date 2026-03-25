/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <attention/InteractionProvider.h>

namespace android::attention {

void InteractionProvider::reportInteraction(InteractionType interactionType,
                                            std::chrono::milliseconds interactionTime) {
    std::scoped_lock lock(mMutex);
    mInteractions[static_cast<int32_t>(interactionType)] = interactionTime;
}

std::vector<InteractionState> InteractionProvider::getSourceInteractions() {
    std::scoped_lock lock(mMutex);
    std::vector<InteractionState> interactions;
    for (const auto& [type, time] : mInteractions) {
        InteractionState interactionState;
        interactionState.interactionTypes = type;
        interactionState.interactionTimeMillis = time.count();
        interactions.push_back(interactionState);
    }
    return interactions;
}

} // namespace android::attention
