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

#include <com_android_graphics_libgui_flags.h>
#include <gui/SurfaceComposerClient.h>
#include <ui/PixelFormat.h>

#include <flag_macros.h>
#include <gtest/gtest.h>

namespace android {

namespace libgui_flags = com::android::graphics::libgui::flags;

using Transaction = SurfaceComposerClient::Transaction;

class TestableTransactionCompletedListener : public TransactionCompletedListener {
public:
    size_t getCallbackCount() {
        std::lock_guard lock{mMutex};
        return mCallbacks.size();
    }

    size_t getReleaseCallbackCount() {
        std::lock_guard lock{mMutex};
        return mReleaseBufferCallbacks.size();
    }
};

class TransactionCallbackTest : public ::testing::Test {
protected:
    sp<SurfaceComposerClient> mClient;
    sp<TestableTransactionCompletedListener> mListener;

    void SetUp() override {
        mClient = sp<SurfaceComposerClient>::make();
        mListener = sp<TestableTransactionCompletedListener>::make();
        TransactionCompletedListener::setInstance(mListener);
    }

    Transaction makeTransactionWithReleaseCallback() {
        sp<SurfaceControl> sc =
                mClient->createSurface(String8{"testSurface"}, 1, 1, PIXEL_FORMAT_RGBA_8888);
        sp<GraphicBuffer> buffer =
                sp<GraphicBuffer>::make(1, 1, PIXEL_FORMAT_RGBA_8888, GRALLOC_USAGE_SW_READ_RARELY);

        Transaction transaction;
        transaction.setBuffer(sc, buffer, /*fence=*/std::nullopt, /*frameNumber=*/std::nullopt,
                              /*producerId=*/0, [](auto&&...) {});
        return transaction;
    }
};

TEST_F(TransactionCallbackTest, completedCallback_transactionApplied) {
    std::mutex mutex;
    std::condition_variable cv;
    bool transactionComplete = false;

    Transaction transaction;
    transaction.addTransactionCompletedCallback(
            [&](auto&&...) {
                std::lock_guard lock{mutex};
                transactionComplete = true;
                cv.notify_one();
            },
            nullptr);
    transaction.apply();

    std::unique_lock lock{mutex};
    cv.wait(lock, [&]() { return transactionComplete; });

    ASSERT_EQ(mListener->getCallbackCount(), size_t{0});
}

TEST_F_WITH_FLAGS(TransactionCallbackTest, completedCallback_transactionDeleted,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(libgui_flags,
                                                      unapplied_transaction_cleanup))) {
    {
        Transaction transaction;
        transaction.addTransactionCompletedCallback([](auto&&...) {}, nullptr);
    }

    ASSERT_EQ(mListener->getCallbackCount(), size_t{0});
}

TEST_F_WITH_FLAGS(TransactionCallbackTest, completedCallback_transactionCleared,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(libgui_flags,
                                                      unapplied_transaction_cleanup))) {
    Transaction transaction;
    transaction.addTransactionCompletedCallback([](auto&&...) {}, nullptr);
    transaction.clear();

    ASSERT_EQ(mListener->getCallbackCount(), size_t{0});
}

TEST_F_WITH_FLAGS(TransactionCallbackTest, completedCallback_transactionParceled,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(libgui_flags,
                                                      unapplied_transaction_cleanup))) {
    Transaction transaction;
    transaction.addTransactionCompletedCallback([](auto&&...) {}, nullptr);

    Parcel parcel;
    ASSERT_EQ(transaction.writeToParcel(&parcel), OK);
    transaction.clear();

    // The callback should still be alive because the parcel references it.
    ASSERT_EQ(mListener->getCallbackCount(), size_t{1});

    parcel.freeData();
    ASSERT_EQ(mListener->getCallbackCount(), size_t{0});
}

TEST_F_WITH_FLAGS(TransactionCallbackTest, completedCallback_transactionMerged,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(libgui_flags,
                                                      unapplied_transaction_cleanup))) {
    Transaction t1;
    Transaction t2;
    t2.addTransactionCompletedCallback([](auto&&...) {}, nullptr);
    t1.merge(std::move(t2));

    // The callback should still be alive after merging the transaction that created it
    // into another transaction.
    ASSERT_EQ(mListener->getCallbackCount(), size_t{1});

    t1.clear();
    ASSERT_EQ(mListener->getCallbackCount(), size_t{0});
}

TEST_F(TransactionCallbackTest, releaseCallback_transactionApplied) {
    std::mutex mutex;
    std::condition_variable cv;
    bool transactionComplete = false;

    sp<SurfaceControl> sc =
            mClient->createSurface(String8{"testSurface"}, 1, 1, PIXEL_FORMAT_RGBA_8888);
    sp<GraphicBuffer> buffer1 =
            sp<GraphicBuffer>::make(1, 1, PIXEL_FORMAT_RGBA_8888, GRALLOC_USAGE_SW_READ_RARELY);
    sp<GraphicBuffer> buffer2 =
            sp<GraphicBuffer>::make(1, 1, PIXEL_FORMAT_RGBA_8888, GRALLOC_USAGE_SW_READ_RARELY);

    ReleaseBufferCallback releaseBufferCallback = [&](auto&&...) {
        std::lock_guard lock{mutex};
        transactionComplete = true;
        cv.notify_one();
    };

    Transaction transaction;
    transaction.setBuffer(sc, buffer1, /*fence=*/std::nullopt, /*frameNumber=*/std::nullopt,
                          /*producerId=*/0, releaseBufferCallback);
    ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{1});
    transaction.apply();

    // Apply a second transaction so that buffer1 is released.
    transaction.setBuffer(sc, buffer2);
    transaction.apply();

    std::unique_lock lock{mutex};
    cv.wait(lock, [&]() { return transactionComplete; });

    ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{0});
}

TEST_F_WITH_FLAGS(TransactionCallbackTest, releaseCallback_transactionDeleted,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(libgui_flags,
                                                      unapplied_transaction_cleanup))) {
    {
        Transaction transaction = makeTransactionWithReleaseCallback();
        ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{1});
    }

    ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{0});
}

TEST_F_WITH_FLAGS(TransactionCallbackTest, releaseCallback_transactionCleared,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(libgui_flags,
                                                      unapplied_transaction_cleanup))) {
    Transaction transaction = makeTransactionWithReleaseCallback();
    transaction.clear();

    ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{0});
}

TEST_F_WITH_FLAGS(TransactionCallbackTest, releaseCallback_transactionParceled,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(libgui_flags,
                                                      unapplied_transaction_cleanup))) {
    Transaction transaction = makeTransactionWithReleaseCallback();

    Parcel parcel;
    ASSERT_EQ(transaction.writeToParcel(&parcel), OK);
    transaction.clear();

    // The callback should still be alive because the parcel references it.
    ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{1});

    parcel.freeData();
    ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{0});
}

TEST_F_WITH_FLAGS(TransactionCallbackTest, releaseCallback_transactionMerged,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(libgui_flags,
                                                      unapplied_transaction_cleanup))) {
    Transaction t1;
    Transaction t2 = makeTransactionWithReleaseCallback();
    t1.merge(std::move(t2));

    // The callback should still be alive after merging the transaction that created it
    // into another transaction.
    ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{1});

    t1.clear();
    ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{0});
}

TEST_F_WITH_FLAGS(TransactionCallbackTest, releaseCallback_mergedTransactionParceledThenCleared,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(libgui_flags,
                                                      unapplied_transaction_cleanup))) {
    Transaction t1;
    Transaction t2 = makeTransactionWithReleaseCallback();
    t1.merge(std::move(t2));

    Parcel parcel;
    ASSERT_EQ(t1.writeToParcel(&parcel), OK);
    t1.clear();

    // The callback should still be alive because the parcel references it.
    ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{1});

    parcel.freeData();
    ASSERT_EQ(mListener->getReleaseCallbackCount(), size_t{0});
}

} // namespace android
