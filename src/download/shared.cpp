#include "download/shared.hpp"
#include <condition_variable>
#include <curl/curl.h>
#include <unordered_map>

struct event_base* base; // NOLINT non-const-global

// threaded app needs mutex for stdio
std::mutex cerr_mutex; // NOLINT non-const-global
// labels for threads
std::unordered_map<std::thread::id, std::string> thrnames; // NOLINT non-const-global

cli_config_t cli; // NOLINT non-const-global

