#include "download/queuemgt.hpp"
#include "download/requests.hpp"
#include "download/shared.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <curl/curl.h>
#include <curl/multi.h>
#include <event2/event.h>
#include <exception>
#include <format>
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

static auto next_prefix = 0x0UL; // NOLINT non-cost-gobal

static std::size_t files_processed = 0UL; // NOLINT non-const-global
static std::size_t bytes_processed = 0UL; // NOLINT non-const-global

void print_progress() {
  if (cli_config.progress) {
    auto elapsed     = clk::now() - start_time;
    auto elapsed_sec = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
    auto elapsed_sec_trunc = floor<std::chrono::seconds>(elapsed);

    std::lock_guard lk(cerr_mutex);
    std::cerr << std::format("Elapsed: {:%M:%S}  Progress: {} / {} files  {:.1f}MB/s  {:5.1f}%\r",
                             elapsed_sec_trunc, files_processed, cli_config.prefix_limit,
                             static_cast<double>(bytes_processed) / (1U << 20U) / elapsed_sec,
                             100.0 * static_cast<double>(files_processed) /
                                 static_cast<double>(cli_config.prefix_limit));
  }
}

static void fill_download_queue() {
  while (download_queue.size() != cli_config.parallel_max &&
         next_prefix != cli_config.prefix_limit) {
    auto prefix = std::format("{:05X}", next_prefix++);
    // safe to add_download(), which adds items to curl' internal queue structure,
    // because main (ie this) thread only does this during state::process_queues
    // and curl thread only examines its queue during state::handle_requests
    add_download(prefix);
  }
}

static void process_completed_download_queue_entries() {
  thrprinterr(std::format("download_queue.size() = {}", download_queue.size()));
  thrprinterr(std::format("front.complete = {}", download_queue.front()->complete));
  while (!download_queue.empty()) {
    auto& front = download_queue.front();
    // safe to check complete flagwithout lock, because main (ie this) thread only does this
    // during state::process_queues and curl thread only modifies complete flag during
    // state::handle_requests
    if (!front->complete) {
      break; // these must be done in order, so we don't have to sort afterwards
    }
    thrprinterr(std::format("shuffling {}", front->prefix));
    process_queue.push(std::move(front));
    download_queue.pop();
  }
  fill_download_queue();
}

static std::size_t write_lines(flat_file::stream_writer<hibp::pawned_pw>& writer, download& dl) {
  // "embarrassing" copy onto std:string until C++26 P2495R3 Interfacing stringstreams with
  // string_view
  std::stringstream ss(std::string(dl.buffer.data(), dl.buffer.size()));

  auto recordcount = 0UL;

  for (std::string line, prefixed_line; std::getline(ss, line);) {
    if (line.size() > 0) {
      prefixed_line = dl.prefix + line;
      writer.write(hibp::convert_to_binary(prefixed_line));
      recordcount++;
    }
  }
  thrprinterr(std::format("wrote '{}' in binary, recordcount = {}", dl.prefix, recordcount));
  bytes_processed += dl.buffer.size();
  return recordcount;
}

static void
write_completed_process_queue_entries(flat_file::stream_writer<hibp::pawned_pw>& writer) {
  while (!process_queue.empty()) {
    auto& front = process_queue.front();
    write_lines(writer, *front);
    // there may exist an optimisation that retains the `download` for future add_download()
    // so that the `buffer` allocation can be reused after being clear()'ed
    process_queue.pop();
    files_processed++;
    print_progress();
  }
}

void service_queue(flat_file::stream_writer<hibp::pawned_pw>& writer) {
  while (!download_queue.empty()) {
    {
      thrprinterr("waiting for curl");
      std::unique_lock lk(thrmutex);
      if (!tstate_cv.wait_for(lk, std::chrono::seconds(10), []() { return tstate == state::process_queues; })) {
        throw std::runtime_error("Timed out waiting for requests thread");
      }
      process_completed_download_queue_entries(); // shuffle and fill queues
      tstate = state::handle_requests;            // signal curl thread to continue
    }
    thrprinterr("notifying curl");
    tstate_cv.notify_one();                        // send control back to curl thread
    write_completed_process_queue_entries(writer); // do slow work writing to disk
  }
  if (cli_config.progress) {
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

void run_threads(flat_file::stream_writer<hibp::pawned_pw>& writer) {
  std::exception_ptr requests_exception_ptr;
  std::exception_ptr queuemgt_exception_ptr;

  start_time = clk::now();
  init_curl_and_events();

  auto que_thr_id = std::this_thread::get_id();
  thrnames[que_thr_id] = "queuemgt";
  fill_download_queue(); // no need to lock mutex here, as curl_event thread is not running yet

  tstate = state::handle_requests;

  std::thread requests_thread([&]() {
    try {
      event_base_dispatch(base);
    } catch (...) {
      requests_exception_ptr = std::current_exception();
    }
  });

  auto req_thr_id = requests_thread.get_id();
  thrnames[req_thr_id] = "requests";

  try {
    service_queue(writer);
  } catch (...) {
    queuemgt_exception_ptr = std::current_exception();
  }

  requests_thread.join();
  shutdown_curl_and_events();
  
  // use temps to avoid short cct eval
  bool ex_requests = handle_exception(requests_exception_ptr, req_thr_id);
  bool ex_queuemgt = handle_exception(queuemgt_exception_ptr, que_thr_id);
  if (ex_requests || ex_queuemgt) {
    throw std::runtime_error("Thread exceptions thrown as above");
  }
}
