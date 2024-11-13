#pragma once

#include <string>

void init_curl_and_events();
void run_event_loop(std::size_t start_index);
void shutdown_curl_and_events();
void curl_and_event_cleanup();

std::string curl_sync_get(const std::string& url);

