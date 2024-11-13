#include "download/queuemgt.hpp"
#include "download/download.hpp"
#include "download/requests.hpp"
#include "download/shared.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <curl/curl.h>
#include <curl/multi.h>
#include <event2/event.h>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// queue management

// We have 2 queues. Both queues are managed by the main thread
//
// we feed process_queue from download_queue to be able to unclock curl thread ASAP
// before we do the hard work of converting to binary format and writing to disk
//
// curl_event_thread notifies main thread, when there is work to be done on download_queue
// main thread then shuffles completed items to process_queue and refills the download_queue with
// new items.
//
// main thread notifies curl thread when it has finished updating both queues so curl
// thread can continue
//
// we use uniq_ptr<download> to keep the address of the downloads stable as queues changes

using clk = std::chrono::high_resolution_clock;
static clk::time_point start_time; // NOLINT non-const-global, used in main()

static std::queue<std::unique_ptr<download>> process_queue; // NOLINT non-const-global

static std::mutex              msgmutex;             // NOLINT non-const-global
static std::queue<enq_msg_t>   msg_queue;            // NOLINT non-const-global
static std::condition_variable msg_cv;               // NOLINT non-const-global
static bool                    finished_dls = false; // NOLINT non-const-global

static std::size_t files_processed = 0UL; // NOLINT non-const-global
static std::size_t bytes_processed = 0UL; // NOLINT non-const-global

enum class state { handle_requests, process_queues };

void print_progress() {
  if (cli.progress) {
    auto elapsed       = clk::now() - start_time;
    auto elapsed_trunc = floor<std::chrono::seconds>(elapsed);
    auto elapsed_sec   = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();

    std::lock_guard lk(cerr_mutex);
    auto            files_todo = cli.prefix_limit - start_prefix;
    std::cerr << std::format(
        "Elapsed: {:%H:%M:%S}  Progress: {} / {} files  {:.1f}MB/s  {:5.1f}%\r", elapsed_trunc,
        files_processed, files_todo,
        static_cast<double>(bytes_processed) / (1U << 20U) / elapsed_sec,
        100.0 * static_cast<double>(files_processed) / static_cast<double>(files_todo));
  }
}

static std::size_t write_lines(write_fn_t& write_fn, download& dl) {
  // "embarrassing" copy onto std:string until C++26 P2495R3 Interfacing stringstreams with
  // string_view
  std::stringstream ss(std::string(dl.buffer.data(), dl.buffer.size()));

  auto recordcount = 0UL;

  for (std::string line, prefixed_line; std::getline(ss, line);) {
    if (line.size() > 0) {
      prefixed_line = dl.prefix + line;
      prefixed_line.erase(prefixed_line.find_last_not_of('\r') + 1);

      // calls text_writer or hibp_ff_sw, the latter via implicit conversion
      write_fn(prefixed_line);

      recordcount++;
    }
  }
  logger.log(std::format("wrote '{}' in binary, recordcount = {}", dl.prefix, recordcount));
  bytes_processed += dl.buffer.size();
  return recordcount;
}

// the following 2 functions are the msg api and called from other thread(s)

void enqueue_downloads_for_writing(enq_msg_t&& msg) {
  {
    std::lock_guard lk(msgmutex);
    logger.log("message received");
    msg_queue.emplace(std::move(msg));
  }
  msg_cv.notify_one();
}

void finished_downloads() {
  std::lock_guard lk(msgmutex);
  finished_dls = true;
}

// end of msg API

void service_queue(write_fn_t& write_fn) {
  while (true) {
    std::unique_lock lk(msgmutex);
    msg_cv.wait(lk, [] { return !msg_queue.empty() || finished_dls; });

    while (!msg_queue.empty()) {
      auto& msg = msg_queue.front();
      logger.log("processing msg");
      for (auto& dl: msg) {
        process_queue.emplace(std::move(dl));
      }
      msg_queue.pop();
    }
    lk.unlock(); // free up other thread to pass us more messages

    // there is no contention on this queue, and this is slow processing
    while (!process_queue.empty()) {
      auto& front = process_queue.front();
      logger.log(std::format("service_queue: writing prefix = {}", front->prefix));
      write_lines(write_fn, *front);
      // there may exist an optimisation that retains the `download` for future add_download()
      // so that the `buffer` allocation can be reused after being clear()'ed
      process_queue.pop();
      files_processed++;
      print_progress();
    }
    if (finished_dls && msg_queue.empty()) break;
  }
  if (cli.progress) {
    std::cerr << "\n"; // clear line after progress if being shown, bit nasty
  }
}

bool handle_exception(const std::exception_ptr& exception_ptr, std::thread::id thr_id) {
  if (exception_ptr) {
    try {
      std::rethrow_exception(exception_ptr);
    } catch (const std::exception& e) {
      std::cerr << std::format("Caught exception in {} thread: {}\n", thrnames[thr_id], e.what());
    }
    return true;
  }
  return false;
}

void run_threads(write_fn_t& write_fn, const cli_config_t& cli_config) {
  cli = cli_config;

  std::exception_ptr requests_exception;
  std::exception_ptr queuemgt_exception;

  start_time = clk::now();
  init_curl_and_events();

  auto que_thr_id      = std::this_thread::get_id();
  thrnames[que_thr_id] = "queuemgt";
  fill_download_queue();

  std::thread requests_thread([&]() {
    try {
      event_base_dispatch(base);
      finished_downloads();
    } catch (...) {
      requests_exception = std::current_exception();
    }
  });

  auto req_thr_id      = requests_thread.get_id();
  thrnames[req_thr_id] = "requests";

  try {
    service_queue(write_fn);
  } catch (...) {
    queuemgt_exception = std::current_exception();
  }

  requests_thread.join();

  // use temps to avoid short cct eval
  bool ex_requests = handle_exception(requests_exception, req_thr_id);
  bool ex_queuemgt = handle_exception(queuemgt_exception, que_thr_id);
  if (ex_requests || ex_queuemgt) {
    curl_and_event_cleanup();
    throw std::runtime_error("Thread exceptions thrown as above. Sorry, we are aborting. You can "
                             "try rerunning with `--resume`");
  }
  shutdown_curl_and_events();
}
