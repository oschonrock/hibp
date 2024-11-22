#pragma once

#include <cstdint>
#include <string>
#include <thread>

namespace hibp::srv {

struct cli_config_t {
  std::string   sha1_db_filename;
  std::string   ntlm_db_filename;
  std::string   bind_address = "localhost";
  std::uint16_t port         = 8082;
  unsigned int  threads      = std::thread::hardware_concurrency();
  bool          json         = false;
  bool          perf_test    = false;
  bool          toc          = false;
  unsigned      toc_bits     = 20; // 1Mega chapters
};

extern cli_config_t cli;

void run_server();

} // namespace hibp::srv
