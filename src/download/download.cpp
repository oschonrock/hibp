#include "download/download.hpp"
#include "download/queuemgt.hpp"
#include "download/requests.hpp"
#include "download/shared.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <ostream>
#include <string>

std::size_t start_prefix = 0x0UL; // NOLINT non-cost-gobal
std::size_t next_prefix  = 0x0UL; // NOLINT non-cost-gobal

std::size_t get_last_prefix(const std::string& filename) {
  auto filesize = std::filesystem::file_size(filename);
  if (auto tailsize = filesize % sizeof(hibp::pawned_pw); tailsize != 0) {
    std::cerr << std::format("db_file '{}' size was not a multiple of {}, trimmed off {} bytes.\n",
                             filename, sizeof(hibp::pawned_pw), tailsize);
    std::filesystem::resize_file(filename, filesize - tailsize);
  }

  flat_file::database<hibp::pawned_pw> db(filename);

  const auto& last_db_ppw = db.back();

  std::stringstream ss;
  ss << last_db_ppw;
  std::string last_db_hash = ss.str().substr(0, 40);
  std::string prefix       = last_db_hash.substr(0, 5);
  std::string suffix       = last_db_hash.substr(5, 40 - 5);

  std::string filebody = curl_sync_get("https://api.pwnedpasswords.com/range/" + prefix);

  auto pos_colon = filebody.find_last_of(':');
  if (pos_colon == std::string::npos || pos_colon < 35) {
    throw std::runtime_error(std::format("Corrupt last file download with prefix '{}'.", prefix));
  }

  auto last_file_hash = filebody.substr(pos_colon - 35, 35);

  if (last_file_hash == suffix) {
    std::size_t last_prefix{};
    std::from_chars(prefix.c_str(), prefix.c_str() + prefix.length(), last_prefix, 16);

    return last_prefix; // last file was completely written, so we can continue with next one
  }

  // more complex resume technique

  std::cerr << std::format(
      "Last converted hash not found at end of last retrieved file.\n"
      "Searching backward to hash just before beginning of last retrieved file.\n");

  auto first_file_hash = prefix + filebody.substr(0, 35);
  auto needle          = hibp::pawned_pw(first_file_hash);
  auto rbegin          = std::make_reverse_iterator(db.end());
  auto rend            = std::make_reverse_iterator(db.begin());
  auto found_iter      = std::find(rbegin, rend, needle);
  if (found_iter == rend) {
    throw std::runtime_error(
        "Not found at all, sorry you will need to start afresh without `--resume`.\n");
  }

  auto trimmed_file_size =
      static_cast<std::size_t>(rend - found_iter - 1) * sizeof(hibp::pawned_pw);
  std::cerr << std::format("found: trimming file to {}.\n", trimmed_file_size);

  std::filesystem::resize_file(filename, trimmed_file_size);
  db = flat_file::database<hibp::pawned_pw>{filename}; // reload, does this work safely?

  std::size_t last_prefix{};
  std::from_chars(first_file_hash.c_str(), first_file_hash.c_str() + 5, last_prefix, 16);

  // the last file was incompletely written, so we have trimmed and will do it again
  return last_prefix - 1;
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

// provide simple interface to main
// offer 2 writers
// must keep each alive while running

void run_threads_text(std::ostream& output_db_stream, const cli_config_t& cli_) {
  auto       tw         = text_writer(output_db_stream);
  write_fn_t write_func = {[&](const std::string& line) { tw.write(line); }};
  run_threads(write_func, cli_);
}

void run_threads_ff(std::ostream& output_db_stream, const cli_config_t& cli_) {
  auto       ffsw       = flat_file::stream_writer<hibp::pawned_pw>(output_db_stream);
  write_fn_t write_func = {[&](const std::string& line) { ffsw.write(line); }};
  run_threads(write_func, cli_);
}
