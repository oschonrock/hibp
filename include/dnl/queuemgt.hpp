#pragma once

#include <cstddef>
#include <functional>
#include <iostream>
#include <string>

namespace hibp::dnl {

// alternative simple text writer

struct text_writer {
  explicit text_writer(std::ostream& os) : os_(os) {}
  void write(const std::string& line) {
    os_.write(line.c_str(), static_cast<std::streamsize>(line.length()));
    os_.write("\n", 1);
  }

private:
  std::ostream& os_; // NOLINT reference
};

// prefer use of std::function (ie stdlib type erasure) rather than templates to keep .hpp interface
// clean
using write_fn_t = std::function<void(const std::string&)>;
void run(write_fn_t write_fn, std::size_t start_index_, bool testing);

} // namespace hibp::dnl
