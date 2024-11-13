#pragma once

#include <string>

void init_curl_and_events();
void shutdown_curl_and_events();
void curl_and_event_cleanup();

void fill_download_queue();
std::string curl_sync_get(const std::string& url);

