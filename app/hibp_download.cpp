#include "download/queuemgt.hpp"
#include "download/shared.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <CLI/CLI.hpp>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <string>

void define_options(CLI::App& app, hibp::dnl::cli_config_t& cli_) {

  app.add_option("output_db_filename", cli_.output_db_filename,
                 "The file that the downloaded binary database will be written to")
      ->required();

  app.add_flag("--debug", cli_.debug,
               "Send verbose thread debug output to stderr. Turns off progress.");
  app.add_flag("--progress,!--no-progress", cli_.progress,
               "Show a progress meter on stderr. This is the default.");

  app.add_flag("--resume", cli_.resume,
               "Attempt to resume an earlier download. Not with --text-out.");

  app.add_flag("--text-out", cli_.text_out,
               "Output text format, rather than the default custom binary format.");

  app.add_flag("--force", cli_.force, "Overwrite any existing file!");

  app.add_option("--parallel-max", cli_.parallel_max,
                 "The maximum number of requests that will be started concurrently (default: 300)");

  app.add_option("--limit", cli_.index_limit,
                 "The maximum number (prefix) files that will be downloaded (default: 100 000 hex "
                 "or 1 048 576 dec)");
}

namespace hibp::dnl {
thread_logger logger;
cli_config_t  cli;
} // namespace hibp::dnl

int main(int argc, char* argv[]) {

  using hibp::dnl::cli;

  CLI::App app;
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  if (cli.debug) cli.progress = false;

  hibp::dnl::logger.debug = cli.debug;

  try {
    if (cli.text_out && cli.resume) {
      throw std::runtime_error("can't use `--resume` and `--text-out` together");
    }

    if (!cli.resume && !cli.force && std::filesystem::exists(cli.output_db_filename)) {
      throw std::runtime_error(fmt::format("File '{}' exists. Use `--force` to overwrite, or "
                                           "`--resume` to resume a previous download.",
                                           cli.output_db_filename));
    }

    std::size_t start_index = 0;
    auto        mode        = cli.text_out ? std::ios_base::out : std::ios_base::binary;

    if (cli.resume) {
      start_index = hibp::dnl::get_last_prefix(cli.output_db_filename) + 1;

      if (cli.index_limit <= start_index) {
        throw std::runtime_error(fmt::format("File '{}' contains {} records already, but you have "
                                             "specified --limit={}. Nothing to do. Aborting.",
                                             cli.output_db_filename, start_index, cli.index_limit));
      }
      mode |= std::ios_base::app;
      std::cerr << fmt::format("Resuming from file {}\n", start_index);
    }

    auto output_db_stream = std::ofstream(cli.output_db_filename, mode);
    if (!output_db_stream) {
      throw std::runtime_error(fmt::format("Error opening '{}' for writing. Because: \"{}\".\n",
                                           cli.output_db_filename,
                                           std::strerror(errno))); // NOLINT errno
    }
    if (cli.text_out) {
      auto tw = hibp::dnl::text_writer(output_db_stream);
      hibp::dnl::run([&](const std::string& line) { tw.write(line); }, start_index);

    } else {
      auto ffsw = flat_file::stream_writer<hibp::pawned_pw>(output_db_stream);
      hibp::dnl::run([&](const std::string& line) { ffsw.write(line); }, start_index);
    }

  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
