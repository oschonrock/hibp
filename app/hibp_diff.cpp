#include "flat_file.hpp"
#include "hibp.hpp"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fmt/chrono.h> // IWYU pragma: keep
#include <fmt/format.h>
#include <iostream>
#include <stdexcept>
#include <string>

struct cli_config_t {
  std::string db_file_old;
  std::string db_file_new;
  bool        ntlm = false;
};

void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("db_file_old", cli.db_file_old,
                 "The file that contains 'A' binary database to compare")
      ->required();

  app.add_option("db_file_new", cli.db_file_new,
                 "The file that contains 'B' binary database to compare")
      ->required();

  app.add_flag("--ntlm", cli.ntlm, "Use ntlm hashes rather than sha1.");
}

template <hibp::pw_type PwType>
void run_diff(const cli_config_t& cli) {
  flat_file::database<PwType> db_old(cli.db_file_old, (1U << 16U) / sizeof(PwType));
  flat_file::database<PwType> db_new(cli.db_file_new, (1U << 16U) / sizeof(PwType));

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
        std::cout << fmt::format("I:{:08X}:{}\n", diff_iter_old - db_old.begin(),
                                 diff_iter_new->to_string());
        ++diff_iter_new;
      }
      break;
    }
    if (diff_iter_new == db_new.end()) {
      throw std::runtime_error("NEW was shorter. This shouldn't happen!");
    }
    // fine to dereference both

    if (std::next(diff_iter_old) == db_old.end()) {
      throw std::runtime_error("Reached last in OLD, but != new. Implies deletion in OLD.");
    }
    // fine to dereference std::next(old)
    if (deep_equals(*std::next(diff_iter_old), *diff_iter_new)) {
      throw std::runtime_error("Deletion from OLD. This shouldn't happen!");
    }
    if (std::next(diff_iter_new) == db_new.end()) {
      throw std::runtime_error("Reached last in NEW, but old != new. Implies deletion in OLD.");
    }
    // fine to dereference std::next(new)
    if (deep_equals(*diff_iter_old, *(diff_iter_new + 1))) {
      std::cout << fmt::format("I:{:08X}:{}\n", diff_iter_old - db_old.begin(),
                               diff_iter_new->to_string());
      old_begin = diff_iter_old;
      new_begin = diff_iter_new + 1;
      continue;
    }
    if (*diff_iter_old != *diff_iter_new) { // comparing hash only
      throw std::runtime_error("An update which is not just count. "
                               "Shouldn't happen, as it implies deletion from OLD.");
    }
    std::cout << fmt::format("U:{:08X}:{}\n", diff_iter_old - db_old.begin(),
                             diff_iter_new->to_string());
    old_begin = diff_iter_old + 1;
    new_begin = diff_iter_new + 1;
  }
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app;
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    if (cli.ntlm) {
      run_diff<hibp::pawned_pw_ntlm>(cli);
    } else {
      run_diff<hibp::pawned_pw_sha1>(cli);
    }
  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
