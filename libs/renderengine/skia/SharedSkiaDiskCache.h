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

#pragma once

#include <FileBlobCache.h>
#include <SkRefCnt.h>
#include <utils/Mutex.h>

#include <mutex>
#include <string>
#include <vector>

class SkData;

namespace android {
namespace uirenderer {
namespace skiapipeline {

// SharedSkiaDiskCache attempts to isolate the management of the FileBlobCache from
// any interface obligations (c.f. b/487728260).
//
// To use this class one should:
//    Call setFilename() to enable the rest of the API. If setFilename() isn't called
//    the load() and store() calls won't do anything.
//
//    Before load/store are called, call init(). If the filename has been set this
//    will load the cache contents.
class SharedSkiaDiskCache {
public:
    /**
     * "setFilename" sets the name of the file that should be used to store
     * cache contents from one program invocation to another. This function does not perform any
     * disk operation and it should be invoked before "init".
     */
    void setFilename(const char* filename) EXCLUDES(mMutex);

    /**
     * init" loads the cache contents from disk,
     * checks that the on-disk cache matches a provided identity,
     * and puts the Cache into an initialized state, such that it is
     * able to insert and retrieve entries from the cache. If identity is
     * non-null and validation fails, the cache is initialized but contains
     * no data. If size is less than zero, the cache is initialized but
     * contains no data.
     *
     * When not in the initialized state the load and store methods will return without
     * performing any cache operations.
     * Returns if validation succeeded.
     */
    bool init(const void* identity, ssize_t size) EXCLUDES(mMutex);

    /**
     * "load" attempts to retrieve the value blob associated with a given key
     * from cache.
     */
    sk_sp<SkData> load(const SkData& key) EXCLUDES(mMutex);

    /**
     * "store" attempts to insert a new key/value blob pair into the cache.
     */
    bool store(const SkData& key, const SkData& data) NO_THREAD_SAFETY_ANALYSIS;

protected:
    // Creation and (the lack of) destruction is handled internally.
    SharedSkiaDiskCache(size_t maxKeySize, size_t maxValueSize, size_t maxTotalSize);

    /**
     *  The time in milliseconds to wait before saving newly inserted cache entries.
     *
     *  WARNING: setting this to 0 will disable writing the cache to disk.
     */
    unsigned int mDeferredSaveDelayMs = 4 * 1000;

private:
    // Copying is disallowed.
    SharedSkiaDiskCache(const SharedSkiaDiskCache&) = delete;
    void operator=(const SharedSkiaDiskCache&) = delete;

    /**
     * "validateCache" updates the cache to match the given identity.  If the
     * cache currently has the wrong identity, all entries in the cache are cleared.
     */
    bool validateCache(const void* identity, ssize_t size) REQUIRES(mMutex);

    /**
     * Helper for BlobCache::set to trace the result and ensure the identity hash
     * does not get evicted.
     */
    void set(const void* key, size_t keySize, const void* value, size_t valueSize) REQUIRES(mMutex);

    /**
     * "saveToDiskLocked" attempts to save the current contents of the cache to
     * disk. If the identity hash exists, we will insert the identity hash into
     * the cache for next validation.
     */
    void saveToDiskLocked() REQUIRES(mMutex);

    /**
     * "mInitialized" indicates whether the Cache is in the initialized
     * state.  It is initialized to false at construction time, and gets set to
     * true when init is called.
     * When in this state, the cache behaves as normal.  When not,
     * the load and store methods will return without performing any cache
     * operations.
     */
    bool mInitialized GUARDED_BY(mMutex) = false;

    /**
     * "mBlobCache" is the cache in which the key/value blob pairs are stored.
     * The blob cache contains the Android build number. We treat version mismatches
     * as an empty cache (logic implemented in BlobCache::unflatten).
     */
    std::unique_ptr<FileBlobCache> mBlobCache GUARDED_BY(mMutex);

    /**
     * "mFilename" is the name of the file for storing cache contents in between
     * program invocations.  It is initialized to an empty string at
     * construction time, and can be set with the setCacheFilename method.  An
     * empty string indicates that the cache should not be saved to or restored
     * from disk.
     */
    std::string mFilename GUARDED_BY(mMutex);

    /**
     * "mIDHash" is the current identity hash for the cache validation. It is
     * initialized to an empty vector at construction time, and its content is
     * generated in the call of the validateCache method. An empty vector
     * indicates that cache validation is not performed, and the hash should
     * not be stored on disk.
     */
    std::vector<uint8_t> mIDHash GUARDED_BY(mMutex);

    /**
     * "mSavePending" indicates whether or not a deferred save operation is
     * pending.  Each time a key/value pair is inserted into the cache via
     * store, a deferred save is initiated if one is not already pending.
     * This will wait some amount of time and then trigger a save of the cache
     * contents to disk, unless mDeferredSaveDelayMs is 0 in which case saving
     * is disabled.
     */
    bool mSavePending GUARDED_BY(mMutex) = false;

    /**
     * "mMutex" prevents concurrent access to the member variables. It must be
     * locked whenever the member variables are accessed.
     */
    std::mutex mMutex;

    static constexpr size_t kInvalidCacheSize = std::numeric_limits<size_t>::max();

    /**
     * "sIDKey" is the cache key of the identity hash
     */
    static constexpr uint32_t sIDKey = 0;

    // Ctor parameters - passed on to FileBlobCache in init()
    const size_t mMaxKeySize;
    const size_t mMaxValueSize;
    const size_t mMaxTotalSize;

    friend class SharedSkiaDiskCacheUtils; // used for unit testing
};

} /* namespace skiapipeline */
} /* namespace uirenderer */
} /* namespace android */
