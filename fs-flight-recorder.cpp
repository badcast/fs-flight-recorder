/**
 * fs-flight-recorder: In-memory filesystem black box with anomaly triggers.
 *
 * Developed & Designed by: ARGON 4 (GEMINI 4)
 * Architecture: Zero-disk RAM Ring Buffer + Sliding Window Mutation Detector
 */

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <csignal>
#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;

volatile std::sig_atomic_t keep_running = 1;
void sig_handler(int) { keep_running = 0; }

// Single event record inside the ring buffer
struct FsEvent {
    SystemClock::time_point timestamp;
    Clock::time_point steady_ts;
    uint32_t mask;
    std::string path;
};

// Fixed-capacity circular buffer stored entirely in RAM
class FlightRecorderBuffer {
private:
    std::vector<FsEvent> buffer_;
    size_t head_ = 0;
    size_t count_ = 0;
    const size_t capacity_;

public:
    explicit FlightRecorderBuffer(size_t capacity)
        : buffer_(capacity), capacity_(capacity) {}

    void push(FsEvent&& ev) {
        buffer_[head_] = std::move(ev);
        head_ = (head_ + 1) % capacity_;
        if (count_ < capacity_) count_++;
    }

    // Count mutations occurred within the sliding time window
    size_t count_recent_mutations(std::chrono::milliseconds window) const {
        auto now = Clock::now();
        size_t recent = 0;
        for (size_t i = 0; i < count_; ++i) {
            size_t idx = (head_ + capacity_ - 1 - i) % capacity_;
            if (now - buffer_[idx].steady_ts <= window) {
                recent++;
            } else {
                break; // Events are chronologically ordered
            }
        }
        return recent;
    }

    // Emergency dump of the ring buffer to disk upon trigger activation
    void dump_to_disk(const std::string& dump_path) const {
        std::ofstream out(dump_path);
        if (!out.is_open()) return;

        out << "=== FS FLIGHT RECORDER EMERGENCY DUMP ===\n";
        out << "Total recorded events in ring: " << count_ << "\n\n";

        size_t start = (count_ == capacity_) ? head_ : 0;
        for (size_t i = 0; i < count_; ++i) {
            size_t idx = (start + i) % capacity_;
            const auto& ev = buffer_[idx];

            auto time_t_ts = SystemClock::to_time_t(ev.timestamp);
            out << std::put_time(std::localtime(&time_t_ts), "%Y-%m-%d %H:%M:%S")
                << " | Mask: [ " << mask_to_string(ev.mask) << "] "
                << " | File: " << ev.path << "\n";
        }
        out.flush();
    }

    static std::string mask_to_string(uint32_t mask) {
        std::string res;
        if (mask & IN_CREATE) res += "CREATE ";
        if (mask & IN_DELETE) res += "DELETE ";
        if (mask & IN_MODIFY) res += "MODIFY ";
        if (mask & IN_MOVED_FROM) res += "MOVED_FROM ";
        if (mask & IN_MOVED_TO) res += "MOVED_TO ";
        if (mask & IN_ATTRIB) res += "CHMOD/CHOWN ";
        return res;
    }
};

// Core monitoring and reactive watchdog engine
class AnomalyWatchdog {
private:
    int inotify_fd_;
    FlightRecorderBuffer recorder_;
    std::string watch_dir_;
    size_t threshold_ops_;
    std::chrono::milliseconds window_ms_;
    std::string emergency_cmd_;
    std::chrono::steady_clock::time_point last_trigger_;

public:
    AnomalyWatchdog(std::string dir, size_t ring_size, size_t threshold,
                    std::chrono::milliseconds window, std::string cmd)
        : recorder_(ring_size), watch_dir_(std::move(dir)),
          threshold_ops_(threshold), window_ms_(window), emergency_cmd_(std::move(cmd)) {

        inotify_fd_ = inotify_init1(IN_NONBLOCK);
        if (inotify_fd_ < 0) {
            perror("inotify_init1");
            std::exit(EXIT_FAILURE);
        }

        uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB;
        int wd = inotify_add_watch(inotify_fd_, watch_dir_.c_str(), mask);
        if (wd < 0) {
            perror("inotify_add_watch");
            std::exit(EXIT_FAILURE);
        }
        last_trigger_ = Clock::now() - std::chrono::minutes(10);
    }

    ~AnomalyWatchdog() {
        if (inotify_fd_ >= 0) close(inotify_fd_);
    }

    void run() {
        std::cout << "[*] Flight recorder started. Monitoring: " << watch_dir_ << "\n";
        std::cout << "[*] Anomaly threshold: " << threshold_ops_ << " mutations within "
                  << window_ms_.count() << " ms.\n";

        alignas(struct inotify_event) char buf[4096];
        struct pollfd pfd = { inotify_fd_, POLLIN, 0 };

        while (keep_running) {
            int ret = poll(&pfd, 1, 250);
            if (ret < 0) {
                if (errno == EINTR) continue;
                perror("poll");
                break;
            }
            if (ret == 0) continue; // Timeout, no events

            ssize_t len = read(inotify_fd_, buf, sizeof(buf));
            if (len <= 0) continue;

            const struct inotify_event* event;
            for (char* ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
                event = reinterpret_cast<const struct inotify_event*>(ptr);
                if (event->len > 0) {
                    recorder_.push(FsEvent{
                        SystemClock::now(),
                        Clock::now(),
                        event->mask,
                        watch_dir_ + "/" + std::string(event->name)
                    });
                }
            }

            // Check for mutation rate spike
            size_t recent_ops = recorder_.count_recent_mutations(window_ms_);
            if (recent_ops >= threshold_ops_) {
                auto now = Clock::now();
                // 10-second cooldown to prevent trigger storming
                if (now - last_trigger_ > std::chrono::seconds(10)) {
                    last_trigger_ = now;
                    trigger_emergency(recent_ops);
                }
            }
        }
        std::cout << "\n[*] Stopping daemon...\n";
    }

private:
    void trigger_emergency(size_t ops) {
        std::cerr << "\n[!] WARNING: Anomaly detected! (" << ops << " operations within "
                  << window_ms_.count() << " ms)\n";

        // 1. Immediately invoke defensive action (e.g. Btrfs/ZFS CoW snapshot)
        if (!emergency_cmd_.empty()) {
            std::cerr << "[!] Executing defensive action: " << emergency_cmd_ << "\n";
            int res = std::system(emergency_cmd_.c_str());
            (void)res;
        }

        // 2. Dump RAM ring buffer to disk for forensic investigation
        auto ts = SystemClock::to_time_t(SystemClock::now());
        std::stringstream ss;
        ss << "/tmp/flight_recorder_dump_" << ts << ".log";
        recorder_.dump_to_disk(ss.str());

        std::cerr << "[+] Flight recorder dump saved to: " << ss.str() << "\n\n";
    }
};

int main(int argc, char* argv[]) {
    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    std::string target_dir = (argc > 1) ? argv[1] : "/tmp";

    // Default emergency action (e.g. replace with `btrfs subvolume snapshot -r / /@emergency`)
    std::string emergency_action = (argc > 2) ? argv[2] : "echo '[Snapshot Triggered]'";

    // Configuration:
    // - Monitored path
    // - RAM ring buffer capacity: 5000 events
    // - Anomaly threshold: 50 file mutations
    // - Sliding window: 1000 ms (1 second)
    AnomalyWatchdog watchdog(target_dir, 5000, 50, std::chrono::milliseconds(1000), emergency_action);
    watchdog.run();

    return 0;
}
