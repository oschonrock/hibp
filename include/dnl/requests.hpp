#pragma once

#include <cstddef>
#include <stop_token>
#include <string>

namespace hibp::dnl {

void init_curl_and_events();
void run_event_loop(std::size_t start_index, bool testing_, std::stop_token stoken);
void shutdown_curl_and_events();
void curl_and_event_cleanup();

std::string curl_sync_get(const std::string& url);

} // namespace hibp::dnl
