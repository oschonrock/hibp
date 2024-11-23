#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace hibp {

std::array<std::byte, 16> ntlm(const std::string& password);

} // namespace hibp
