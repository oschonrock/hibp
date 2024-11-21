#include "dnl/shared.hpp"
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace hibp::dnl {

std::mutex cerr_mutex; // threaded app needs mutex for stdio

std::unordered_map<std::thread::id, std::string> thrnames; // labels for threads

} // namespace hibp::dnl
