#pragma once

#include "flat_file.hpp"
#include "hibp.hpp"
#include <chrono>

using clk = std::chrono::high_resolution_clock;
extern clk::time_point start_time; // NOLINT non-const-global

void run_threads(flat_file::stream_writer<hibp::pawned_pw>& writer);
