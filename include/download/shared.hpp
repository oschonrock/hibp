#pragma once

#include <chrono>
#include <compare>
#include <cstddef>
#include <curl/curl.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// shared types

struct download {
  explicit download(std::size_t index_) : index(index_) {
    prefix = fmt::format("{:05X}", index);
    buffer.reserve(1U << 16U); // 64kB should be enough for any file for a while
  }

  // used in priority_queue to keep items in order
  std::strong_ordering operator<=>(const download& rhs) const { return index <=> rhs.index; }

  static constexpr int max_retries = 5;

  CURL*             easy = nullptr;
  std::size_t       index;
  std::string       prefix;
  std::vector<char> buffer;
  int               retries_left = max_retries;
};

// thread messaging API
using enq_msg_t = std::vector<std::unique_ptr<download>>;
void enqueue_downloads_for_writing(enq_msg_t&& msg);
void finished_downloads();

// app wide cli_config

struct cli_config_t {
  std::string output_db_filename;
  bool        debug        = false;
  bool        progress     = true;
  bool        resume       = false;
  bool        text_out     = false;
  bool        force        = false;
  std::size_t index_limit  = 0x100000;
  std::size_t parallel_max = 300;
};

extern cli_config_t cli;

// simple logging

extern std::mutex                                       cerr_mutex;
extern std::unordered_map<std::thread::id, std::string> thrnames;

struct thread_logger {
  void log(const std::string& msg) const {
    if (debug) {
      const std::lock_guard lk(cerr_mutex);
      // can't portably use high resolution clock here
      auto timestamp = std::chrono::system_clock::now();
      std::cerr << fmt::format("{:%Y-%m-%d %H:%M:%S} thread: {:>9}: {}\n", timestamp,
                               thrnames[std::this_thread::get_id()], msg);
    }
  }
  bool debug = false;
};

extern thread_logger logger;
