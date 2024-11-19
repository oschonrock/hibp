#include "download/requests.hpp"
#include "download/shared.hpp"
#include "fmt/core.h"
#include <cstddef>
#include <cstdlib>
#include <curl/curl.h>
#include <curl/multi.h>
#include <event2/event.h>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// double indirection via unique_ptr. Strictly unecessary for address stability, but consistent with
// other queues and non critical
static std::unordered_map<std::size_t, std::unique_ptr<download>> download_slots;

static CURLM* curl_multi_handle;
static event* timeout;

static event_base* ebase;

static std::size_t next_index = 0x0UL;

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
      event_new(ebase, static_cast<evutil_socket_t>(sockfd), 0, curl_perform_event_cb, context);

  return context;
}

static void destroy_curl_context(curl_context_t* context) {
  event_del(context->event);
  event_free(context->event);
  delete context; // NOLINT manual new and delete
}

static std::size_t write_data_curl_cb(char* ptr, std::size_t size, std::size_t nmemb,
                                      void* userdata);

static void add_download(std::size_t index) {
  auto [dl_iter, inserted] =
      download_slots.insert(std::make_pair(index, std::make_unique<download>(index)));

  if (!inserted) {
    throw std::runtime_error(fmt::format("unexpected condition: index {} already existed", index));
  }
  auto& dl  = dl_iter->second;
  auto  url = "https://api.pwnedpasswords.com/range/" + dl->prefix;

  CURL* easy = curl_easy_init();
  curl_easy_setopt(easy, CURLOPT_PIPEWAIT, 1L); // wait for multiplexing! key for perf
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_data_curl_cb);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, dl.get());
  curl_easy_setopt(easy, CURLOPT_PRIVATE, dl.get());
  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  // abort if slower than 1000 bytes/sec during 5 seconds
  curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 5L);
  curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1000L);
  curl_multi_add_handle(curl_multi_handle, easy);
  dl->easy = easy;
}

static void fill_download_queue() {
  while (download_slots.size() != cli.parallel_max && next_index != cli.index_limit) {
    add_download(next_index++);
  }
}

static void process_curl_done_msg(CURLMsg* message, enq_msg_t& msg) {
  CURL* easy_handle = message->easy_handle;

  auto result = message->data.result;

  download* dl = nullptr;
  curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &dl);
  curl_multi_remove_handle(curl_multi_handle, easy_handle);

  if (result == CURLE_OK) {
    curl_easy_cleanup(easy_handle);
    dl->easy = nullptr; // prevent further attempts at cleanup
    auto nh  = download_slots.extract(dl->index);
    logger.log(fmt::format("download {} complete. batching up into message", dl->prefix));
    msg.emplace_back(std::move(nh.mapped())); // batch up to avoid mutex too many times
    return;
  }

  if (dl->retries_left == 0) {
    // hard fail, will eventually terminate whole program
    throw std::runtime_error(fmt::format("prefix '{}': returned result '{}' after {} retries",
                                         dl->prefix, curl_easy_strerror(message->data.result),
                                         download::max_retries));
  }

  dl->retries_left--;
  dl->buffer.clear(); // throw away anything that was returned
  logger.log(fmt::format("prefix '{}': returned result '{}'. {} retries left\n", dl->prefix,
                         curl_easy_strerror(message->data.result), dl->retries_left));
  curl_multi_add_handle(curl_multi_handle, easy_handle); // try again with same handle
}

static void process_curl_messages() {
  CURLMsg* message = nullptr;
  int      pending = 0;

  enq_msg_t msg;
  while ((message = curl_multi_info_read(curl_multi_handle, &pending)) != nullptr) {
    switch (message->msg) {
    case CURLMSG_DONE:
      process_curl_done_msg(message, msg);
      break;

    default:
      logger.log("CURLMSG default");
      break;
    }
  }
  if (!msg.empty()) {
    enqueue_downloads_for_writing(std::move(msg));
  }
  fill_download_queue();
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
    timeval tv{};
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
    event_assign(curl_context->event, ebase, static_cast<evutil_socket_t>(curl_context->sockfd),
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

std::string curl_sync_get(const std::string& url) {
  CURL* curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

  // can't put inliue as setopt is a macro which fails
  auto write_cb = [](char* ptr, size_t size, size_t nmemb, void* body) {
    *static_cast<std::string*>(body) += std::string_view{ptr, size * nmemb};
    return size * nmemb;
  };

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +write_cb); // convert to func ptr
  std::string result_body;
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result_body);
  if (auto res = curl_easy_perform(curl); res != CURLE_OK) {
    curl_easy_cleanup(curl);
    throw std::runtime_error(fmt::format("curl_sync_get: Couldn't retrieve '{}'. Error: {}", url,
                                         curl_easy_strerror(res)));
  }

  curl_easy_cleanup(curl);
  return result_body;
}

void init_curl_and_events() {
  if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
    throw std::runtime_error("Error: Could not init curl\n");
  }

  ebase   = event_base_new();
  timeout = evtimer_new(ebase, timeout_event_cb, nullptr);

  curl_multi_handle = curl_multi_init();
  curl_multi_setopt(curl_multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
  curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETFUNCTION, handle_socket_curl_cb);
  curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERFUNCTION, start_timeout_curl_cb);
}

void run_event_loop(std::size_t start_index) {
  next_index = start_index;
  fill_download_queue();
  event_base_dispatch(ebase);
  finished_downloads();
}

void shutdown_curl_and_events() {
  if (auto res = curl_multi_cleanup(curl_multi_handle); res != CURLM_OK) {
    std::cerr << fmt::format("error: curl_multi_cleanup: '{}'\n", curl_multi_strerror(res));
  }

  event_free(timeout);
  event_base_free(ebase);

  libevent_global_shutdown();
  curl_global_cleanup();
}

void curl_and_event_cleanup() {
  event_base_loopbreak(ebase); // not sure if required, but to be safe

  for (auto& dl_item: download_slots) {
    auto& dl = dl_item.second;
    if (dl->easy != nullptr) {
      if (auto res = curl_multi_remove_handle(curl_multi_handle, dl->easy); res != CURLM_OK) {
        std::cerr << fmt::format("error in curl_multi_remove_handle(): '{}'\n",
                                 curl_multi_strerror(res));
      }
      curl_easy_cleanup(dl->easy);
    }
  }
  download_slots.clear(); // more efficient to clear all at once
  shutdown_curl_and_events();
}
