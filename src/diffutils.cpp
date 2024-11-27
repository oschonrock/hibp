#include "flat_file.hpp"
#include "hibp.hpp"
#include <algorithm>
#include <cstdlib>
#include <fmt/chrono.h> // IWYU pragma: keep
#include <fmt/format.h>
#include <iostream>
#include <stdexcept>

namespace hibp::diffutils {

template <hibp::pw_type PwType>
void run_diff(const std::filesystem::path& old_path, const std::filesystem::path& new_path,
              std::ostream& diff) {
  flat_file::database<PwType> db_old(old_path, (1U << 16U) / sizeof(PwType));
  flat_file::database<PwType> db_new(new_path, (1U << 16U) / sizeof(PwType));

  auto old_begin   = db_old.begin();
  auto new_begin   = db_new.begin();
  auto deep_equals = [](const PwType& a, const PwType& b) { return a == b && a.count == b.count; };
  while (true) {
    auto [diff_iter_old, diff_iter_new] =
        std::mismatch(old_begin, db_old.end(), new_begin, db_new.end(), deep_equals);

    if (diff_iter_old == db_old.end()) {
      // OLD was shorter..
      // copy rest of new into diff as inserts
      while (diff_iter_new != db_new.end()) {
        diff << fmt::format("I:{:08X}:{}\n", diff_iter_old - db_old.begin(),
                            diff_iter_new->to_string());
        ++diff_iter_new;
      }
      break;
    }
    if (diff_iter_new == db_new.end()) { // protect against deref of end()
      throw std::runtime_error("NEW was shorter");
    }
    // fine to dereference both

    if (std::next(diff_iter_old) != db_old.end() &&
        deep_equals(*std::next(diff_iter_old), *diff_iter_new)) {
      throw std::runtime_error("Deletion from OLD");
    }
    // fine to dereference std::next(new)
    if (std::next(diff_iter_new) != db_new.end() &&
        deep_equals(*diff_iter_old, *std::next(diff_iter_new))) {
      diff << fmt::format("I:{:08X}:{}\n", diff_iter_old - db_old.begin(),
                          diff_iter_new->to_string());
      old_begin = diff_iter_old;
      new_begin = diff_iter_new + 1;
      continue;
    }
    if (*diff_iter_old != *diff_iter_new) { // comparing hash only
      throw std::runtime_error("Replacement implies deletion");
    }
    diff << fmt::format("U:{:08X}:{}\n", diff_iter_old - db_old.begin(),
                        diff_iter_new->to_string());
    old_begin = diff_iter_old + 1;
    new_begin = diff_iter_new + 1;
  }
}

template void run_diff<hibp::pawned_pw_sha1>(const std::filesystem::path& old_path,
                                             const std::filesystem::path& new_path,
                                             std::ostream&                diff);
template void run_diff<hibp::pawned_pw_ntlm>(const std::filesystem::path& old_path,
                                             const std::filesystem::path& new_path,
                                             std::ostream&                diff);

} // namespace hibp::diffutils
