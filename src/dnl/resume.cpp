#include "dnl/requests.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

namespace hibp::dnl {

// utility function for --resume
template <pw_type PwType>
std::size_t get_last_prefix(const std::string& filename, bool testing) {
  auto filesize = std::filesystem::file_size(filename);
  if (auto tailsize = filesize % sizeof(PwType); tailsize != 0) {
    std::cerr << fmt::format("db_file '{}' size was not a multiple of {}, trimmed off {} bytes.\n",
                             filename, sizeof(PwType), tailsize);
    std::filesystem::resize_file(filename, filesize - tailsize);
  }

  flat_file::database<PwType> db(filename);

  const auto& last_db_ppw = db.back();

  std::stringstream ss;
  ss << last_db_ppw;
  const std::string last_db_hash = ss.str().substr(0, PwType::hash_str_size);
  const std::string prefix       = last_db_hash.substr(0, PwType::prefix_str_size);
  const std::string suffix = last_db_hash.substr(PwType::prefix_str_size, PwType::suffix_str_size);

  const std::string filebody = curl_sync_get(url<PwType>(prefix, testing));

  auto pos_colon = filebody.find_last_of(':');
  if (pos_colon == std::string::npos || pos_colon < PwType::suffix_str_size) {
    throw std::runtime_error(fmt::format("Corrupt last file download with prefix '{}'.", prefix));
  }

  auto last_file_hash =
      filebody.substr(pos_colon - PwType::suffix_str_size, PwType::suffix_str_size);

  if (last_file_hash == suffix) {
    std::size_t last_prefix{};
    std::from_chars(prefix.c_str(), prefix.c_str() + prefix.length(), last_prefix, 16);

    return last_prefix; // last file was completely written, so we can continue with next one
  }

  // more complex resume technique

  std::cerr << fmt::format(
      "Last converted hash not found at end of last retrieved file.\n"
      "Searching backward to hash just before beginning of last retrieved file.\n");

  auto first_file_hash = prefix + filebody.substr(0, PwType::suffix_str_size);
  auto needle          = PwType(first_file_hash);
  auto rbegin          = std::make_reverse_iterator(db.end());
  auto rend            = std::make_reverse_iterator(db.begin());
  auto found_iter      = std::find(rbegin, rend, needle);
  if (found_iter == rend) {
    throw std::runtime_error(
        "Not found at all, sorry you will need to start afresh without `--resume`.\n");
  }

  auto trimmed_file_size = static_cast<std::size_t>(rend - found_iter - 1) * sizeof(PwType);
  std::cerr << fmt::format("found: trimming file to {}.\n", trimmed_file_size);

  std::filesystem::resize_file(filename, trimmed_file_size);
  db = flat_file::database<PwType>{filename}; // reload db

  std::size_t last_prefix{};
  std::from_chars(first_file_hash.c_str(), first_file_hash.c_str() + 5, last_prefix, 16);

  // the last file was incompletely written, so we have trimmed and will do it again
  return last_prefix - 1;
}

// explicit instantiations

template std::size_t get_last_prefix<pawned_pw_sha1>(const std::string& filename, bool testing);
template std::size_t get_last_prefix<pawned_pw_ntlm>(const std::string& filename, bool testing);
template std::size_t get_last_prefix<pawned_pw_sha1t64>(const std::string& filename, bool testing);

} // namespace hibp::dnl
