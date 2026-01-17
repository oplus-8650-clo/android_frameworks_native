/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <procpartition/procpartition.h>
#include <procpartition/apexcache.h>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <android-base/logging.h>
#include <filesystem>

namespace android {
namespace procpartition {

using apexcache::ApexCache;

std::ostream& operator<<(std::ostream& os, Partition p) {
    switch (p) {
        case Partition::SYSTEM: return os << "system";
        case Partition::SYSTEM_EXT: return os << "system_ext";
        case Partition::PRODUCT: return os << "product";
        case Partition::VENDOR: return os << "vendor";
        case Partition::ODM: return os << "odm";
        case Partition::UNKNOWN: // fallthrough
        default:
            return os << "(unknown)";
    }
}

std::string getExe(pid_t pid) {
    std::string exe;
    std::string real;
    if (!android::base::Readlink("/proc/" + std::to_string(pid) + "/exe", &exe)) {
        return "";
    }
    if (!android::base::Realpath(exe, &real)) {
        return "";
    }
    return real;
}

Partition getPartitionFromPreinstalledPath(const std::string& path) {
    if (android::base::StartsWith(path, "/system/")) {
        return Partition::SYSTEM;
    }
    if (android::base::StartsWith(path, "/system_ext/")) {
        return Partition::SYSTEM_EXT;
    }
    if (android::base::StartsWith(path, "/product/")) {
        return Partition::PRODUCT;
    }
    if (android::base::StartsWith(path, "/vendor/")) {
        return Partition::VENDOR;
    }
    if (android::base::StartsWith(path, "/odm/")) {
        return Partition::ODM;
    }
    return Partition::UNKNOWN;
}

Partition parseApex(const std::string& s) {
    std::filesystem::path p(s);
    auto it = p.begin();
    // Expecting components: /, apex, <apexName>, ...
    if (it == p.end() || *it != "/") return Partition::UNKNOWN;
    ++it; // -> apex
    if (it == p.end() || *it != "apex") return Partition::UNKNOWN;
    ++it; // -> <apexName>
    if (it == p.end()) return Partition::UNKNOWN;

    std::string apexName = it->string();

    size_t at = apexName.find('@');
    if (at != std::string::npos) {
        apexName = apexName.substr(0, at);
    }

    apexcache::ApexCache *instance = ApexCache::getInstance();
    for (const auto& info: instance->getCache(false /*invalidate*/)) {
        if (info.moduleName == apexName) {
            return getPartitionFromPreinstalledPath(info.preinstalledModulePath);
        }
    }
    LOG(INFO) << "parseApex did not find apexName: " << apexName;
    return Partition::UNKNOWN;
}

std::string getCmdline(pid_t pid) {
    std::string content;
    if (!android::base::ReadFileToString("/proc/" + std::to_string(pid) + "/cmdline", &content,
                                         false /* follow symlinks */)) {
        return "";
    }
    return std::string{content.c_str()};
}

Partition parsePartition(const std::string& s) {
    if (s == "system") {
        return Partition::SYSTEM;
    }
    if (s == "system_ext") {
        return Partition::SYSTEM_EXT;
    }
    if (s == "product") {
        return Partition::PRODUCT;
    }
    if (s == "vendor") {
        return Partition::VENDOR;
    }
    if (s == "odm") {
        return Partition::ODM;
    }
    return Partition::UNKNOWN;
}

Partition getPartitionFromRealpath(const std::string& path) {
    if (path == "/system/bin/app_process64" ||
        path == "/system/bin/app_process32") {

        return Partition::UNKNOWN; // cannot determine
    }
    size_t backslash = path.find_first_of('/', 1);
    std::string partition = (backslash != std::string::npos) ? path.substr(1, backslash - 1) : path;
    if (partition == "apex") {
        return parseApex(path);
    }

    return parsePartition(partition);
}

Partition getPartitionFromCmdline(pid_t pid) {
    const auto& cmdline = getCmdline(pid);
    if (cmdline == "system_server") {
        return Partition::SYSTEM;
    }
    if (cmdline.empty() || cmdline.front() != '/') {
        LOG(INFO) << "getPartitionFromCmdline empty or front not '/': " << cmdline;
        return Partition::UNKNOWN;
    }
    return getPartitionFromRealpath(cmdline);
}

Partition getPartitionFromExe(pid_t pid) {
    const auto& real = getExe(pid);
    if (real.empty() || real.front() != '/') {
        LOG(INFO) << "getPartitionFromExe empty or front not '/': " << real;
        return Partition::UNKNOWN;
    }
    return getPartitionFromRealpath(real);
}


Partition getPartition(pid_t pid) {
    Partition partition = getPartitionFromExe(pid);
    if (partition == Partition::UNKNOWN) {
        partition = getPartitionFromCmdline(pid);
    }
    return partition;
}

}  // namespace procpartition
}  // namespace android
