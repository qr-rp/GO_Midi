#pragma once
#include <string>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace Util {

    class NtpClient {
    public:
        // Returns true if sync successful
        static bool Sync(long& offset_sec);

        static void StartAutoSync();
        static void StopAutoSync();
        static void ForceShutdown();  // 强制关闭NTP客户端
        
        // Get current NTP time (approximate based on steady clock and last sync)
        static std::chrono::system_clock::time_point GetNow();
        
        // Check if synced
        static bool IsSynced();

        static long long GetLastDelayMs();
        static long long GetLastOffsetMs();

    private:
        static void AutoSyncThread();
        static bool SyncOnceMs(const char* server_name, int timeout_ms, double& offset_ms, double& delay_ms);

        static std::atomic<bool> s_synced;
        static std::chrono::system_clock::time_point s_base_ntp;
        static std::chrono::steady_clock::time_point s_base_steady;
        static double s_skew; // Clock skew (drift) factor
        
        // Anchor points for long-term skew calculation
        static std::chrono::system_clock::time_point s_anchor_ntp;
        static std::chrono::steady_clock::time_point s_anchor_steady;

        static std::mutex s_mutex;
        static std::condition_variable s_cv;
        static std::atomic<bool> s_auto_sync_running;
        static std::atomic<bool> s_auto_sync_stop;
        static std::thread s_auto_thread;

        static std::atomic<long long> s_last_delay_ms;
        static std::atomic<long long> s_last_offset_ms;
        static std::atomic<int> s_sync_count;
    };

}
