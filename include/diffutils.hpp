#include "hibp.hpp"
#include <cstdlib>
#include <filesystem>

namespace hibp::diffutils {

template <hibp::pw_type PwType>
void run_diff(const std::filesystem::path& old_path, const std::filesystem::path& new_path,
              std::ostream& diff);

} // namespace hibp::diffutils
