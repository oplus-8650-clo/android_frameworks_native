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
#include <iterator>
#include <optional>
#include "FrontEnd/LayerSnapshot.h"
#include "LayerFE.h"
#include "ScreenCaptureOutput.h"
#include "renderengine/impl/ExternalTexture.h"

#include "MergeableHierarchy.h"

namespace android::surfaceflinger::frontend::caching {

bool MergeableHierarchy::Accumulator::add(const LayerHierarchy* hierarchy) {
    // TODO: Add a check for whether we actually want to add the hierarchy
    // For now, unconditionally add the hierarchy
    mHierarchies.push_back(
            {.layerId = hierarchy->getLayer() ? hierarchy->getLayer()->id : UNASSIGNED_LAYER_ID,
             .hierarchy = hierarchy});
    return true;
}

void MergeableHierarchy::constructSnapshot(
        LayerSnapshotBuilder& builder, const LayerSnapshotBuilder::Args& args,
        compositionengine::CompositionEngine& compositionEngine) {
    SFTRACE_CALL();
    if (mSnapshot) {
        return;
    }

    auto localArgs = args;
    localArgs.forceUpdate = LayerSnapshotBuilder::ForceUpdateFlags::ALL;

    std::vector<std::unique_ptr<LayerSnapshot>> snapshots;
    constructSnapshotForHierarchy(builder, localArgs, mHierarchies.front().hierarchy,
                                  localArgs.rootSnapshot, snapshots);

    materializeSnapshot(std::move(snapshots), compositionEngine);
}

void MergeableHierarchy::constructSnapshotForHierarchy(
        LayerSnapshotBuilder& builder, const LayerSnapshotBuilder::Args& args,
        const LayerHierarchy* hierarchy, const LayerSnapshot& parent,
        std::vector<std::unique_ptr<LayerSnapshot>>& outSnapshots) {
    auto snapshot = std::make_unique<LayerSnapshot>();

    if (hierarchy->getLayer()) {
        *snapshot = LayerSnapshot(*hierarchy->getLayer(), LayerHierarchy::TraversalPath::ROOT);
    } else {
        *snapshot = args.rootSnapshot;
        snapshot->geomLayerBounds = FloatRect(0, 0, 3000, 3000);
    }

    if (hierarchy->getLayer()) {
        snapshot->merge(*hierarchy->getLayer(), /*forceUpdate=*/true, /*displayChanges=*/true,
                        args.forceFullDamage, 0u);
        builder.updateSnapshot(*snapshot, args, *hierarchy->getLayer(), parent,
                               LayerHierarchy::TraversalPath::ROOT);
    }

    std::vector<std::unique_ptr<LayerSnapshot>> children;
    for (const auto& [child, _] : hierarchy->mChildren) {
        constructSnapshotForHierarchy(builder, args, child, *snapshot, children);
    }

    outSnapshots.emplace_back(std::move(snapshot));
    std::move(children.begin(), children.end(), std::back_inserter(outSnapshots));
}

void MergeableHierarchy::materializeSnapshot(
        std::vector<std::unique_ptr<LayerSnapshot>> snapshots,
        compositionengine::CompositionEngine& compositionEngine) {
    auto& firstSnapshot = *snapshots.begin();
    auto bounds = Rect(firstSnapshot->sourceBounds());
    auto width = std::min(3000u, static_cast<uint32_t>(bounds.getWidth()));
    auto height = std::min(3000u, static_cast<uint32_t>(bounds.getHeight()));

    auto buffer = sp<GraphicBuffer>::make(width, height, PIXEL_FORMAT_RGBA_8888, 1u,

                                          static_cast<uint64_t>(GRALLOC_USAGE_HW_COMPOSER |
                                                                GRALLOC_USAGE_HW_RENDER |
                                                                GRALLOC_USAGE_HW_TEXTURE),
                                          std::format("flattenedHierarchy{}", getId()));

    ALOGE_IF(buffer->initCheck() != OK, "Failed to init buffer for EH: %d %" PRIu32 " %" PRIu32,
             buffer->initCheck(), width, height);

    auto texture = std::make_shared<
            renderengine::impl::ExternalTexture>(buffer, compositionEngine.getRenderEngine(),
                                                 renderengine::impl::ExternalTexture::Usage::
                                                         WRITEABLE);

    std::shared_ptr<ScreenCaptureOutput> output = createScreenCaptureOutput(
            ScreenCaptureOutputArgs{.compositionEngine = compositionEngine,
                                    .colorProfile = {},
                                    .layerStack = firstSnapshot->outputFilter.layerStack,
                                    .sourceCrop = bounds,
                                    .buffer = texture,
                                    .displayIdVariant = std::nullopt,
                                    .reqBufferSize = ui::Size(width, height),
                                    .sdrWhitePointNits = -1,
                                    .displayBrightnessNits = -1,
                                    .targetBrightness = -1,
                                    .layerAlpha = 1.0f,
                                    .disableBlur = false,
                                    .treat170mAsSrgb = false,
                                    .dimInGammaSpaceForEnhancedScreenshots = false,
                                    .isSecure = true,
                                    .enableLocalTonemapping = false,
                                    .debugName = "HierarchyFlattener"});

    std::vector<sp<compositionengine::LayerFE>> ceLayerFEs;
    std::vector<sp<LayerFE>> layerFEs;
    for (auto& snapshot : snapshots) {
        if (!snapshot->hasSomethingToDraw()) {
            continue;
        }
        auto layerFE = sp<LayerFE>::make("Hierarchy");
        layerFE->mSnapshot = std::move(snapshot);
        layerFEs.emplace_back(layerFE);
        ceLayerFEs.emplace_back(layerFE);
    }

    sp<LayerFE> firstLayer = layerFEs.back();

    compositionengine::CompositionRefreshArgs refreshArgs{
            .outputs = {output},
            .layers = std::move(ceLayerFEs),
            .updatingOutputGeometryThisFrame = true,
            .updatingGeometryThisFrame = true,
    };
    compositionEngine.present(refreshArgs);

    mSnapshot = std::move(firstLayer->mSnapshot);
    mSnapshot->externalTexture = texture;
    mSnapshot->acquireFence = output->getRenderSurface()->getClientTargetAcquireFence();
}

void MergeableHierarchy::dump(std::ostream& out) const {
    out << "id = " << getId() << ", hierarchies = {";
    for (const auto& hierarchy : mHierarchies) {
        out << hierarchy.layerId << ",";
    }
    out << "}";
}

} // namespace android::surfaceflinger::frontend::caching