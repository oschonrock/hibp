#include "download/shared.hpp"
#include <condition_variable>
#include <curl/curl.h>
#include <format>
#include <iostream>
#include <memory>
#include <queue>
#include <unordered_map>

struct event_base* base; // NOLINT non-const-global

std::queue<std::unique_ptr<download>> download_queue; // NOLINT non-const-global

std::mutex              thrmutex;  // NOLINT non-const-global
std::condition_variable tstate_cv; // NOLINT non-const-global
state                   tstate;    // NOLINT non-const-global

// threaded app needs mutex for stdio
[[maybe_unused]] std::mutex cerr_mutex; // NOLINT non-const-global
// labels for threads
std::unordered_map<std::thread::id, std::string> thrnames; // NOLINT non-const-global

void thrprinterr([[maybe_unused]] const std::string& msg) {
#ifndef NDEBUG
  std::lock_guard lk(cerr_mutex);
  std::cerr << std::format("thread: {:>9}: {}\n", thrnames[std::this_thread::get_id()], msg);
#endif
}
