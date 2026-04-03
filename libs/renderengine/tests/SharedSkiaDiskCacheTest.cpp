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

#include "../skia/SharedSkiaDiskCache.h"

#include <SkData.h>

#include <dirent.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>

using namespace android::uirenderer::skiapipeline;

namespace android {
namespace uirenderer {
namespace skiapipeline {

class SharedSkiaDiskCacheUtils {
public:
    /**
     * Access the protected constructor
     */
    static SharedSkiaDiskCache Make(size_t maxKeySize, size_t maxValueSize, size_t maxTotalSize) {
        return SharedSkiaDiskCache(maxKeySize, maxValueSize, maxTotalSize);
    }

    /**
     * Turn off the cache's ability to save to disk.
     */
    static void DisableDiskWrite(SharedSkiaDiskCache& cache) {
        std::lock_guard lock(cache.mMutex);
        cache.mDeferredSaveDelayMs = 0;
    }

    /**
     * Get the size the cache will occupy on disk.
     */
    static size_t GetFlattenedSize(SharedSkiaDiskCache& cache) {
        std::lock_guard lock(cache.mMutex);
        return cache.mBlobCache ? cache.mBlobCache->getSize() : 0;
    }

    /**
     * Release the in-memory cache after, optionally, writing it to disk.
     */
    static void Terminate(SharedSkiaDiskCache& cache, bool saveContent) {
        std::lock_guard lock(cache.mMutex);
        if (saveContent) {
            cache.saveToDiskLocked();
        }
        cache.mBlobCache = nullptr;
    }

    static uint32_t IdentityKey() { return SharedSkiaDiskCache::sIDKey; }
};

} /* namespace skiapipeline */
} /* namespace uirenderer */
} /* namespace android */

namespace {

std::string getExternalStorageFolder() {
    return getenv("EXTERNAL_STORAGE");
}

bool folderExist(const std::string& folderName) {
    DIR* dir = opendir(folderName.c_str());
    if (dir) {
        closedir(dir);
        return true;
    }
    return false;
}

/**
 * Attempts to delete the given file, return true if either the deletion was successful or
 * the file did not exist.
 */
bool deleteFile(const std::string& filePath) {
    int deleteResult = remove(filePath.c_str());
    return 0 == deleteResult || ENOENT == errno;
}

bool clean_up_test_dir() {
    if (!folderExist(getExternalStorageFolder())) {
        // don't run the test if external storage folder is not available
        return false;
    }
    std::string cacheFile = getExternalStorageFolder() + "/sharedSkiaDiskCacheTest";

    // remove any test files from previous test run
    if (!deleteFile(cacheFile)) {
        return false;
    }
    return true;
}

void write_to_cache(SharedSkiaDiskCache* cache, uint32_t key, uint32_t value, bool expectedResult) {
    sk_sp<SkData> keyData = SkData::MakeWithoutCopy(&key, sizeof(uint32_t));

    sk_sp<SkData> valueData = SkData::MakeWithoutCopy(&value, sizeof(uint32_t));

    bool result = cache->store(*keyData, *valueData);
    ASSERT_EQ(result, expectedResult);
}

void read_from_cache(SharedSkiaDiskCache* cache, uint32_t key, uint32_t expectedValue,
                     bool expectedResult) {
    sk_sp<SkData> keyData = SkData::MakeWithoutCopy(&key, sizeof(uint32_t));

    sk_sp<SkData> readback = cache->load(*keyData);

    if (expectedResult) {
        ASSERT_NE(readback, nullptr);
        if (readback) {
            sk_sp<SkData> valueData = SkData::MakeWithoutCopy(&expectedValue, sizeof(uint32_t));
            ASSERT_TRUE(valueData->equals(readback.get()));
        }
    } else {
        ASSERT_EQ(readback, nullptr);
    }
}

void check_identity_exists(SharedSkiaDiskCache* cache, bool expectedResult) {
    uint32_t keyValue = SharedSkiaDiskCacheUtils::IdentityKey();
    sk_sp<SkData> keyData = SkData::MakeWithoutCopy(&keyValue, sizeof(uint32_t));

    sk_sp<SkData> readback = cache->load(*keyData);

    if (expectedResult) {
        ASSERT_NE(readback, nullptr);
        if (readback) {
            ASSERT_TRUE(readback->size() == SHA256_DIGEST_LENGTH);
        }
    } else {
        ASSERT_EQ(readback, nullptr);
    }
}

} // anonymous namespace

constexpr int kTestKeySize = sizeof(uint32_t);
constexpr int kTestDataSize = sizeof(uint32_t);
// The identity key + the identity hash + one testing key/value pair
constexpr int kOneBlobCacheSize =
        kTestKeySize + SHA256_DIGEST_LENGTH + kTestKeySize + kTestDataSize;

// Try using store and load without ever setting the filename - both calls should fail
TEST(SharedSkiaDiskCacheTest, testNoFilename) {
    if (!clean_up_test_dir()) {
        return;
    }

    SharedSkiaDiskCache cache =
            SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                           /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                           /* mTotalSize= */ kOneBlobCacheSize);
    SharedSkiaDiskCacheUtils::DisableDiskWrite(cache);

    check_identity_exists(&cache, /* expectedResult= */ false);

    constexpr uint32_t kKey = 77;
    constexpr uint32_t kValue = 777;
    write_to_cache(&cache, kKey, kValue, /* expectedResult= */ false);
    read_from_cache(&cache, kKey, kValue, /* expectedResult= */ false);

    clean_up_test_dir();
}

// Try using store and load after setting the filename but not calling init - both calls should fail
TEST(SharedSkiaDiskCacheTest, testNoInit) {
    if (!clean_up_test_dir()) {
        return;
    }

    SharedSkiaDiskCache cache =
            SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                           /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                           /* mTotalSize= */ kOneBlobCacheSize);
    SharedSkiaDiskCacheUtils::DisableDiskWrite(cache);

    std::string cacheFileName = getExternalStorageFolder() + "/sharedSkiaDiskCacheTest";
    cache.setFilename(cacheFileName.c_str());

    check_identity_exists(&cache, /* expectedResult= */ false);

    constexpr uint32_t kKey = 77;
    constexpr uint32_t kValue = 777;
    write_to_cache(&cache, kKey, kValue, /* expectedResult= */ false);
    read_from_cache(&cache, kKey, kValue, /* expectedResult= */ false);

    clean_up_test_dir();
}

// Test reading from a nonexistent file
TEST(SharedSkiaDiskCacheTest, testNonexistentFile) {
    if (!clean_up_test_dir()) {
        return;
    }

    SharedSkiaDiskCache cache =
            SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                           /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                           /* mTotalSize= */ kOneBlobCacheSize);
    SharedSkiaDiskCacheUtils::DisableDiskWrite(cache);

    std::string cacheFileName = getExternalStorageFolder() + "/sharedSkiaDiskCacheTest";
    cache.setFilename(cacheFileName.c_str());

    uint32_t identity = 1024;
    bool validationResult = cache.init(&identity, sizeof(identity));
    ASSERT_FALSE(validationResult); // No prior identity

    check_identity_exists(&cache, /* expectedResult= */ true);

    constexpr uint32_t kKey = 77;
    constexpr uint32_t kValue = 777;
    read_from_cache(&cache, kKey, kValue, /* expectedResult= */ false); // cache should be empty

    clean_up_test_dir();
}

// Check that store/load work as expected w/ memory-only cache
TEST(SharedSkiaDiskCacheTest, testStoreLoad) {
    if (!clean_up_test_dir()) {
        return;
    }

    SharedSkiaDiskCache cache =
            SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                           /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                           /* mTotalSize= */ kOneBlobCacheSize);
    SharedSkiaDiskCacheUtils::DisableDiskWrite(cache);

    std::string cacheFileName = getExternalStorageFolder() + "/sharedSkiaDiskCacheTest";
    cache.setFilename(cacheFileName.c_str());

    uint32_t identity = 1024;
    bool validationResult = cache.init(&identity, sizeof(identity));
    ASSERT_FALSE(validationResult); // No prior identity

    check_identity_exists(&cache, /* expectedResult= */ true);

    constexpr uint32_t kKey = 77;
    constexpr uint32_t kValue = 777;
    write_to_cache(&cache, kKey, kValue, /* expectedResult= */ true);
    read_from_cache(&cache, kKey, kValue, /* expectedResult= */ true);

    clean_up_test_dir();
}

// Test out saving to a file and reading back. This additionally tests out the
// successful validation path.
TEST(SharedSkiaDiskCacheTest, testSaveReadBack) {
    if (!clean_up_test_dir()) {
        return;
    }

    std::string cacheFileName = getExternalStorageFolder() + "/sharedSkiaDiskCacheTest";
    uint32_t identity = 1024;

    constexpr uint32_t kKey = 77;
    constexpr uint32_t kValue1 = 777;
    constexpr uint32_t kValue2 = 111;

    size_t flattenedSize;

    // Save in the first cache instance
    {
        SharedSkiaDiskCache cache1 =
                SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                               /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                               /* mTotalSize= */ kOneBlobCacheSize);
        cache1.setFilename(cacheFileName.c_str());

        bool validationResult = cache1.init(&identity, sizeof(identity));
        ASSERT_FALSE(validationResult); // No prior identity

        check_identity_exists(&cache1, /* expectedResult= */ true);

        write_to_cache(&cache1, kKey, kValue1, /* expectedResult= */ true);

        flattenedSize = SharedSkiaDiskCacheUtils::GetFlattenedSize(cache1);

        SharedSkiaDiskCacheUtils::Terminate(cache1, /* saveContent= */ true);
    }
    // Load in the cache, verify the key/value pair and then modify the value
    {
        SharedSkiaDiskCache cache2 =
                SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                               /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                               /* mTotalSize= */ flattenedSize);
        cache2.setFilename(cacheFileName.c_str());

        bool validationResult = cache2.init(&identity, sizeof(identity));
        ASSERT_TRUE(validationResult);

        check_identity_exists(&cache2, /* expectedResult= */ true);

        read_from_cache(&cache2, kKey, kValue1, /* expectedResult= */ true);
        write_to_cache(&cache2, kKey, kValue2, /* expectedResult= */ true);
        SharedSkiaDiskCacheUtils::Terminate(cache2, /* saveContent= */ true);
    }
    // Reload the cache and verify the modified value
    {
        SharedSkiaDiskCache cache3 =
                SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                               /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                               /* mTotalSize= */ flattenedSize);
        cache3.setFilename(cacheFileName.c_str());

        bool validationResult = cache3.init(&identity, sizeof(identity));
        ASSERT_TRUE(validationResult);

        check_identity_exists(&cache3, /* expectedResult= */ true);

        read_from_cache(&cache3, kKey, kValue2, /* expectedResult= */ true);
    }

    clean_up_test_dir();
}

// Test out cache limits
TEST(SharedSkiaDiskCacheTest, testCacheLimits) {
    if (!clean_up_test_dir()) {
        return;
    }

    SharedSkiaDiskCache cache =
            SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                           /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                           /* mTotalSize= */ kOneBlobCacheSize);
    SharedSkiaDiskCacheUtils::DisableDiskWrite(cache);

    std::string cacheFileName = getExternalStorageFolder() + "/sharedSkiaDiskCacheTest";
    cache.setFilename(cacheFileName.c_str());

    uint32_t identity = 1024;
    bool validationResult = cache.init(&identity, sizeof(identity));
    ASSERT_FALSE(validationResult); // No prior identity

    check_identity_exists(&cache, /* expectedResult= */ true);

    uint32_t goodKey = 77;
    sk_sp<SkData> goodKeyData = SkData::MakeWithoutCopy(&goodKey, sizeof(goodKey));
    uint64_t tooLargeKey = 77;
    sk_sp<SkData> tooLargeKeyData = SkData::MakeWithoutCopy(&tooLargeKey, sizeof(tooLargeKey));

    sk_sp<SkData> goodValue = SkData::MakeZeroInitialized(SHA256_DIGEST_LENGTH);
    sk_sp<SkData> tooLargeValue = SkData::MakeZeroInitialized(SHA256_DIGEST_LENGTH + 1);

    bool result = cache.store(*tooLargeKeyData, *goodValue);
    ASSERT_EQ(result, false); // key too large

    result = cache.store(*goodKeyData, *tooLargeValue);
    ASSERT_EQ(result, false); // value too large

    result = cache.store(*tooLargeKeyData, *tooLargeValue);
    ASSERT_EQ(result, false); // both key and value are too large

    result = cache.store(*goodKeyData, *goodValue);
    ASSERT_EQ(result, true);

    clean_up_test_dir();
}

// Verify that the validation correctly handles changing identities
TEST(SharedSkiaDiskCacheTest, testValidation) {
    if (!clean_up_test_dir()) {
        return;
    }

    constexpr uint32_t kKey = 77;
    constexpr uint32_t kValue = 777;

    const uint32_t kCorrectIdentity = 1024;
    const uint32_t kWrongIdentity = 2048;

    std::string cacheFileName = getExternalStorageFolder() + "/sharedSkiaDiskCacheTest";

    size_t flattenedSize;

    // Create the cache with the correct identity and save it to disk
    {
        SharedSkiaDiskCache cache1 =
                SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                               /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                               /* mTotalSize= */ kOneBlobCacheSize);

        cache1.setFilename(cacheFileName.c_str());

        bool validationResult = cache1.init(&kCorrectIdentity, sizeof(kCorrectIdentity));
        ASSERT_FALSE(validationResult); // No prior identity

        check_identity_exists(&cache1, /* expectedResult= */ true);

        write_to_cache(&cache1, kKey, kValue, /* expectedResult= */ true);

        flattenedSize = SharedSkiaDiskCacheUtils::GetFlattenedSize(cache1);

        SharedSkiaDiskCacheUtils::Terminate(cache1, /* saveContent= */ true);
    }
    // Try loading with the incorrect identity - it should fail and empty the cache
    {
        SharedSkiaDiskCache cache2 =
                SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                               /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                               /* mTotalSize= */ flattenedSize);

        cache2.setFilename(cacheFileName.c_str());

        bool validationResult = cache2.init(&kWrongIdentity, sizeof(kWrongIdentity));
        ASSERT_FALSE(validationResult); // Identity mismatch

        check_identity_exists(&cache2, /* expectedResult= */ true);

        read_from_cache(&cache2, kKey, kValue, /* expectedResult= */ false);
        SharedSkiaDiskCacheUtils::Terminate(cache2, /* saveContent= */ false); // Note: not saving
    }
    // Reload w/ correct identity and verify contents
    {
        SharedSkiaDiskCache cache3 =
                SharedSkiaDiskCacheUtils::Make(/* maxKeySize= */ kTestKeySize,
                                               /* maxValueSize= */ SHA256_DIGEST_LENGTH,
                                               /* mTotalSize= */ flattenedSize);

        cache3.setFilename(cacheFileName.c_str());

        bool validationResult = cache3.init(&kCorrectIdentity, sizeof(kCorrectIdentity));
        ASSERT_TRUE(validationResult); // Identity match

        check_identity_exists(&cache3, /* expectedResult= */ true);

        read_from_cache(&cache3, kKey, kValue, /* expectedResult= */ true);
    }

    clean_up_test_dir();
}
