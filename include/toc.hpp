#pragma once

#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstdlib>
#include <optional>
#include <string>

namespace hibp {

template <pw_type PwType>
void toc_build(const std::string& db_filename, unsigned bits);

template <pw_type PwType>
std::optional<PwType> toc_search(flat_file::database<PwType>& db, const PwType& needle,
                                 unsigned bits);

} // namespace hibp
