#pragma once

#include "Log.h"

#include <atomic>
#include <cstdint>

struct DebugStats {
    std::atomic<uint64_t> req031 {0};
    std::atomic<uint64_t> unlinkReq {0};
    std::atomic<uint64_t> retError {0};
    std::atomic<uint64_t> botRecovery {0};
    std::atomic<uint64_t> bulkError {0};
    std::atomic<uint64_t> controlError {0};

    std::atomic<uint64_t> retEpipe {0};
    std::atomic<uint64_t> retEtimedout {0};
    std::atomic<uint64_t> retEnodev {0};
    std::atomic<uint64_t> retOtherErr {0};
};

inline DebugStats g_stats;

inline void dump_stats(const char* reason) {
    LOGI("stats[" << reason << "]:"
                    << " 0x31=" << g_stats.req031.load()
                    << " unlink=" << g_stats.unlinkReq.load()
                    << " ret_error=" << g_stats.retError.load()
                    << " bulk_error=" << g_stats.bulkError.load()
                    << " control_error=" << g_stats.controlError.load()
                    << " bot_recovery=" << g_stats.botRecovery.load()
                    << " epipe=" << g_stats.retEpipe.load()
                    << " etimedout=" << g_stats.retEtimedout.load()
                    << " enodev=" << g_stats.retEnodev.load()
                    << " other=" << g_stats.retOtherErr.load());
}

inline void record_ret_error(int32_t status) {
    ++g_stats.retError;

    if (status == -32) {
        ++g_stats.retEpipe;
    } else if (status == -110) {
        ++g_stats.retEtimedout;
    } else if (status == -19) {
        ++g_stats.retEnodev;
    } else {
        ++g_stats.retOtherErr;
    }
}
