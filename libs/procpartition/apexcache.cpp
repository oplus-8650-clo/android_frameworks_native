/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <procpartition/apexcache.h>

#include <android-base/logging.h>
#include <tinyxml2.h>

namespace android {
namespace apexcache {

using namespace tinyxml2;

ApexCache* ApexCache::getInstance() {
    static ApexCache *instance = new ApexCache();
    return instance;
}

ApexCache::ApexCache() {}

const std::vector<ApexInfo>& ApexCache::getCache(bool invalidate) {
    if (invalidate) {
        cache.clear();
    }
    if (cache.empty()) {
        XMLDocument doc;
        if (doc.LoadFile("/apex/apex-info-list.xml") != XML_SUCCESS) {
            LOG(ERROR) << "Failed to load /apex/apex-info-list.xml";
            return cache;
        }

        XMLElement* root = doc.FirstChildElement("apex-info-list");
        if (root == nullptr) {
            LOG(ERROR) << "Missing apex-info-list root element";
            return cache;
        }

        for (XMLElement* child = root->FirstChildElement("apex-info");
             child != nullptr; child = child->NextSiblingElement("apex-info")) {
            ApexInfo info;
            const char* moduleName = child->Attribute("moduleName");
            if (moduleName) info.moduleName = moduleName;

            const char* preinstalledModulePath = child->Attribute("preinstalledModulePath");
            if (preinstalledModulePath) info.preinstalledModulePath = preinstalledModulePath;
            cache.push_back(info);
        }
    }
    return cache;
}

} // namespace apexcache
} // namespace android
