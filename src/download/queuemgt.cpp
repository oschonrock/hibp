#include "download/queuemgt.hpp"
#include "download/requests.hpp"
#include "download/shared.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <algorithm>
#if __has_include(<bits/chrono.h>)
#include <bits/chrono.h>
#endif
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// queue management

// We have 2 threads:
//
// 1. the `requests` thread, which handles the curl/libevent event
// loop to affect the downloads. It also manages the `download_queue`
//
// 2. the `queuemgt`(main) thread, which manages the `process_queue` and
// the `message_queue` writes the downloads to disk.

// We have 3 queues:
//
// 1. `download_slots` managed by `requests.cpp` (the `requests`
// thread), contains the current set of parallel downloads. it is an
// unordered_map, so not really a 'queue' as such, just a collection
// of parallel 'slots'. As each set of downloads completes the
// requests thread sends an `enq_msg_t` to queuemgt.cpp (running in
// the main thread) by calling `enqueue_downloads_for_writing()`. This
// inserts the message into the `message_queue`.  When all dowloads
// are done it calls finished_downloads().
//
// 2. `message_queue` managed by queuemgr.cpp. When receiving messages
// from the requests thread, the main thread is notified and it
// shuffles the contents of the messages into the process_queue. The
// `message_queue` is the minimal communication interface between the
// two threads.
//
// 3. The `process_queue` is a std::priority_queue which reorders the
// dowloads into index order and items are only removed when the
// `next_process_index` is at top().

// we use std::unique_ptr<download> as the queue and message elements
// throughout to keep the address of the downloads stable as they move
// through the 3 queues. This ensures the curl C-APi has stable pointers.

namespace hibp::dnl {

namespace qmgt {

using clk = std::chrono::high_resolution_clock;

clk::time_point start_time;
std::size_t     start_index = 0x0UL;

auto process_queue_compare = [](const auto& a, auto& b) {
  return *a > *b; // smallest first (logic inverted in std::priority_queue)
};
std::priority_queue<std::unique_ptr<download>, std::vector<std::unique_ptr<download>>,
                    decltype(process_queue_compare)>
    process_queue(process_queue_compare);

std::mutex              msgmutex;
std::queue<enq_msg_t>   msg_queue;
std::condition_variable msg_cv;
bool                    finished_dls = false;

std::size_t files_processed = 0UL;
std::size_t bytes_processed = 0UL;

void print_progress() {
  if (cli.progress) {
    auto elapsed       = clk::now() - start_time;
    auto elapsed_trunc = floor<std::chrono::seconds>(elapsed);
    auto elapsed_sec   = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();

    const std::lock_guard lk(cerr_mutex);
    auto                  files_todo = cli.index_limit - start_index;
    std::cerr << fmt::format("Elapsed: {:%H:%M:%S}  Progress: {} / {} files  {:.1f}MB/s  {:5.1f}%  "
                             "  Write queue size: {:4d}\r",
                             elapsed_trunc, files_processed, files_todo,
                             static_cast<double>(bytes_processed) / (1U << 20U) / elapsed_sec,
                             100.0 * static_cast<double>(files_processed) /
                                 static_cast<double>(files_todo),
                             process_queue.size());
  }
}

std::size_t write_lines(write_fn_t& write_fn, download& dl) {
  // "embarrassing" copy onto std:string until C++26 P2495R3 Interfacing stringstreams with
  // string_view
  std::stringstream ss(std::string(dl.buffer.data(), dl.buffer.size()));

  auto recordcount = 0UL;

  for (std::string line, prefixed_line; std::getline(ss, line);) {
    if (!line.empty()) {
      prefixed_line = dl.prefix + line;
      prefixed_line.erase(prefixed_line.find_last_not_of('\r') + 1);

      // calls text_writer or hibp_ff_sw, the latter via implicit conversion
      write_fn(prefixed_line);

      recordcount++;
    }
  }
  logger.log(fmt::format("wrote {} binary records with prefix {}", recordcount, dl.prefix));
  bytes_processed += dl.buffer.size();
  return recordcount;
}

bool handle_exception(const std::exception_ptr& exception_ptr, std::thread::id thr_id) {
  if (exception_ptr) {
    try {
      std::rethrow_exception(exception_ptr);
    } catch (const std::exception& e) {
      std::cerr << fmt::format("Caught exception in {} thread: {}\n", thrnames[thr_id], e.what());
    }
    return true;
  }
  return false;
}

} // namespace qmgt

// msg API between threads
void enqueue_downloads_for_writing(enq_msg_t&& msg) {
  {
    const std::lock_guard lk(qmgt::msgmutex);
    logger.log(fmt::format("message received: msg.size() = {}", msg.size()));
    qmgt::msg_queue.emplace(std::move(msg));
  }
  qmgt::msg_cv.notify_one();
}

// msg API between threads
void finished_downloads() {
  const std::lock_guard lk(qmgt::msgmutex);
  qmgt::finished_dls = true;
}

void service_queue(write_fn_t& write_fn, std::size_t next_index) {

  while (true) {
    std::unique_lock lk(qmgt::msgmutex);
    qmgt::msg_cv.wait(lk, [] { return !qmgt::msg_queue.empty() || qmgt::finished_dls; });

    // bring messages over into process_queue
    while (!qmgt::msg_queue.empty()) {
      auto& msg = qmgt::msg_queue.front();
      logger.log(fmt::format("processing message: msg.size() = {}", msg.size()));
      for (auto& dl: msg) {
        qmgt::process_queue.emplace(std::move(dl));
      }
      qmgt::msg_queue.pop();
    }
    lk.unlock(); // free up other thread to pass us more messages

    // now do the work in the process queue
    // there is no contention on this queue, and this is slow processing
    logger.log(fmt::format("process_queue.size() = {}", qmgt::process_queue.size()));
    while (!qmgt::process_queue.empty()) {
      const auto& top = qmgt::process_queue.top();
      if (top->index != next_index) {
        break; // must wait for an earlier batch to preserve the correct order
      }
      logger.log(fmt::format("service_queue: writing prefix = {}", top->prefix));
      qmgt::write_lines(write_fn, *top);
      qmgt::process_queue.pop();
      next_index++;
      qmgt::files_processed++;
    }
    qmgt::print_progress();
    if (qmgt::finished_dls && qmgt::msg_queue.empty()) break;
  }
  if (cli.progress) {
    std::cerr << "\n"; // clear line after progress if being shown, bit nasty
  }
}

// main entry point for the download process
void run(write_fn_t write_fn, std::size_t start_index_) {

  std::exception_ptr requests_exception;
  std::exception_ptr queuemgt_exception;

  qmgt::start_time  = qmgt::clk::now(); // for progress
  qmgt::start_index = start_index_;     // for progress
  init_curl_and_events();

  auto que_thr_id      = std::this_thread::get_id();
  thrnames[que_thr_id] = "queuemgt";

  std::thread requests_thread([&]() {
    try {
      run_event_loop(start_index_);
    } catch (...) {
      requests_exception = std::current_exception();
    }
  });

  auto req_thr_id      = requests_thread.get_id();
  thrnames[req_thr_id] = "requests";

  try {
    service_queue(write_fn, start_index_);
  } catch (...) {
    queuemgt_exception = std::current_exception();
  }

  requests_thread.join();

  // use temps to avoid short cct eval
  const bool ex_requests = qmgt::handle_exception(requests_exception, req_thr_id);
  const bool ex_queuemgt = qmgt::handle_exception(queuemgt_exception, que_thr_id);
  if (ex_requests || ex_queuemgt) {
    curl_and_event_cleanup();
    throw std::runtime_error("Thread exceptions thrown as above. Sorry, we are aborting. You can "
                             "try rerunning with `--resume`");
  }
  shutdown_curl_and_events();
}

// utility function for --resume
std::size_t get_last_prefix(const std::string& filename) {
  auto filesize = std::filesystem::file_size(filename);
  if (auto tailsize = filesize % sizeof(hibp::pawned_pw); tailsize != 0) {
    std::cerr << fmt::format("db_file '{}' size was not a multiple of {}, trimmed off {} bytes.\n",
                             filename, sizeof(hibp::pawned_pw), tailsize);
    std::filesystem::resize_file(filename, filesize - tailsize);
  }

  flat_file::database<hibp::pawned_pw> db(filename);

  const auto& last_db_ppw = db.back();

  std::stringstream ss;
  ss << last_db_ppw;
  const std::string last_db_hash = ss.str().substr(0, 40);
  std::string       prefix       = last_db_hash.substr(0, 5);
  const std::string suffix       = last_db_hash.substr(5, 40 - 5);

  const std::string filebody = curl_sync_get("https://api.pwnedpasswords.com/range/" + prefix);

  auto pos_colon = filebody.find_last_of(':');
  if (pos_colon == std::string::npos || pos_colon < 35) {
    throw std::runtime_error(fmt::format("Corrupt last file download with prefix '{}'.", prefix));
  }

  auto last_file_hash = filebody.substr(pos_colon - 35, 35);

  if (last_file_hash == suffix) {
    std::size_t last_prefix{};
    std::from_chars(prefix.c_str(), prefix.c_str() + prefix.length(), last_prefix, 16);

    return last_prefix; // last file was completely written, so we can continue with next one
  }

  // more complex resume technique

  std::cerr << fmt::format(
      "Last converted hash not found at end of last retrieved file.\n"
      "Searching backward to hash just before beginning of last retrieved file.\n");

  auto first_file_hash = prefix + filebody.substr(0, 35);
  auto needle          = hibp::pawned_pw(first_file_hash);
  auto rbegin          = std::make_reverse_iterator(db.end());
  auto rend            = std::make_reverse_iterator(db.begin());
  auto found_iter      = std::find(rbegin, rend, needle);
  if (found_iter == rend) {
    throw std::runtime_error(
        "Not found at all, sorry you will need to start afresh without `--resume`.\n");
  }

  auto trimmed_file_size =
      static_cast<std::size_t>(rend - found_iter - 1) * sizeof(hibp::pawned_pw);
  std::cerr << fmt::format("found: trimming file to {}.\n", trimmed_file_size);

  std::filesystem::resize_file(filename, trimmed_file_size);
  db = flat_file::database<hibp::pawned_pw>{filename}; // reload db

  std::size_t last_prefix{};
  std::from_chars(first_file_hash.c_str(), first_file_hash.c_str() + 5, last_prefix, 16);

  // the last file was incompletely written, so we have trimmed and will do it again
  return last_prefix - 1;
}

} // namespace hibp::dnl
