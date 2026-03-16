#include "dnl/queuemgt.hpp"
#include "dnl/requests.hpp"
#include "dnl/shared.hpp"
#include <stop_token>
#if __has_include(<bits/chrono.h>)
#include <bits/chrono.h>
#endif
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fmt/format.h>
#include <iostream>
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

namespace {

using clk = std::chrono::high_resolution_clock;

clk::time_point start_time;
std::size_t     start_index = 0x0UL;

auto process_queue_compare = [](const auto& a, auto& b) {
  return *a > *b; // smallest first (logic inverted in std::priority_queue)
};
std::priority_queue<std::unique_ptr<download>, std::vector<std::unique_ptr<download>>,
                    decltype(process_queue_compare)>
    process_queue(process_queue_compare);

std::mutex                  msgmutex;
std::queue<enq_msg_t>       msg_queue;
std::condition_variable_any msg_cv; // _any for stop_token
bool                        finished_dls = false;

std::size_t files_processed = 0UL;
std::size_t bytes_processed = 0UL;

void print_progress() {
  if (cli.progress) {
    auto elapsed       = clk::now() - start_time;
    auto elapsed_trunc = floor<std::chrono::seconds>(elapsed);
    auto elapsed_sec   = duration_cast<std::chrono::duration<double>>(elapsed).count();

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

} // namespace

// msg API called by requests thread
void enqueue_downloads_for_writing(enq_msg_t&& msg) {
  {
    const std::lock_guard lk(msgmutex);
    auto                  msg_size = msg.size();
    msg_queue.emplace(std::move(msg));
    logger.log(fmt::format("enqueue_downloads_for_writing(): "
                           "acquired lock and received mesage of size = {}, "
                           "notifying queuemgt thread",
                           msg_size));
  }
  msg_cv.notify_one();
}

// msg API called by requests thread
void finished_downloads() {
  {
    const std::lock_guard lk(msgmutex);
    finished_dls = true;
    logger.log("finished_downloads(): acquired lock, "
               "set finished_dls = true, "
               "notifying queuemgt thread");
  }
  msg_cv.notify_one();
}

namespace {

void service_queue(write_fn_t& write_fn, std::size_t next_index,
                   std::stop_token stoken) { // NOLINT stoken

  while (true) {
    std::unique_lock lk(msgmutex);
    msg_cv.wait(lk, stoken, [&] { return !msg_queue.empty() || finished_dls; });

    if (stoken.stop_requested()) {
      logger.log("stop request received: bailing out");
      break;
    }

    // bring messages over into process_queue
    while (!msg_queue.empty()) {
      auto& msg = msg_queue.front();
      logger.log(fmt::format("processing message: msg.size() = {}", msg.size()));
      for (auto& dl: msg) {
        process_queue.emplace(std::move(dl)); // moving the uniqptrs & ownership over
      }
      msg_queue.pop();
    }
    if (finished_dls && msg_queue.empty() && process_queue.empty()) {
      lk.unlock(); // ensure no lock on exit, just for clarity, would happen in ~lk anyway
      break;       // normal finish
    }

    lk.unlock(); // free up other thread to pass us more messages

    // now do the work in the process queue
    // there is no contention on this queue, and this is slow processing
    logger.log(fmt::format("process_queue.size() = {}", process_queue.size()));
    while (!process_queue.empty()) {
      const auto& top = process_queue.top();
      if (top->index != next_index) {
        break; // must wait for an earlier batch to preserve the correct order
      }
      logger.log(fmt::format("service_queue: writing prefix = {}", top->prefix));
      write_lines(write_fn, *top);
      process_queue.pop();
      next_index++;
      files_processed++;
    }
    print_progress();
  }
  if (cli.progress) {
    std::cerr << "\n"; // clear line after progress if being shown, bit nasty
  }
}

} // namespace

// main entry point for the download process
void run(write_fn_t write_fn, std::size_t start_index_, bool testing_) {
  std::exception_ptr requests_exception;
  std::exception_ptr queuemgt_exception;

  start_time  = clk::now();   // for progress
  start_index = start_index_; // for progress
  init_curl_and_events();

  std::thread::id que_thr_id;
  std::thread::id req_thr_id;
  {
    std::stop_source que_stop_source;
    std::stop_source req_stop_source;

    const std::jthread requests_thread([&]() {
      try {
        run_event_loop(start_index_, testing_, req_stop_source.get_token());
      } catch (...) {
        requests_exception = std::current_exception();
        logger.log("exception caught: requesting stop of queuemgt thread via stop_token");
        que_stop_source.request_stop();
      }
    });

    req_thr_id           = requests_thread.get_id();
    thrnames[req_thr_id] = "requests";

    const std::jthread queuemgt_thread([&]() {
      try {
        service_queue(write_fn, start_index_, que_stop_source.get_token());
      } catch (...) {
        queuemgt_exception = std::current_exception();
        logger.log("exception caught: requesting stop of requests thread via stop_token");
        req_stop_source.request_stop();
      }
    });
    que_thr_id           = queuemgt_thread.get_id();
    thrnames[que_thr_id] = "queuemgt";

  } // wait here until threads join

  // use temps to avoid short cct eval
  const bool ex_requests = handle_exception(requests_exception, req_thr_id);
  const bool ex_queuemgt = handle_exception(queuemgt_exception, que_thr_id);
  if (ex_requests || ex_queuemgt) {
    curl_and_event_cleanup();
    throw std::runtime_error("Thread exceptions thrown as above. Sorry, we are aborting. You can "
                             "try rerunning with `--resume` in many download modes.");
  }
  shutdown_curl_and_events();
}

} // namespace hibp::dnl
