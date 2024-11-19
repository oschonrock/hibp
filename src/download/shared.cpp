#include "download/shared.hpp"
#include <curl/curl.h>
#include <unordered_map>

std::mutex cerr_mutex; // threaded app needs mutex for stdio

std::unordered_map<std::thread::id, std::string> thrnames; // labels for threads
