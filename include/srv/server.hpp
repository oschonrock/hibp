#pragma once

#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstdint>
#include <string>
#include <thread>

namespace hibp::srv {

struct cli_config_t {
  std::string   db_filename;
  std::string   bind_address = "localhost";
  std::uint16_t port         = 8082;
  unsigned int  threads      = std::thread::hardware_concurrency();
  bool          json         = false;
  bool          perf_test    = false;
  bool          toc          = false;
  unsigned      toc_bits     = 20; // 1Mega chapters
};

extern cli_config_t cli;

auto search_and_respond(flat_file::database<hibp::pawned_pw>& db, const hibp::pawned_pw& needle,
                        auto req);
auto get_router(const std::string& db_filename);
void run_server();

} // namespace hibp::srv
