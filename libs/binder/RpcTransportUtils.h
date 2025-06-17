/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <binder/unique_fd.h>
#include <poll.h>

#include "FdTrigger.h"
#include "RpcState.h"

namespace android {

template <typename SendOrReceive>
status_t interruptableReadOrWrite(
        const android::RpcTransportFd& socket, FdTrigger* fdTrigger, iovec* iovs, int niovs,
        SendOrReceive sendOrReceiveFun, const char* funName, int16_t event,
        const std::optional<binder::impl::SmallFunction<status_t()>>& altPoll) {
    MAYBE_WAIT_IN_FLAKE_MODE;

    if (niovs < 0) {
        return BAD_VALUE;
    }

    // Since we didn't poll, we need to manually check to see if it was triggered. Otherwise, we
    // may never know we should be shutting down.
    if (fdTrigger->isTriggered()) {
        return DEAD_OBJECT;
    }

    // EMPTY IOVEC ISSUE
    // If iovs has one or more empty vectors at the end and
    // we somehow advance past all the preceding vectors and
    // pass some or all of the empty ones to sendmsg/recvmsg,
    // the call will return processSize == 0. In that case
    // we should be returning OK but instead return DEAD_OBJECT.
    // To avoid this problem, we make sure here that the last
    // vector at iovs[niovs - 1] has a non-zero length.
    while (niovs > 0 && iovs[niovs - 1].iov_len == 0) {
        niovs--;
    }
    if (niovs == 0) {
        // The vectors are all empty, so we have nothing to send.
        return OK;
    }

    bool havePolled = false;
    while (true) {
        ssize_t processSize = -1;
        bool canSendFullTransaction = false;

        // This block dynamically adjusts packet sizes down to work around a
        // limitation in the vsock driver where large packets are sometimes
        // dropped (b/419364025 b/399469406 b/421244320).
        // TODO: only apply this workaround on vsock ???
        // TODO: fix vsock
        {
            size_t limit = 65536;
            int i = 0;
            for (i = 0; i < niovs; i++) {
                if (iovs[i].iov_len >= limit) {
                    break;
                }
                limit -= iovs[i].iov_len;
            }
            canSendFullTransaction = i == niovs;

            int old_niovs = niovs;
            size_t old_len = 0xDEADBEEF;

            if (!canSendFullTransaction) {
                // pretend like we have fewer iovecs
                niovs = i + 1; // to restore (A)
                old_len = iovs[i].iov_len;
                // only send up to remaining limit from this iovec
                iovs[i].iov_len = limit; // to restore (B)
                LOG_ALWAYS_FATAL_IF(limit == 0,
                                    "limit should not be zero - see EMPTY IOVEC ISSUE above");
            }

            // MAIN ACTION
            processSize = sendOrReceiveFun(iovs, niovs);
            // MAIN ACTION

            if (!canSendFullTransaction) {
                // Now put the iovecs back. As far as the following logic
                // is concerned, this just looks like a partial read, up
                // to limit.
                niovs = old_niovs;         // (A) - restored
                iovs[i].iov_len = old_len; // (B) - restored
            }
        }

        if (processSize < 0) {
            int savedErrno = errno;

            // Still return the error on later passes, since it would expose
            // a problem with polling
            if (havePolled || (savedErrno != EAGAIN && savedErrno != EWOULDBLOCK)) {
                LOG_RPC_DETAIL("RpcTransport %s(): %s", funName, strerror(savedErrno));
                return -savedErrno;
            }
        } else if (processSize == 0) {
            return DEAD_OBJECT;
        } else {
            while (processSize > 0 && niovs > 0) {
                auto& iov = iovs[0];
                if (static_cast<size_t>(processSize) < iov.iov_len) {
                    // Advance the base of the current iovec
                    iov.iov_base = reinterpret_cast<char*>(iov.iov_base) + processSize;
                    iov.iov_len -= processSize;
                    break;
                }

                // The current iovec was fully written
                processSize -= iov.iov_len;
                iovs++;
                niovs--;
            }
            if (niovs == 0) {
                LOG_ALWAYS_FATAL_IF(processSize > 0,
                                    "Reached the end of iovecs "
                                    "with %zd bytes remaining",
                                    processSize);
                return OK;
            }
        }

        // altPoll may introduce long waiting since it assumes if it cannot write
        // data, that it needs to wait to send more to give time for the producer
        // consumer problem to be solved - otherwise it will busy loop. However,
        // for this worakround, we are breaking up the transaction intentionally,
        // not because the transaction won't fit, but to avoid a bug in the kernel
        // for how it combines messages. So, when we artificially simulate a
        // limited send, don't poll and just keep on sending data.
        if (!canSendFullTransaction) continue;

        if (altPoll) {
            if (status_t status = (*altPoll)(); status != OK) return status;
            if (fdTrigger->isTriggered()) {
                return DEAD_OBJECT;
            }
        } else {
            if (status_t status = fdTrigger->triggerablePoll(socket, event); status != OK)
                return status;
            if (!havePolled) havePolled = true;
        }
    }
}

} // namespace android
