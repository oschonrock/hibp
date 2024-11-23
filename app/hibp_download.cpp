#include "dnl/queuemgt.hpp"
#include "dnl/resume.hpp"
#include "dnl/shared.hpp"
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

void define_options(CLI::App& app, hibp::dnl::cli_config_t& cli) {

  app.add_option("output_db_filename", cli.output_db_filename,
                 "The file that the downloaded binary database will be written to")
      ->required();

  app.add_flag("--debug", cli.debug,
               "Send verbose thread debug output to stderr. Turns off progress.");
  app.add_flag("--progress,!--no-progress", cli.progress,
               "Show a progress meter on stderr. This is the default.");

  app.add_flag("--resume", cli.resume,
               "Attempt to resume an earlier download. Not with --txt-out. And not with --force.");

  app.add_flag("--ntlm", cli.ntlm, "Download the NTLM format password hashes instead of SHA1.");

  app.add_flag("--txt-out", cli.txt_out,
               "Output text format, rather than the default custom binary format.");

  app.add_flag("--force", cli.force, "Overwrite any existing file! Not with --resume.");

  app.add_option("--parallel-max", cli.parallel_max,
                 "The maximum number of requests that will be started concurrently (default: 300)");

  app.add_option("--limit", cli.index_limit,
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
    if (cli.txt_out && cli.resume) {
      throw std::runtime_error("can't use `--resume` and `--txt-out` together");
    }

    if (cli.force && cli.resume) {
      throw std::runtime_error("can't use `--resume` and `--force` together");
    }

    if (!cli.resume && !cli.force && std::filesystem::exists(cli.output_db_filename)) {
      throw std::runtime_error(fmt::format("File '{}' exists. Use `--force` to overwrite, or "
                                           "`--resume` to resume a previous download.",
                                           cli.output_db_filename));
    }

    std::size_t start_index = 0;
    auto        mode        = cli.txt_out ? std::ios_base::out : std::ios_base::binary;

    if (cli.resume) {
      if (cli.ntlm) {
        start_index = hibp::dnl::get_last_prefix<hibp::pawned_pw_ntlm>(cli.output_db_filename) + 1;
      } else {
        start_index = hibp::dnl::get_last_prefix<hibp::pawned_pw_sha1>(cli.output_db_filename) + 1;
      }

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
    if (cli.txt_out) {
      auto tw = hibp::dnl::text_writer(output_db_stream);
      hibp::dnl::run([&](const std::string& line) { tw.write(line); }, start_index);

    } else {
      // use a largegish output buffer ~240kB for efficient writes
      if (cli.ntlm) {
        auto ffsw = flat_file::stream_writer<hibp::pawned_pw_ntlm>(output_db_stream, 10'000);
        hibp::dnl::run([&](const std::string& line) { ffsw.write(line); }, start_index);
      } else {
        auto ffsw = flat_file::stream_writer<hibp::pawned_pw_sha1>(output_db_stream, 10'000);
        hibp::dnl::run([&](const std::string& line) { ffsw.write(line); }, start_index);
      }
    }

  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
