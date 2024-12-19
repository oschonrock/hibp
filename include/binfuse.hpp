#pragma once

#include "binfuse/sharded_filter.hpp"

namespace hibp {
  
// binfuse types 
template <typename T>
concept binfuse_filter_sink_type = std::is_same_v<T, binfuse::sharded_filter8_sink> || std::is_same_v<T, binfuse::sharded_filter16_sink>;

template <typename T>
concept binfuse_filter_source_type = std::is_same_v<T, binfuse::sharded_filter8_source> || std::is_same_v<T, binfuse::sharded_filter16_source>;

} // namespace hibp
