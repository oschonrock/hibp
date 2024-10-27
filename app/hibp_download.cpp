#include <curl/curl.h>
#include <deque>
#include <event2/event.h>
#include <ios>
#include <iostream>
#include <libgen.h>
#include <memory>
#include <sstream>
#include <stdio.h>  // NOLINT
#include <stdlib.h> // NOLINT
#include <string.h> // NOLINT
#include <string>
#include <vector>

struct event_base* base;              // NOLINT
CURLM*             curl_multi_handle; // NOLINT
struct event*      timeout;           // NOLINT

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

struct donwload {
  std::vector<char> buffer;
  std::string prefix;
};

static std::deque<std::unique_ptr<donwload>> queue;

static void add_download(const char* url) {
  auto url_cpy  = std::string(url);
  char* prefix = basename(url_cpy.data());

  FILE* file = fopen(prefix, "wb");
  if (!file) {
    fprintf(stderr, "Error opening %s\n", prefix);
    return;
  }

  CURL* handle = curl_easy_init();
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, file);
  curl_easy_setopt(handle, CURLOPT_PRIVATE, file);
  curl_easy_setopt(handle, CURLOPT_PIPEWAIT, 1L);

  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_multi_add_handle(curl_multi_handle, handle);
  // fprintf(stderr, "Added download %s -> %s\n", url, filename);
}

static void check_multi_info() {
  char*    done_url;
  CURLMsg* message;
  int      pending;
  CURL*    easy_handle;
  FILE*    file;

  while ((message = curl_multi_info_read(curl_multi_handle, &pending))) {
    switch (message->msg) {
    case CURLMSG_DONE:
      /* Do not use message data after calling curl_multi_remove_handle() and
         curl_easy_cleanup(). As per curl_multi_info_read() docs:
         "WARNING: The data the returned pointer points to does not survive
         calling curl_multi_cleanup, curl_multi_remove_handle or
         curl_easy_cleanup." */
      easy_handle = message->easy_handle;
      fprintf(stderr, "CURLMSG_DONE: Result = %d\n", message->data.result);

      curl_easy_getinfo(easy_handle, CURLINFO_EFFECTIVE_URL, &done_url);
      curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &file);
      /* printf("%s DONE\n", done_url); */

      curl_multi_remove_handle(curl_multi_handle, easy_handle);
      curl_easy_cleanup(easy_handle);
      if (file) {
        fclose(file);
      }
      break;

    default:
      fprintf(stderr, "CURLMSG default\n");
      break;
    }
  }
}

static void curl_perform(int /*fd*/, short event, void* arg) {
  int running_handles = 0;
  int flags           = 0;

  if (event & EV_READ) flags |= CURL_CSELECT_IN;
  if (event & EV_WRITE) flags |= CURL_CSELECT_OUT;

  auto* context = static_cast<curl_context_t*>(arg);

  curl_multi_socket_action(curl_multi_handle, context->sockfd, flags, &running_handles);

  check_multi_info();
}

static void on_timeout(evutil_socket_t /*fd*/, short /*events*/, void* /*arg*/) {
  int running_handles = 0;
  curl_multi_socket_action(curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
  check_multi_info();
}

static int start_timeout(CURLM* /*multi*/, long timeout_ms, void* /*userp*/) {
  if (timeout_ms < 0) {
    evtimer_del(timeout);
  } else {
    if (timeout_ms == 0) timeout_ms = 1; /* 0 means call socket_action asap */
    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    evtimer_del(timeout);
    evtimer_add(timeout, &tv);
  }
  return 0;
}

static int handle_socket(CURL* /*easy*/, curl_socket_t s, int action, void* /*userp*/,
                         void* socketp) {
  curl_context_t* curl_context = NULL;
  short           events       = 0;

  switch (action) {
  case CURL_POLL_IN:
  case CURL_POLL_OUT:
  case CURL_POLL_INOUT:
    curl_context = socketp ? static_cast<curl_context_t*>(socketp) : create_curl_context(s);

    curl_multi_assign(curl_multi_handle, s, curl_context);

    if (action != CURL_POLL_IN) events |= EV_WRITE;
    if (action != CURL_POLL_OUT) events |= EV_READ;

    events |= EV_PERSIST;

    event_del(curl_context->event);
    event_assign(curl_context->event, base, curl_context->sockfd, events, curl_perform,
                 curl_context);
    event_add(curl_context->event, NULL);

    break;
  case CURL_POLL_REMOVE:
    if (socketp) {
      event_del((static_cast<curl_context_t*>(socketp))->event);
      destroy_curl_context(static_cast<curl_context_t*>(socketp));
      curl_multi_assign(curl_multi_handle, s, NULL);
    }
    break;
  default:
    abort();
  }

  return 0;
}

int main() {

  std::vector<char> buf{'h', 'e', 'l', 'l', 'o', '\n', 'h', 'e', 'l', 'l', 'o', '2', '\n'};
  
  std::stringstream ss;
  ss.rdbuf()->pubsetbuf(buf.data(), static_cast<std::streamsize>(buf.size()));

  for (std::string line; std::getline(ss, line); ) {
    std::cout << line << "\n";
  }
  return 0;
  
  if (curl_global_init(CURL_GLOBAL_ALL)) {
    fprintf(stderr, "Could not init curl\n");
    return 1;
  }
  
  base    = event_base_new();
  timeout = evtimer_new(base, on_timeout, NULL);

  curl_multi_handle = curl_multi_init();

  // and PIPEWAIT is set to 1L in easy, which forces HTTP2 pipelining
  curl_multi_setopt(curl_multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);

  curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETFUNCTION, handle_socket);
  curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERFUNCTION, start_timeout);

  char url[50];
  for (unsigned i = 0; i != 0x300; i++) {
    snprintf(url, 50, "https://api.pwnedpasswords.com/range/%05X", i);
    add_download(url);
  }

  event_base_dispatch(base);

  curl_multi_cleanup(curl_multi_handle);
  event_free(timeout);
  event_base_free(base);

  libevent_global_shutdown();
  curl_global_cleanup();

  return 0;
}
