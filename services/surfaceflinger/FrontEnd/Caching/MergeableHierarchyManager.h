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

#pragma once

#include <memory>
#include "FrontEnd/Caching/MergeableHierarchy.h"
#include "compositionengine/CompositionEngine.h"

namespace android::surfaceflinger::frontend {

namespace caching {

// Manages the lifecycle of MergeableHierarchies constructed from the layer graph
class MergeableHierarchyManager {
public:
    void update(const LayerHierarchy& hierarchy);

    void constructSnapshots(LayerSnapshotBuilder& builder, const LayerSnapshotBuilder::Args& args,
                            compositionengine::CompositionEngine& compositionEngine);

    MergeableHierarchy* getOwnedHierarchy(uint32_t id) const {
        auto hierarchy =
                std::find_if(mMergeableHierarchies.begin(), mMergeableHierarchies.end(),
                             [id](const auto& hierarchy) { return hierarchy->getId() == id; });

        if (hierarchy != mMergeableHierarchies.end()) {
            return hierarchy->get();
        } else {
            return nullptr;
        }
    }

    bool isMemberOfAnyHierarchy(uint32_t id) const {
        return std::any_of(mMergeableHierarchies.cbegin(), mMergeableHierarchies.cend(),
                           [id](const auto& hierarchy) { return hierarchy->hasLayer(id); });
    }

    // Dumps all tracked MergeableHiearchies to a string
    std::string dump() const {
        std::ostringstream os;
        dump(os);
        return os.str();
    }

    // Dumps all tracked MergeableHiearchies to the supplied ostream
    void dump(std::ostream& out) const {
        out << "\nMergeable Hierarchies\n";
        for (const auto& mergeableHierarchy : mMergeableHierarchies) {
            out << "  ";
            mergeableHierarchy->dump(out);
            out << "\n";
        }
    }

private:
    void add(std::unique_ptr<MergeableHierarchy>&& mergeableHierarchy) {
        mMergeableHierarchies.emplace_back(std::move(mergeableHierarchy));
    }
    void remove(uint32_t id) {
        std::erase_if(mMergeableHierarchies, [id](const auto& mergeableHierarchy) {
            return mergeableHierarchy->getId() == id;
        });
    }
    void update(const LayerHierarchy* hierarchy, MergeableHierarchy::Accumulator& accumulator);
    // TODO: use a better data structure for this. Conceptually we want a set of sets
    // so that destroying a LayerHierarchy won't cause a linear time search.
    std::vector<std::unique_ptr<caching::MergeableHierarchy>> mMergeableHierarchies;
};

} // namespace caching

} // namespace android::surfaceflinger::frontend