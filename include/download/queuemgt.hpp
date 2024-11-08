#pragma once

#include "flat_file.hpp"
#include "hibp.hpp"
#include <ostream>

void        run_threads(flat_file::stream_writer<hibp::pawned_pw>& writer);
std::size_t get_last_prefix(const std::string& filename);

extern std::size_t start_prefix; // NOLINT non-cost-gobal
extern std::size_t next_prefix;  // NOLINT non-cost-gobal

struct text_writer {
  explicit text_writer(std::ostream& os) : os_(os) {}
  void write(const std::string& line) {
    os_.write(line.c_str(), static_cast<std::streamsize>(line.length()));
    os_.write("\n", 1);
  }

private:
  std::ostream& os_; // NOLINT reference
};

template <typename WriterType>
void run_threads(WriterType&& writer);
