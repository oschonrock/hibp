#include "flat_file.hpp"
#include "download/queuemgt.hpp"
#include "download/requests.hpp"
#include "hibp.hpp"
#include <functional>
#include <ostream>
#include <string>

std::size_t start_prefix = 0x0UL; // NOLINT non-cost-gobal
std::size_t next_prefix  = 0x0UL; // NOLINT non-cost-gobal

std::size_t get_last_prefix(const std::string& filename) {
  flat_file::database<hibp::pawned_pw> db(filename);

  const auto& last = db.back();

  std::stringstream ss;
  ss << last;
  std::string last_hash = ss.str().substr(0, 40);
  std::string prefix    = last_hash.substr(0, 5);
  std::string suffix    = last_hash.substr(5, 40 - 5);

  std::string result_body = curl_sync_get("https://api.pwnedpasswords.com/range/" + prefix);

  auto pos = result_body.find_last_of(':');
  if (pos == std::string::npos || pos < 35 || suffix != result_body.substr(pos - 35, 35)) {
    throw std::runtime_error("Last converted hash not found at end of last retrieved file. Cannot "
                             "resume. You need to start afresh without `--resume`. Sorry.\n");
  }
  std::size_t last_prefix{};
  std::from_chars(prefix.c_str(), prefix.c_str() + prefix.length(), last_prefix,
                  16); // known to be correct

  return last_prefix;
}

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

// provide simple interface to main
// offer 2 writers
// must keep each alive while running

void run_threads_text(std::ostream& output_db_stream) {
  auto       tw         = text_writer(output_db_stream);
  write_fn_t write_func = {[&](const std::string& line) { tw.write(line); }};
  run_threads(write_func);
}

void run_threads_ff(std::ostream& output_db_stream) {
  auto       ffsw       = flat_file::stream_writer<hibp::pawned_pw>(output_db_stream);
  write_fn_t write_func = {[&](const std::string& line) { ffsw.write(line); }};
  run_threads(write_func);
}
