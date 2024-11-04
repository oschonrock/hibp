#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

struct download {
  explicit download(std::string prefix_) : prefix(std::move(prefix_)) {
    buffer.reserve(1U << 16U); // 64kB should be enough for any file for a while
  }

  static constexpr int max_retries = 5;

  std::vector<char> buffer;
  std::string       prefix;
  int               retries_left = max_retries;
  bool              complete     = false;
};

enum class state { handle_requests, process_queues };

extern std::queue<std::unique_ptr<download>> download_queue; // NOLINT non-const-global

extern std::mutex              thrmutex;  // NOLINT non-const-global
extern std::condition_variable tstate_cv; // NOLINT non-const-global
extern state                   tstate;    // NOLINT non-const-global

extern struct event_base* base; // NOLINT non-const-global

extern std::mutex cerr_mutex; // NOLINT non-const-global

extern std::unordered_map<std::thread::id, std::string> thrnames; // NOLINT non-const-global

extern void thrprinterr([[maybe_unused]] const std::string& msg);
