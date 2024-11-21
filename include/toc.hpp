#pragma once

#include "flat_file.hpp"
#include <cstdlib>
#include <optional>
#include <string>

namespace hibp {

template <typename PwType>
void toc_build(const std::string& db_filename, unsigned bits);

template <typename PwType>
std::optional<PwType> toc_search(flat_file::database<PwType>& db, const PwType& needle,
                                 unsigned bits);

} // namespace hibp
