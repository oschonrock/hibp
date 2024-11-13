#include "download/shared.hpp"
#include <curl/curl.h>
#include <unordered_map>

// threaded app needs mutex for stdio
std::mutex cerr_mutex; // NOLINT non-const-global
// labels for threads
std::unordered_map<std::thread::id, std::string> thrnames; // NOLINT non-const-global

