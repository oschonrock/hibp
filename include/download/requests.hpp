#pragma once

#include <cstddef>
#include <string>

namespace hibp::dnl {
  
void init_curl_and_events();
void run_event_loop(std::size_t start_index);
void shutdown_curl_and_events();
void curl_and_event_cleanup();

std::string curl_sync_get(const std::string& url);

} // namespace hibp::dnl
