#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>

void build_toc(const std::string& db_filename, std::size_t toc_entries);

void build_toc2(const std::string& db_filename, unsigned bits);

std::optional<hibp::pawned_pw> toc_search(flat_file::database<hibp::pawned_pw>& db,
                                          const hibp::pawned_pw&                needle);

std::optional<hibp::pawned_pw> toc2_search(flat_file::database<hibp::pawned_pw>& db,
                                           const hibp::pawned_pw& needle, unsigned bits);
