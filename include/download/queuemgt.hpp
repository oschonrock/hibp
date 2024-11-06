#pragma once

#include "flat_file.hpp"
#include "hibp.hpp"

void run_threads(flat_file::stream_writer<hibp::pawned_pw>& writer);
std::size_t get_last_prefix();

extern std::size_t start_prefix; // NOLINT non-cost-gobal
extern std::size_t next_prefix;  // NOLINT non-cost-gobal
