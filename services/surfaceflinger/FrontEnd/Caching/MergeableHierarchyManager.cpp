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

#include <FrontEnd/LayerHierarchy.h>

#include "MergeableHierarchyManager.h"

namespace android::surfaceflinger::frontend::caching {

void MergeableHierarchyManager::update(const LayerHierarchy& hierarchy) {
    std::vector<MergeableHierarchy> incomingHierarchies;
    MergeableHierarchy::Accumulator accumulator;
    update(&hierarchy, accumulator, incomingHierarchies);

    if (accumulator.canBuild()) {
        pushToIncomingHierarchy(accumulator, incomingHierarchies);
    }

    mMergeableHierarchies = std::move(incomingHierarchies);
}

void MergeableHierarchyManager::update(const LayerHierarchy* hierarchy,
                                       MergeableHierarchy::Accumulator& accumulator,
                                       std::vector<MergeableHierarchy>& incomingHierarchies) {
    if (!accumulator.add(hierarchy) && accumulator.canBuild()) {
        pushToIncomingHierarchy(accumulator, incomingHierarchies);
        accumulator = MergeableHierarchy::Accumulator();
    }

    for (auto& [childHierarchy, _] : hierarchy->mChildren) {
        update(childHierarchy, accumulator, incomingHierarchies);
    }
}

void MergeableHierarchyManager::constructSnapshots(
        LayerSnapshotBuilder& builder, const LayerSnapshotBuilder::Args& args,
        compositionengine::CompositionEngine& compositionEngine) {
    for (auto& hierarchy : mMergeableHierarchies) {
        hierarchy.constructSnapshot(builder, args, compositionEngine);
    }
}

} // namespace android::surfaceflinger::frontend::caching