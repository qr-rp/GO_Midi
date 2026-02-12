#include "NtpClient.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <algorithm>
#include <cmath>

// Link with ws2_32.lib
// In CMake we added it, but if using MSVC direct, we might need pragma.
// #pragma comment(lib, "ws2_32.lib")

namespace Util {

    std::atomic<bool> NtpClient::s_synced{false};
    std::chrono::system_clock::time_point NtpClient::s_base_ntp;
    std::chrono::steady_clock::time_point NtpClient::s_base_steady;
    double NtpClient::s_skew{1.0};
    
    // Anchor definitions
    std::chrono::system_clock::time_point NtpClient::s_anchor_ntp;
    std::chrono::steady_clock::time_point NtpClient::s_anchor_steady;

    std::mutex NtpClient::s_mutex;
    std::condition_variable NtpClient::s_cv;
    std::atomic<bool> NtpClient::s_auto_sync_running{false};
    std::atomic<bool> NtpClient::s_auto_sync_stop{false};
    std::thread NtpClient::s_auto_thread;
    std::atomic<long long> NtpClient::s_last_delay_ms{0};
    std::atomic<long long> NtpClient::s_last_offset_ms{0};
    std::atomic<int> NtpClient::s_sync_count{0};

    static long long DurationToMs(const std::chrono::system_clock::duration& d) {
        return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    }

    static std::chrono::system_clock::time_point NtpTimestampToTimePoint(uint32_t seconds_be, uint32_t fraction_be) {
        const unsigned long long NTP_TIMESTAMP_DELTA = 2208988800ull;

        const uint32_t seconds = ntohl(seconds_be);
        const uint32_t fraction = ntohl(fraction_be);

        const long long unix_seconds = (long long)seconds - (long long)NTP_TIMESTAMP_DELTA;
        const double frac_seconds = (double)fraction / 4294967296.0;

        const auto base = std::chrono::system_clock::from_time_t((time_t)unix_seconds);
        const auto extra = std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::duration<double>(frac_seconds));
        return base + extra;
    }

    bool NtpClient::SyncOnceMs(const char* server_name, int timeout_ms, double& offset_ms, double& delay_ms) {
        SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockfd == INVALID_SOCKET) {
            return false;
        }

        DWORD timeout = (DWORD)timeout_ms;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        
        struct addrinfo* result = nullptr;
        if (getaddrinfo(server_name, "123", &hints, &result) != 0) {
            closesocket(sockfd);
            return false;
        }

        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        // Use the first result
        if (result) {
            memcpy(&serv_addr, result->ai_addr, result->ai_addrlen);
        }
        freeaddrinfo(result);

        unsigned char packet[48] = {0};
        packet[0] = 0x1B;

        const auto t0 = std::chrono::system_clock::now();
        if (sendto(sockfd, (char*)packet, 48, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            closesocket(sockfd);
            return false;
        }

        struct sockaddr_in from;
        int fromlen = sizeof(from);
        if (recvfrom(sockfd, (char*)packet, 48, 0, (struct sockaddr*)&from, &fromlen) < 0) {
            closesocket(sockfd);
            return false;
        }
        const auto t3 = std::chrono::system_clock::now();

        uint32_t recv_sec_be = 0;
        uint32_t recv_frac_be = 0;
        uint32_t tx_sec_be = 0;
        uint32_t tx_frac_be = 0;

        memcpy(&recv_sec_be, packet + 32, 4);
        memcpy(&recv_frac_be, packet + 36, 4);
        memcpy(&tx_sec_be, packet + 40, 4);
        memcpy(&tx_frac_be, packet + 44, 4);

        const auto t1 = NtpTimestampToTimePoint(recv_sec_be, recv_frac_be);
        const auto t2 = NtpTimestampToTimePoint(tx_sec_be, tx_frac_be);

        const auto offset = ((t1 - t0) + (t2 - t3)) / 2;
        const auto delay = (t3 - t0) - (t2 - t1);

        offset_ms = (double)std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(offset).count();
        delay_ms = (double)std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(delay).count();

        closesocket(sockfd);
        return std::isfinite(offset_ms) && std::isfinite(delay_ms) && delay_ms >= 0.0;
    }

    bool NtpClient::Sync(long& offset_sec) {
        offset_sec = 0;

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return false;
        }

        const char* ntp_servers[] = {
            "ntp.aliyun.com",
            "ntp.tencent.com",
            "cn.pool.ntp.org",
            "pool.ntp.org"
        };

        struct Sample {
            double offset_ms;
            double delay_ms;
        };

        // 1. 收集所有服务器的样本
        std::vector<Sample> all_samples;
        double min_delay = 1e9;

        // 快速启动策略：如果未同步，使用快速模式
        bool fast_mode = !s_synced.load();
        // 极速模式：每个服务器最多2个样本，总共只要3个样本（强制跨服务器校验），超时仅200ms
        int max_samples_per_server = fast_mode ? 2 : 8;
        int target_total_samples = fast_mode ? 3 : 1000;

        for (const char* server_name : ntp_servers) {
            if (s_auto_sync_stop.load()) {
                WSACleanup();
                return false;
            }

            // 每个服务器多采几次，增加样本量
            for (int i = 0; i < max_samples_per_server; ++i) {
                if (s_auto_sync_stop.load()) {
                    WSACleanup();
                    return false;
                }
                double off = 0.0;
                double del = 0.0;
                
                // 快速模式下超时时间极短，优先保证响应速度
                int timeout = fast_mode ? 200 : 1000;
                
                if (SyncOnceMs(server_name, timeout, off, del)) {
                    if (del > 0.0) {
                        all_samples.push_back({off, del});
                        if (del < min_delay) {
                            min_delay = del;
                        }
                    }
                }
            }

            // 如果已经收集到足够样本，提前结束（仅限快速模式）
            if (fast_mode && all_samples.size() >= target_total_samples) {
                break;
            }
        }

        if (all_samples.empty()) {
            WSACleanup();
            return false;
        }

        // 2. 过滤：只保留 delay 较小的样本 (比如 min_delay * 1.5 或 min_delay + 30ms)
        // 这里的策略是：保留 delay 最小的前 50% 样本，或者绝对 delay 小于一定阈值的样本
        // 考虑到网络抖动，我们使用 min_delay 作为基准
        double delay_threshold = min_delay * 1.5; 
        if (delay_threshold < min_delay + 10.0) delay_threshold = min_delay + 10.0; // 至少允许 10ms 浮动

        std::vector<Sample> good_samples;
        for (const auto& s : all_samples) {
            if (s.delay_ms <= delay_threshold) {
                good_samples.push_back(s);
            }
        }

        if (good_samples.empty()) {
            // 如果没有样本符合要求（理论上 min_delay 那个肯定符合），回退到使用所有样本
            good_samples = all_samples;
        }

        // 3. 加权平均：Weight = 1 / (Delay^2)
        // Delay 越小，权重越大
        double total_weight = 0.0;
        double weighted_offset_sum = 0.0;
        double weighted_delay_sum = 0.0;

        for (const auto& s : good_samples) {
            double weight = 1.0 / (s.delay_ms * s.delay_ms);
            total_weight += weight;
            weighted_offset_sum += s.offset_ms * weight;
            weighted_delay_sum += s.delay_ms * weight;
        }

        double final_offset_ms = weighted_offset_sum / total_weight;
        double final_delay_ms = weighted_delay_sum / total_weight;

        // 4. 更新逻辑
        const auto steady_now = std::chrono::steady_clock::now();
        const auto local_now = std::chrono::system_clock::now();
        const auto now_est = local_now + std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double, std::milli>(final_offset_ms)
        );

        const auto current_now = GetNow();
        const auto error = now_est - current_now;
        const auto abs_err_ms = std::llabs(DurationToMs(error));

        int current_count = s_sync_count.load();

        // 计算并更新时钟漂移 (Skew) - 优化版：使用锚点 (Anchor Point) 机制
        // 必须在更新 s_base_ntp 之前进行
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            
            // 初始化锚点（如果是第一次同步或重置）
            // 策略：在初始化阶段（前5次同步）持续更新锚点，直到进入稳定阶段才固定下来
            static bool anchor_initialized = false;
            if (!s_synced.load() || !anchor_initialized || abs_err_ms > 5000 || current_count < 5) {
                s_anchor_ntp = now_est;
                s_anchor_steady = steady_now;
                anchor_initialized = true;
                s_skew = 1.0; // 重置 Skew
            } else {
                auto steady_delta = steady_now - s_anchor_steady;
                double steady_delta_sec = std::chrono::duration<double>(steady_delta).count();
                
                // 只有当间隔足够长 (>60s) 时才开始更新漂移
                // 间隔越长，网络抖动 (Jitter) 对斜率计算的影响越小
                if (steady_delta_sec > 60.0) {
                    auto real_delta = now_est - s_anchor_ntp;
                    double real_delta_sec = std::chrono::duration<double>(real_delta).count();
                    
                    double measured_skew = real_delta_sec / steady_delta_sec;
                    
                    // 简单的异常过滤：漂移不应超过 +/- 1000ppm (0.1%)
                    if (std::abs(measured_skew - 1.0) < 0.001) {
                        // 使用 EWMA 平滑更新 Skew
                        // 由于使用了长时段锚点，测量值本身已经很稳定，可以使用较大的 alpha 加快收敛
                        double skew_alpha = 0.3;
                        s_skew = s_skew * (1.0 - skew_alpha) + measured_skew * skew_alpha;
                    }
                }
            }
        }

        const auto count = s_sync_count.fetch_add(1) + 1;
        // abs_err_ms defined above

        std::chrono::system_clock::time_point new_base_ntp;
        std::chrono::steady_clock::time_point new_base_steady = steady_now;

        // 平滑更新策略：
        // 如果是初次同步或误差巨大 (>5s)，直接硬更新
        // 否则，应用 EWMA 平滑 (alpha = 0.2)，逐步逼近真实时间
        if (!s_synced.load() || abs_err_ms > 5000) {
            new_base_ntp = now_est;
        } else {
            // 限制最大单次调整幅度，防止时间跳变
            // 计算目标调整量
            double adjust_ms = (double)std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(error).count();
            
            // EWMA 因子：越小越平滑，但也越慢
            double alpha = 0.2; 
            double smooth_adjust_ms = adjust_ms * alpha;

            // 再次限制最大步长 (例如每次最多修 5ms)
            if (smooth_adjust_ms > 5.0) smooth_adjust_ms = 5.0;
            if (smooth_adjust_ms < -5.0) smooth_adjust_ms = -5.0;

            auto adjust = std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double, std::milli>(smooth_adjust_ms)
            );
            new_base_ntp = current_now + adjust;
        }

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_base_ntp = new_base_ntp;
            s_base_steady = new_base_steady;
        }
        s_synced.store(true);

        s_last_delay_ms.store((long long)std::llround(final_delay_ms));
        s_last_offset_ms.store((long long)std::llround(final_offset_ms));
        offset_sec = (long)std::llround(final_offset_ms / 1000.0);

        WSACleanup();
        return true;
    }
    
    std::chrono::system_clock::time_point NtpClient::GetNow() {
        if (!s_synced.load()) {
            return std::chrono::system_clock::now();
        }
        auto now_steady = std::chrono::steady_clock::now();
        std::chrono::system_clock::time_point base_ntp;
        std::chrono::steady_clock::time_point base_steady;
        double skew = 1.0;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            base_ntp = s_base_ntp;
            base_steady = s_base_steady;
            skew = s_skew;
        }
        const auto diff = now_steady - base_steady;
        // Apply skew correction
        // real_diff = steady_diff * skew
        const auto diff_us = std::chrono::duration_cast<std::chrono::microseconds>(diff).count();
        const auto real_diff_us = (long long)(diff_us * skew);
        
        return base_ntp + std::chrono::microseconds(real_diff_us);
    }
    
    bool NtpClient::IsSynced() {
        return s_synced;
    }

    long long NtpClient::GetLastDelayMs() {
        return s_last_delay_ms.load();
    }

    long long NtpClient::GetLastOffsetMs() {
        return s_last_offset_ms.load();
    }

    void NtpClient::StartAutoSync() {
        bool expected = false;
        if (!s_auto_sync_running.compare_exchange_strong(expected, true)) {
            return;
        }

        s_sync_count.store(0); // 重置同步计数，确保每次启动都经历快速初始化阶段
        s_auto_sync_stop.store(false);
        s_auto_thread = std::thread(&NtpClient::AutoSyncThread);
    }

    void NtpClient::StopAutoSync() {
        if (!s_auto_sync_running.load()) {
            return;
        }

        s_auto_sync_stop.store(true);
        s_cv.notify_all();
        
        // 设置超时时间以避免无限等待
        if (s_auto_thread.joinable()) {
            // 使用条件变量超时等待
            std::unique_lock<std::mutex> lock(s_mutex);
            if (s_cv.wait_for(lock, std::chrono::milliseconds(200), []() { 
                return !s_auto_sync_running.load(); 
            })) {
                // 正常退出
                s_auto_thread.join();
            } else {
                // 超时处理 - 在析构函数中线程会被自动清理
                // 这里我们接受可能的资源泄漏以换取快速关闭
            }
        }
        s_auto_sync_running.store(false);
    }
    
    void NtpClient::ForceShutdown() {
        // 立即设置停止标志
        s_auto_sync_stop.store(true);
        
        // 通知所有等待的线程
        s_cv.notify_all();
        
        // 如果线程仍在运行，立即分离它以避免阻塞
        if (s_auto_thread.joinable()) {
            try {
                // 给线程一小段时间来响应停止信号
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                
                // 检查线程是否已经结束
                if (s_auto_thread.joinable()) {
                    // 如果线程仍未结束，就分离它
                    s_auto_thread.detach();
                } else {
                    // 线程已结束，正常join
                    s_auto_thread.join();
                }
            } catch (...) {
                // 忽略任何异常，确保强制关闭
                if (s_auto_thread.joinable()) {
                    s_auto_thread.detach();
                }
            }
        }
        
        // 确保状态被重置
        s_auto_sync_running.store(false);
        s_synced.store(false);
    }

    void NtpClient::AutoSyncThread() {
        while (!s_auto_sync_stop.load()) {
            long offset_sec = 0;
            Sync(offset_sec);

            const int count = s_sync_count.load();
            const auto interval = (count <= 3 || !s_synced.load()) ? std::chrono::seconds(1) : std::chrono::seconds(10);

            std::unique_lock<std::mutex> lock(s_mutex);
            s_cv.wait_for(lock, interval, []() { return s_auto_sync_stop.load(); });
        }
    }

}
