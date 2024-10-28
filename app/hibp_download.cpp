#include "flat_file.hpp"
#include "hibp.hpp"
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <curl/curl.h>
#include <curl/multi.h>
#include <deque>
#include <event2/event.h>
#include <format>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static struct event_base* base;              // NOLINT non-const-global
static CURLM*             curl_multi_handle; // NOLINT non-const-global
static struct event*      timeout;           // NOLINT non-const-global

struct curl_context_t {
  struct event* event;
  curl_socket_t sockfd;
};

static void curl_perform(int fd, short event, void* arg);

static curl_context_t* create_curl_context(curl_socket_t sockfd) {

  auto* context = new curl_context_t; // NOLINT

  context->sockfd = sockfd;
  context->event  = event_new(base, sockfd, 0, curl_perform, context);

  return context;
}

static void destroy_curl_context(curl_context_t* context) {
  event_del(context->event);
  event_free(context->event);
  delete context; // NOLINT
}

struct download {
  explicit download(std::string prefix_) : prefix(std::move(prefix_)) {}

  static constexpr int max_retries = 10;

  std::vector<char> buffer;
  std::string       prefix;
  int               retries_left = max_retries;
  bool              complete     = false;
};

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* dl       = static_cast<download*>(userdata);
  auto  realsize = size * nmemb;
  std::copy(ptr, ptr + realsize, std::back_insert_iterator(dl->buffer));
  return realsize;
}

// both queues are managed by the main thread
//
// we feed process_queue from download_queue to be able to unclock curl thread ASAP
// before we do the hard work of converting to binary format and writing to disk
//
// curl_event_thread notifies main thread, when there is work to be done on download_queue
// main thread then shuffles completed items to process_queue and refills the download_queue with
// new items main thread notifies curl thread when it has finished updating both queues so curl
// thread can continue
static std::mutex cerr_mutex; // NOLINT non-const-global

// we use uniq_ptr<download> to keep the address of the downloads stable as queues change
static std::deque<std::unique_ptr<download>> download_queue; // NOLINT non-const-global
static std::deque<std::unique_ptr<download>> process_queue;  // NOLINT non-const-global

static std::mutex              queue_mutex;                            // NOLINT non-const-global
static std::condition_variable queue_cv;                               // NOLINT non-const-global
static bool                    downloads_ready_for_processing = false; // NOLINT non-const-global
static bool                    downloads_processed            = false; // NOLINT non-const-global

constexpr auto max_queue_size      = 300L;
constexpr auto max_prefix_plus_one = 0x100000UL; // 5 hex digits up to FFFFF
static auto    next_prefix         = 0x0UL;      // NOLINT non-cost-gobal

static void add_download(const std::string& prefix) {
  auto url = "https://api.pwnedpasswords.com/range/" + prefix;

  auto& dl = download_queue.emplace_back(std::make_unique<download>(prefix));
  dl->buffer.reserve(1UL << 16U); // 64kB should be enough for any file for a while

  CURL* easy_handle = curl_easy_init();
  curl_easy_setopt(easy_handle, CURLOPT_PIPEWAIT, 1L); // wait for multiplexing
  curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, dl.get());
  curl_easy_setopt(easy_handle, CURLOPT_PRIVATE, dl.get());
  curl_easy_setopt(easy_handle, CURLOPT_URL, url.c_str());
  curl_multi_add_handle(curl_multi_handle, easy_handle);
}

static void fill_queue() {
  while (download_queue.size() != max_queue_size && next_prefix != max_prefix_plus_one) {
    auto prefix = std::format("{:05X}", next_prefix++);
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

static void process_complete_queue_entries() {
  while (!download_queue.empty()) {
    auto& front = download_queue.front();
    if (!front->complete) {
      break; // these must be done in order, so we don't have to sort afterwards
    }
    process_queue.push_back(std::move(front));
    download_queue.pop_front();
  }
  fill_queue();
}

static void write_complete_queue_entries(flat_file::stream_writer<hibp::pawned_pw>& writer) {
  while (!process_queue.empty()) {
    auto& front = process_queue.front();
    write_lines(writer, *front);
    process_queue.pop_front();
  }
}

static bool process_curl_done_msg(CURLMsg* message) {
  bool  found_successful_completions = false;
  CURL* easy_handle                  = message->easy_handle;

  auto result = message->data.result;

  download* dl = nullptr;
  curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &dl);
  curl_multi_remove_handle(curl_multi_handle, easy_handle);

  if (result == CURLE_OK) {
    {
      std::lock_guard lk(queue_mutex);
      dl->complete = true;
    }
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
    {
      std::lock_guard lk(cerr_mutex);
      std::cerr << std::format("retrying prefix '{}', retries left = {}\n", dl->prefix,
                               dl->retries_left);
    }
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
      {
        std::lock_guard lk(cerr_mutex);
        std::cerr << "CURLMSG_LAST\n";
      }
      break;
    }

    default: {
      {
        std::lock_guard lk(cerr_mutex);
        std::cerr << "CURLMSG default\n";
      }
      break;
    }
    }
  }
  if (found_successful_completions) {
    {
      std::lock_guard lk(queue_mutex);
      downloads_ready_for_processing = true;
      downloads_processed            = false;
    }
    queue_cv.notify_one();

    std::unique_lock lk(queue_mutex);
    queue_cv.wait(lk, []() { return downloads_processed; });
  }
}

static void curl_perform(int /*fd*/, short event, void* arg) {
  int running_handles = 0;
  int flags           = 0;

  if (event & EV_READ) flags |= CURL_CSELECT_IN;   // NOLINT -> bool & bitwise
  if (event & EV_WRITE) flags |= CURL_CSELECT_OUT; // NOLINT -> bool & bitwise

  auto* context = static_cast<curl_context_t*>(arg);

  curl_multi_socket_action(curl_multi_handle, context->sockfd, flags, &running_handles);

  process_curl_messages();
}

static void on_timeout(evutil_socket_t /*fd*/, short /*events*/, void* /*arg*/) {
  int running_handles = 0;
  curl_multi_socket_action(curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
  process_curl_messages();
}

static int start_timeout(CURLM* /*multi*/, long timeout_ms, void* /*userp*/) {
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

static int handle_socket(CURL* /*easy*/, curl_socket_t s, int action, void* /*userp*/,
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

    events |= EV_PERSIST; // NOLINT bitwise

    event_del(curl_context->event);
    event_assign(curl_context->event, base, curl_context->sockfd, events, curl_perform,
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
  if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
    std::cerr << "Could not init curl\n";
    return 1;
  }

  base    = event_base_new();
  timeout = evtimer_new(base, on_timeout, NULL);

  curl_multi_handle = curl_multi_init();
  curl_multi_setopt(curl_multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
  curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETFUNCTION, handle_socket);
  curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERFUNCTION, start_timeout);

  std::ios_base::sync_with_stdio(false);
  auto writer = flat_file::stream_writer<hibp::pawned_pw>(std::cout);

  fill_queue(); // no need to lock queue here, as curl_event thread is not running yet

  std::jthread curl_event_thread([]() { event_base_dispatch(base); });

  while (!download_queue.empty()) {
    {
      std::unique_lock lk(queue_mutex);
      queue_cv.wait(lk, []() { return downloads_ready_for_processing; });
      process_complete_queue_entries(); // shuffle and fill queues
      downloads_ready_for_processing = false;
      downloads_processed            = true; // signal curl thread
    }
    queue_cv.notify_one();                // send control back to curl thread
    write_complete_queue_entries(writer); // do slow work writing to disk
  }

  curl_multi_cleanup(curl_multi_handle);
  event_free(timeout);
  event_base_free(base);

  libevent_global_shutdown();
  curl_global_cleanup();

  return EXIT_SUCCESS;
}
