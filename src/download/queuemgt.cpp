#include "download/queuemgt.hpp"
#include "download/requests.hpp"
#include "download/shared.hpp"
#include "hibp.hpp"
#include <chrono>
#include <condition_variable>
#include <cstddef>
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

// We have 2 threads:
//
// 1. the `requests` thread, which handles the curl/libevent event
// loop to affect the downloads and which manages the `download_queue`
//
// 2. the `queuemgt` thread, which manages the `process_queue` and
// writes the downloads to disk.

// We have 3 queues:
//
// 1. `download_queue` managed by `requests.cpp` (the `requests` thread),
// contains the current set of parallel downloads. the `download`
// struct contains a `complete` flag. `requests.cpp` sets this when
// curl/libevent signal that the download is complete, but it does not
// send an `enq_msg_t` message to queuemgt.cpp (running in the main
// thread), until the `front` item in its `download_queue` is
// `complete`. This is done such that the `enq_msg_t`s are sent in the
// order that they are scheduled. Actually maybe this should be
// reviewed, such that this "ordering" function is some by the done by
// the main thread (here in `queuemgt.cpp`, maybe by using a
// `std::priority_queue`), because otherwise, the `complete` but not
// yet at front of queue `download` slots in `download_queue` are
// reducing the parallelism that `reuests` thread is able to achieve.
//
// 2. `process_queue`, managed by queuemgt.cpp (the `queuemgt`
// thread), contains the completed downloads in order. The `queuemgt`
// thread, takes items from this queue and writes them to disk. The
// items are already in the correct order.

// Both queues are managed by the main thread
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
static clk::time_point start_time;          // NOLINT non-const-global, used in main()
static std::size_t     start_index = 0x0UL; // NOLINT non-cost-gobal

static auto process_queue_compare = [](const auto& a, auto& b) { // NOLINT non-const-global
  return *a > *b; // smallest first (logic inverted in std::priority_queue)
};
static std::priority_queue<std::unique_ptr<download>, std::vector<std::unique_ptr<download>>,
                           decltype(process_queue_compare)>
    process_queue(process_queue_compare); // NOLINT non-const-global

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
    auto            files_todo = cli.index_limit - start_index;
    std::cerr << std::format("Elapsed: {:%H:%M:%S}  Progress: {} / {} files  {:.1f}MB/s  {:5.1f}%  "
                             "  Write queue size: {}\r",
                             elapsed_trunc, files_processed, files_todo,
                             static_cast<double>(bytes_processed) / (1U << 20U) / elapsed_sec,
                             100.0 * static_cast<double>(files_processed) /
                                 static_cast<double>(files_todo),
                             process_queue.size());
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
    logger.log(std::format("message received: msg.size() = {}", msg.size()));
    msg_queue.emplace(std::move(msg));
  }
  msg_cv.notify_one();
}

void finished_downloads() {
  std::lock_guard lk(msgmutex);
  finished_dls = true;
}

// end of msg API

void service_queue(write_fn_t& write_fn, std::size_t next_index) {

  while (true) {
    std::unique_lock lk(msgmutex);
    msg_cv.wait(lk, [] { return !msg_queue.empty() || finished_dls; });

    while (!msg_queue.empty()) {
      auto& msg = msg_queue.front();
      logger.log(std::format("processing message: msg.size() = {}", msg.size()));
      for (auto& dl: msg) {
        process_queue.emplace(std::move(dl));
      }
      msg_queue.pop();
    }
    lk.unlock(); // free up other thread to pass us more messages

    // there is no contention on this queue, and this is slow processing
    logger.log(std::format("process_queue.size() = {}", process_queue.size()));
    while (!process_queue.empty()) {
      const auto& top = process_queue.top();
      if (top->index != next_index) {
        break; // must wait for an earlier batch
      }
      logger.log(std::format("service_queue: writing prefix = {}", top->prefix));
      write_lines(write_fn, *top);
      process_queue.pop();
      next_index++;
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

void run_downloads(write_fn_t write_fn, std::size_t start_index_) {

  std::exception_ptr requests_exception;
  std::exception_ptr queuemgt_exception;

  start_time  = clk::now();   // for progress
  start_index = start_index_; // for progress
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
  bool ex_requests = handle_exception(requests_exception, req_thr_id);
  bool ex_queuemgt = handle_exception(queuemgt_exception, que_thr_id);
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
    std::cerr << std::format("db_file '{}' size was not a multiple of {}, trimmed off {} bytes.\n",
                             filename, sizeof(hibp::pawned_pw), tailsize);
    std::filesystem::resize_file(filename, filesize - tailsize);
  }

  flat_file::database<hibp::pawned_pw> db(filename);

  const auto& last_db_ppw = db.back();

  std::stringstream ss;
  ss << last_db_ppw;
  std::string last_db_hash = ss.str().substr(0, 40);
  std::string prefix       = last_db_hash.substr(0, 5);
  std::string suffix       = last_db_hash.substr(5, 40 - 5);

  std::string filebody = curl_sync_get("https://api.pwnedpasswords.com/range/" + prefix);

  auto pos_colon = filebody.find_last_of(':');
  if (pos_colon == std::string::npos || pos_colon < 35) {
    throw std::runtime_error(std::format("Corrupt last file download with prefix '{}'.", prefix));
  }

  auto last_file_hash = filebody.substr(pos_colon - 35, 35);

  if (last_file_hash == suffix) {
    std::size_t last_prefix{};
    std::from_chars(prefix.c_str(), prefix.c_str() + prefix.length(), last_prefix, 16);

    return last_prefix; // last file was completely written, so we can continue with next one
  }

  // more complex resume technique

  std::cerr << std::format(
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
  std::cerr << std::format("found: trimming file to {}.\n", trimmed_file_size);

  std::filesystem::resize_file(filename, trimmed_file_size);
  db = flat_file::database<hibp::pawned_pw>{filename}; // reload, does this work safely?

  std::size_t last_prefix{};
  std::from_chars(first_file_hash.c_str(), first_file_hash.c_str() + 5, last_prefix, 16);

  // the last file was incompletely written, so we have trimmed and will do it again
  return last_prefix - 1;
}
