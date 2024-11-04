#pragma once

#include <string>

void init_curl_and_events();
void shutdown_curl_and_events();
void add_download(const std::string& prefix);

