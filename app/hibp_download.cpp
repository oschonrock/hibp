#include "CLI/CLI.hpp"
#include "download/queuemgt.hpp"
#include "download/shared.hpp"
#include <cstdlib>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <event2/event.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <stdexcept>

cli_config_t cli_config; // NOLINT non-const-global

void define_options(CLI::App& app) {

  app.add_option("output_db_filename", cli_config.output_db_filename,
                 "The file that the downloaded binary database will be written to")
      ->required();

  app.add_flag("--debug", cli_config.debug,
               "Send verbose thread debug output to stderr. Turns off progress.");
  app.add_flag("--progress,!--no-progress", cli_config.progress,
               "Show a progress meter on stderr. This is the default.");

  app.add_flag("--resume", cli_config.resume,
               "Attempt to resume an earlier download. Not with --text-out.");
  app.add_flag("--text-out", cli_config.text_out,
               "Output text format, rather than the default custom binary format.");
  app.add_flag("--force", cli_config.force, "Overwrite any existing file!");
  app.add_option("--parallel-max", cli_config.parallel_max,
                 "The maximum number of requests that will be started concurrently (default: 300)");

  app.add_option("--limit", cli_config.prefix_limit,
                 "The maximum number (prefix) files that will be downloaded (default: 0x100000)");
}

int main(int argc, char* argv[]) {
  CLI::App app;
  define_options(app);
  CLI11_PARSE(app, argc, argv);

  if (cli_config.debug) cli_config.progress = false;

  try {
    if (cli_config.text_out && cli_config.resume) {
      throw std::runtime_error("can't use `--resume` and `--text-out` together");
    }

    if (!cli_config.resume && !cli_config.force &&
        std::filesystem::exists(cli_config.output_db_filename)) {
      throw std::runtime_error(std::format("File '{}' exists. Use `--force` to overwrite, or "
                                           "`--resume` to resume a previous download.",
                                           cli_config.output_db_filename));
    }

    auto mode = std::ios_base::binary;

    if (cli_config.resume) {
      next_prefix = get_last_prefix() + 1;
      if (cli_config.prefix_limit <= next_prefix) {
        throw std::runtime_error(std::format("File '{}' contains {} records already, but you have "
                                             "specified --limit={}. Nothing to do. Aborting.",
                                             cli_config.output_db_filename,
                                             next_prefix,
                                             cli_config.prefix_limit));
      }
      start_prefix = next_prefix; // to make progress correct
      mode |= std::ios_base::app;
      std::cerr << std::format("Resuming from file {}\n", start_prefix);
    }

    auto output_db_stream = std::ofstream(cli_config.output_db_filename, mode);
    if (!output_db_stream) {
      throw std::runtime_error(std::format("Error opening '{}' for writing. Because: \"{}\".\n",
                                           cli_config.output_db_filename,
                                           std::strerror(errno))); // NOLINT errno
    }
    auto writer = flat_file::stream_writer<hibp::pawned_pw>(output_db_stream);

    run_threads(writer);

  } catch (const std::exception& e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
