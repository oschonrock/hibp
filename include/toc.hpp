#pragma once

#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstdlib>
#include <optional>
#include <string>

namespace hibp {

void toc_build(const std::string& db_filename, unsigned bits);

std::optional<hibp::pawned_pw> toc_search(flat_file::database<hibp::pawned_pw>& db,
                                          const hibp::pawned_pw& needle, unsigned bits);

} // namespace hibp
