#pragma once

#include "flat_file.hpp"
#include "hibp.hpp"

void run_threads(flat_file::stream_writer<hibp::pawned_pw>& writer);
