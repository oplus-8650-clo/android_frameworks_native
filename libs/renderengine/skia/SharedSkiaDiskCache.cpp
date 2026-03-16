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

#include "SharedSkiaDiskCache.h"

#include "FileBlobCache.h"

#include <SkData.h>
#include <gui/TraceUtils.h>
#include <include/gpu/graphite/Context.h>
#include <log/log.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <thread>

namespace android {
namespace uirenderer {
namespace skiapipeline {

SharedSkiaDiskCache::SharedSkiaDiskCache(size_t maxKeySize, size_t maxValueSize,
                                         size_t maxTotalSize)
      : mMaxKeySize(maxKeySize), mMaxValueSize(maxValueSize), mMaxTotalSize(maxTotalSize) {
    // There is an "incomplete FileBlobCache type" compilation error, if ctor is moved to header.
}

void SharedSkiaDiskCache::setFilename(const char* filename) {
    std::lock_guard lock(mMutex);
    mFilename = filename;
}

bool SharedSkiaDiskCache::validateCache(const void* identity, ssize_t size) {
    ATRACE_NAME("SharedSkiaDiskCache::validateCache");
    if (nullptr == identity && size == 0) {
        return true;
    }

    if (nullptr == identity || size < 0) {
        mBlobCache->clear();
        return false;
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    SHA256_Update(&ctx, identity, size);
    mIDHash.resize(SHA256_DIGEST_LENGTH);
    SHA256_Final(mIDHash.data(), &ctx);

    std::array<uint8_t, SHA256_DIGEST_LENGTH> hash;
    uint32_t key = sIDKey;
    size_t loaded = mBlobCache->get(&key, sizeof(key), hash.data(), hash.size());

    if (loaded && std::equal(hash.begin(), hash.end(), mIDHash.begin())) {
        return true;
    }

    mBlobCache->clear();
    return false;
}

bool SharedSkiaDiskCache::init(const void* identity, ssize_t size) {
    ATRACE_NAME("SharedSkiaDiskCache::init");
    std::lock_guard lock(mMutex);

    bool validationSucceeded = false;

    if (mFilename.length() > 0) {
        mBlobCache.reset(new FileBlobCache(mMaxKeySize, mMaxValueSize, mMaxTotalSize, mFilename));
        validationSucceeded = this->validateCache(identity, size);
        mInitialized = true;
        if (identity != nullptr && size > 0 && mIDHash.size()) {
            set(&sIDKey, sizeof(sIDKey), mIDHash.data(), mIDHash.size());
        }
    }

    return validationSucceeded;
}

sk_sp<SkData> SharedSkiaDiskCache::load(const SkData& key) {
    ATRACE_NAME("SharedSkiaDiskCache::load");

    const size_t keySize = key.size();

    std::lock_guard lock(mMutex);
    if (!mInitialized) {
        return nullptr;
    }

    size_t testValueSize = mBlobCache->get(key.data(), keySize, nullptr, 0);
    if (testValueSize > mMaxValueSize || testValueSize == 0) {
        return nullptr;
    }

    // Allocate a buffer with malloc. SkData takes ownership of that allocation and will call free.
    void* valueBuffer = malloc(testValueSize);
    if (!valueBuffer) {
        return nullptr;
    }
    size_t realValueSize = mBlobCache->get(key.data(), keySize, valueBuffer, testValueSize);
    if (realValueSize != testValueSize) {
        free(valueBuffer);
        return nullptr;
    }

    return SkData::MakeFromMalloc(valueBuffer, realValueSize);
}

void SharedSkiaDiskCache::set(const void* key, size_t keySize, const void* value,
                              size_t valueSize) {
    switch (mBlobCache->set(key, keySize, value, valueSize)) {
        case BlobCache::InsertResult::kInserted:
            // This is what we expect/hope. It means the cache is large enough.
            return;
        case BlobCache::InsertResult::kDidClean: {
            ATRACE_FORMAT("SharedSkiaDiskCache: evicted an entry to fit {key: %lu value "
                          "%lu}!",
                          keySize, valueSize);
            if (mIDHash.size()) {
                set(&sIDKey, sizeof(sIDKey), mIDHash.data(), mIDHash.size());
            }
            return;
        }
        case BlobCache::InsertResult::kNotEnoughSpace: {
            ATRACE_FORMAT("SharedSkiaDiskCache: could not fit {key: %lu value %lu}!", keySize,
                          valueSize);
            return;
        }
        case BlobCache::InsertResult::kInvalidValueSize:
        case BlobCache::InsertResult::kInvalidKeySize: {
            ATRACE_FORMAT("SharedSkiaDiskCache: invalid size {key: %lu value %lu}!", keySize,
                          valueSize);
            return;
        }
        case BlobCache::InsertResult::kKeyTooBig:
        case BlobCache::InsertResult::kValueTooBig:
        case BlobCache::InsertResult::kCombinedTooBig: {
            ATRACE_FORMAT("SharedSkiaDiskCache: entry too big: {key: %lu value %lu}!", keySize,
                          valueSize);
            return;
        }
    }
}

void SharedSkiaDiskCache::saveToDiskLocked() {
    ATRACE_NAME("SharedSkiaDiskCache::saveToDiskLocked");
    if (mInitialized) {
        mBlobCache->writeToFile();
    }
}

bool SharedSkiaDiskCache::store(const SkData& key, const SkData& data) {
    ATRACE_NAME("SharedSkiaDiskCache::store");

    std::unique_lock<std::mutex> lock(mMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return false;
    }

    if (!mInitialized) {
        return false;
    }

    size_t valueSize = data.size();
    size_t keySize = key.size();
    if (keySize == 0 || keySize > mMaxKeySize || valueSize == 0 || valueSize > mMaxValueSize) {
        ALOGW("SharedSkiaDiskCache::store: sizes %d %d not allowed - maxs %zu %zu", (int)keySize,
              (int)valueSize, mMaxKeySize, mMaxValueSize);
        return false;
    }

    set(key.data(), keySize, data.data(), valueSize);

    // The disk cache is out of date - try to update it
    if (!mSavePending && mDeferredSaveDelayMs > 0) {
        mSavePending = true;
        std::thread deferredSaveThread([this]() {
            usleep(mDeferredSaveDelayMs * 1000); // milliseconds to microseconds
            std::lock_guard lock(mMutex);
            saveToDiskLocked();
            mSavePending = false;
        });
        deferredSaveThread.detach();
    }

    return true;
}

} /* namespace skiapipeline */
} /* namespace uirenderer */
} /* namespace android */
