#include "diffutils.hpp"
#include "hibp.hpp"
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>
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

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app;
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    if (cli.ntlm) {
      hibp::diffutils::run_diff<hibp::pawned_pw_ntlm>(cli.db_file_old, cli.db_file_new, std::cout);
    } else {
      hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(cli.db_file_old, cli.db_file_new, std::cout);
    }
  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
