#pragma once

#include "hibp.hpp"
#include <cstddef>
#include <string>

namespace hibp::dnl {

template <pw_type PwType>
std::size_t get_last_prefix(const std::string& filename);

} // namespace hibp::dnl
