#include "download/requests.hpp"
#include "download/shared.hpp"
#include <condition_variable>
#include <cstdlib>
#include <curl/curl.h>
#include <curl/multi.h>
#include <event2/event.h>
#include <format>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

// high volume concurrent requests using curl multi and libevent
// curl internal queue management and event driven callbacks

static CURLM*        curl_multi_handle; // NOLINT non-const-global
static struct event* timeout;           // NOLINT non-const-global

// connects an event with a socketfd
struct curl_context_t {
  struct event* event;
  curl_socket_t sockfd;
};

static void curl_perform_event_cb(evutil_socket_t fd, short event, void* arg);

static curl_context_t* create_curl_context(curl_socket_t sockfd) {
  auto* context = new curl_context_t; // NOLINT manual new and delete

  context->sockfd = sockfd;
  context->event =
      event_new(base, static_cast<evutil_socket_t>(sockfd), 0, curl_perform_event_cb, context);

  return context;
}

static void destroy_curl_context(curl_context_t* context) {
  event_del(context->event);
  event_free(context->event);
  delete context; // NOLINT manual new and delete
}

static std::size_t write_data_curl_cb(char* ptr, std::size_t size, std::size_t nmemb,
                                      void* userdata);

void add_download(const std::string& prefix) {
  auto url = "https://api.pwnedpasswords.com/range/" + prefix;

  auto& dl = download_queue.emplace(std::make_unique<download>(prefix));

  CURL* easy = curl_easy_init();
  curl_easy_setopt(easy, CURLOPT_PIPEWAIT, 1L); // wait for multiplexing! key for perf
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_data_curl_cb);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, dl.get());
  curl_easy_setopt(easy, CURLOPT_PRIVATE, dl.get());
  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  // abort if slower than 1000 bytes/sec during 10 seconds
  curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 10L);
  curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1000L);
  curl_multi_add_handle(curl_multi_handle, easy);
}

static bool process_curl_done_msg(CURLMsg* message) {
  bool  found_successful_completions = false;
  CURL* easy_handle                  = message->easy_handle;

  auto result = message->data.result;

  download* dl = nullptr;
  curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &dl);
  curl_multi_remove_handle(curl_multi_handle, easy_handle);

  if (result == CURLE_OK) {
    // safe to change complete flag without lock, because main thread only accesses dl->complete
    // during state::process_queues and we only process new messages (ie this code) during
    // state::handle_requests
    dl->complete = true;
    thrprinterr(std::format("setting '{}' complete = {}", dl->prefix, dl->complete));
    found_successful_completions = true;
    curl_easy_cleanup(easy_handle);
  } else {
    if (dl->retries_left == 0) {
      throw std::runtime_error(std::format("prefix '{}': returned result '{}' after {} retries",
                                           dl->prefix, curl_easy_strerror(message->data.result),
                                           download::max_retries));
    }
    dl->retries_left--;
    dl->buffer.clear(); // throw away anything that was returned
    {
      std::lock_guard lk(cerr_mutex);
      std::cerr << std::format("prefix '{}': returned result '{}'. {} retries left\n", dl->prefix,
                               curl_easy_strerror(message->data.result), dl->retries_left);
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
    case CURLMSG_DONE:
      found_successful_completions |= process_curl_done_msg(message);
      break;

    default:
      thrprinterr("CURLMSG default");
      break;
    }
  }
  if (found_successful_completions) {
    {
      std::lock_guard lk(thrmutex);
      tstate = state::process_queues;
    }
    thrprinterr("notifying main");
    tstate_cv.notify_one();

    // must pause this thread here, as otherwise the callbacks could fire any time
    // cause accesto curls internal queue structures which could still be being modified
    // by main thread
    std::unique_lock lk(thrmutex);
    thrprinterr("waiting for main");
    tstate_cv.wait(lk, []() { return tstate == state::handle_requests; });
  }
}

// event callbacks

static void curl_perform_event_cb(evutil_socket_t /*fd*/, short event, void* arg) {
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
    event_assign(curl_context->event, base, static_cast<evutil_socket_t>(curl_context->sockfd),
                 events, curl_perform_event_cb, curl_context);
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
    throw std::runtime_error("handle_socket_curl_cb: unknown action received.");
  }
  return 0;
}

void init_curl_and_events() {
  base    = event_base_new();
  timeout = evtimer_new(base, timeout_event_cb, nullptr);

  curl_multi_handle = curl_multi_init();
  curl_multi_setopt(curl_multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
  curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETFUNCTION, handle_socket_curl_cb);
  curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERFUNCTION, start_timeout_curl_cb);
}

void shutdown_curl_and_events() {
  curl_multi_cleanup(curl_multi_handle);
  event_free(timeout);
  event_base_free(base);

  libevent_global_shutdown();
  curl_global_cleanup();
}
