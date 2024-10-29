#include "flat_file.hpp"
#include "hibp.hpp"
#include <condition_variable>
#include <curl/curl.h>
#include <curl/multi.h>
#include <event2/event.h>
#include <format>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// threaded app needs mutex for stdio
static std::mutex cerr_mutex; // NOLINT non-const-global
// labels for threads
static std::unordered_map<std::thread::id, std::string> thrnames; // NOLINT non-const-global

static void thrprinterr(const std::string& msg) {
  {
    std::lock_guard lk(cerr_mutex);
    std::cerr << std::format("thread: {:>5}: {}\n", thrnames[std::this_thread::get_id()], msg);
  }
}

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
// we use uniq_ptr<download> to keep the address of the downloads stable as queues change

struct download {
  explicit download(std::string prefix_) : prefix(std::move(prefix_)) {}

  static constexpr int max_retries = 10;

  std::vector<char> buffer;
  std::string       prefix;
  int               retries_left = max_retries;
  bool              complete     = false;
};

static std::queue<std::unique_ptr<download>> download_queue; // NOLINT non-const-global
static std::queue<std::unique_ptr<download>> process_queue;  // NOLINT non-const-global

static std::mutex              queue_mutex; // NOLINT non-const-global
static std::condition_variable queue_cv;    // NOLINT non-const-global

enum class state { handle_requests, process_queues };
static state tstate; // NOLINT non-const-global

constexpr auto max_queue_size      = 300UL;
constexpr auto max_prefix_plus_one = 0x100000UL; // 5 hex digits up to FFFFF
static auto    next_prefix         = 0x0UL;      // NOLINT non-cost-gobal

static void add_download(const std::string& prefix);

static void fill_queue() {
  while (download_queue.size() != max_queue_size && next_prefix != max_prefix_plus_one) {
    auto prefix = std::format("{:05X}", next_prefix++);
    // safe to add_download(), which adds items to curl' internal queue structure,
    // because main (ie this) thread only does this during state::process_queues
    // and curl thread only examines its queue during state::handle_requests
    add_download(prefix);
  }
}

static std::size_t write_lines(flat_file::stream_writer<hibp::pawned_pw>& writer, download& dl) {
  std::stringstream ss;
  ss.rdbuf()->pubsetbuf(dl.buffer.data(), static_cast<std::streamsize>(dl.buffer.size()));

  auto linecount = 0UL;

  for (std::string line, prefixed_line; std::getline(ss, line);) {
    if (line.size() > 0) {
      prefixed_line = dl.prefix + line;
      writer.write(hibp::convert_to_binary(prefixed_line));
      linecount++;
    }
  }
  return linecount;
}

static void process_completed_queue_entries() {
  while (!download_queue.empty()) {
    auto& front = download_queue.front();
    // safe to check complete flagwithout lock, because main (ie this) thread only does this
    // during state::process_queues and curl thread only modifies complete flag during
    // state::handle_requests
    if (!front->complete) {
      break; // these must be done in order, so we don't have to sort afterwards
    }
    process_queue.push(std::move(front));
    download_queue.pop();
  }
  fill_queue();
}

static void write_completed_queue_entries(flat_file::stream_writer<hibp::pawned_pw>& writer) {
  while (!process_queue.empty()) {
    auto& front = process_queue.front();
    write_lines(writer, *front);
    process_queue.pop();
  }
}

// curl internal queue management and event driven callbacks

static CURLM*             curl_multi_handle; // NOLINT non-const-global
static struct event_base* base;              // NOLINT non-const-global
static struct event*      timeout;           // NOLINT non-const-global

// connects an event with a socketfd
struct curl_context_t {
  struct event* event;
  curl_socket_t sockfd;
};

static void curl_perform_event_cb(int fd, short event, void* arg);

static curl_context_t* create_curl_context(curl_socket_t sockfd) {

  auto* context = new curl_context_t; // NOLINT manual new and delete

  context->sockfd = sockfd;
  context->event  = event_new(base, sockfd, 0, curl_perform_event_cb, context);

  return context;
}

static void destroy_curl_context(curl_context_t* context) {
  event_del(context->event);
  event_free(context->event);
  delete context; // NOLINT manual new and delete
}

static std::size_t write_data_curl_cb(char* ptr, std::size_t size, std::size_t nmemb,
                                      void* userdata);

static void add_download(const std::string& prefix) {
  auto url = "https://api.pwnedpasswords.com/range/" + prefix;

  auto& dl = download_queue.emplace(std::make_unique<download>(prefix));
  dl->buffer.reserve(1UL << 16U); // 64kB should be enough for any file for a while

  CURL* easy_handle = curl_easy_init();
  curl_easy_setopt(easy_handle, CURLOPT_PIPEWAIT, 1L); // wait for multiplexing
  curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, write_data_curl_cb);
  curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, dl.get());
  curl_easy_setopt(easy_handle, CURLOPT_PRIVATE, dl.get());
  curl_easy_setopt(easy_handle, CURLOPT_URL, url.c_str());
  curl_multi_add_handle(curl_multi_handle, easy_handle);
}

static bool process_curl_done_msg(CURLMsg* message) {
  bool  found_successful_completions = false;
  CURL* easy_handle                  = message->easy_handle;

  auto result = message->data.result;

  download* dl = nullptr;
  curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &dl);
  curl_multi_remove_handle(curl_multi_handle, easy_handle);

  if (result == CURLE_OK) {
    // safe to change complete flagwithout lock, because main thread only accesses dl->complete
    // during state::process_queues and we only process new messages (ie this code) during
    // state::handle_requests
    dl->complete                 = true;
    found_successful_completions = true;
    curl_easy_cleanup(easy_handle);
  } else {
    if (dl->retries_left == 0) {
      throw std::runtime_error(std::format("prefix '{}': returned result '{}' after {} retries",
                                           curl_easy_strerror(message->data.result), dl->prefix,
                                           download::max_retries));
    }
    dl->retries_left--;
    dl->buffer.clear(); // throw away anything that was returned
    thrprinterr(
        std::format("retrying prefix '{}', retries left = {}", dl->prefix, dl->retries_left));
    curl_multi_add_handle(curl_multi_handle, easy_handle); // try again with same handle
  }
  return found_successful_completions;
}

static void process_curl_messages() {
  CURLMsg* message = nullptr;
  int      pending = 0;

  bool found_successful_completions = false;
  while ((message = curl_multi_info_read(curl_multi_handle, &pending)) != nullptr) {
    switch (message->msg) {
    case CURLMSG_DONE: {
      found_successful_completions |= process_curl_done_msg(message);
      break;
    }
    case CURLMSG_LAST: {
      thrprinterr("CURLMSG_LAST");
      break;
    }

    default: {
      thrprinterr("CURLMSG default");
      break;
    }
    }
  }
  if (found_successful_completions) {
    {
      std::lock_guard lk(queue_mutex);
      tstate = state::process_queues;
    }
    queue_cv.notify_one();

    std::unique_lock lk(queue_mutex);
    queue_cv.wait(lk, []() { return tstate == state::handle_requests; });
  }
}

// event callbacks

static void curl_perform_event_cb(int /*fd*/, short event, void* arg) {
  int running_handles = 0;
  int flags           = 0;

  if (event & EV_READ) flags |= CURL_CSELECT_IN;   // NOLINT -> bool & bitwise
  if (event & EV_WRITE) flags |= CURL_CSELECT_OUT; // NOLINT -> bool & bitwise

  auto* context = static_cast<curl_context_t*>(arg);

  curl_multi_socket_action(curl_multi_handle, context->sockfd, flags, &running_handles);

  process_curl_messages();
}

static void timeout_event_cb(evutil_socket_t /*fd*/, short /*events*/, void* /*arg*/) {
  int running_handles = 0;
  curl_multi_socket_action(curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
  process_curl_messages();
}

// CURL callbacks

static std::size_t write_data_curl_cb(char* ptr, std::size_t size, std::size_t nmemb,
                                      void* userdata) {
  auto* dl       = static_cast<download*>(userdata);
  auto  realsize = size * nmemb;
  std::copy(ptr, ptr + realsize, std::back_inserter(dl->buffer));
  return realsize;
}

static int start_timeout_curl_cb(CURLM* /*multi*/, long timeout_ms, void* /*userp*/) {
  if (timeout_ms < 0) {
    evtimer_del(timeout);
  } else {
    if (timeout_ms == 0) timeout_ms = 1; /* 0 means call socket_action asap */
    struct timeval tv {};
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    evtimer_del(timeout);
    evtimer_add(timeout, &tv);
  }
  return 0;
}

static int handle_socket_curl_cb(CURL* /*easy*/, curl_socket_t s, int action, void* /*userp*/,
                                 void* socketp) {
  curl_context_t* curl_context = nullptr;
  short           events       = 0;

  switch (action) {
  case CURL_POLL_IN:
  case CURL_POLL_OUT:
  case CURL_POLL_INOUT:
    curl_context =
        (socketp != nullptr) ? static_cast<curl_context_t*>(socketp) : create_curl_context(s);

    curl_multi_assign(curl_multi_handle, s, curl_context);

    if (action != CURL_POLL_IN) events |= EV_WRITE; // NOLINT signed-bool-ops
    if (action != CURL_POLL_OUT) events |= EV_READ; // NOLINT signed-bool-ops

    events |= EV_PERSIST; // NOLINT signed bitwise

    event_del(curl_context->event);
    event_assign(curl_context->event, base, curl_context->sockfd, events, curl_perform_event_cb,
                 curl_context);
    event_add(curl_context->event, nullptr);

    break;
  case CURL_POLL_REMOVE:
    if (socketp != nullptr) {
      curl_context = static_cast<curl_context_t*>(socketp);
      event_del(curl_context->event);
      destroy_curl_context(curl_context);
      curl_multi_assign(curl_multi_handle, s, nullptr);
    }
    break;
  default:
    abort();
  }

  return 0;
}

int main() {
  thrnames[std::this_thread::get_id()] = "main";

  if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
    thrprinterr("Could not init curl");
    return EXIT_FAILURE;
  }

  base    = event_base_new();
  timeout = evtimer_new(base, timeout_event_cb, nullptr);

  curl_multi_handle = curl_multi_init();
  curl_multi_setopt(curl_multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
  curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETFUNCTION, handle_socket_curl_cb);
  curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERFUNCTION, start_timeout_curl_cb);

  std::ios_base::sync_with_stdio(false);
  auto writer = flat_file::stream_writer<hibp::pawned_pw>(std::cout);

  fill_queue(); // no need to lock queue here, as curl_event thread is not running yet

  tstate = state::handle_requests;
  std::jthread curl_event_thread([]() { event_base_dispatch(base); });
  thrnames[curl_event_thread.get_id()] = "curl";

  while (!download_queue.empty()) {
    {
      std::unique_lock lk(queue_mutex);
      queue_cv.wait(lk, []() { return tstate == state::process_queues; });
      process_completed_queue_entries(); // shuffle and fill queues
      tstate = state::handle_requests;   // signal curl thread to continue
    }
    queue_cv.notify_one();                 // send control back to curl thread
    write_completed_queue_entries(writer); // do slow work writing to disk
  }

  curl_multi_cleanup(curl_multi_handle);
  event_free(timeout);
  event_base_free(base);

  libevent_global_shutdown();
  curl_global_cleanup();

  return EXIT_SUCCESS;
}
