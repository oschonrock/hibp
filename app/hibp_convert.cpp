#include "CLI/CLI.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <stdexcept>

struct cli_config_t {
  std::string output_filename;
  std::string input_filename;
  bool        force    = false;
  bool        text_out = false;
  bool        bin_out  = false;
  std::size_t limit    = -1; // ie max
};

void define_options(CLI::App& app, cli_config_t& cli_config) {

  app.add_option("input_filename", cli_config.input_filename,
                 "The file that the downloaded binary database will be written to")
      ->required();

  app.add_option("output_filename", cli_config.output_filename,
                 "The file that the downloaded binary database will be written to")
      ->required();

  app.add_flag("--text-out", cli_config.text_out,
               "Output text format. Choose one of --bin-out and --text-out");

  app.add_flag("--bin-out", cli_config.bin_out,
               "Output binary format. Choose one of --bin-out and --text-out");

  app.add_option("--limit", cli_config.limit,
                 "The maximum number of records that will be converted (default: all)");

  app.add_flag("--force", cli_config.force, "Overwrite any existing file!");
}

std::ifstream get_input_stream(const std::string& input_filename) {
  auto input_stream = std::ifstream(input_filename);
  if (!input_stream) {
    throw std::runtime_error(std::format("Error opening '{}' for reading. Because: \"{}\".\n",
                                         input_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return input_stream;
}

std::ofstream get_output_stream(const std::string& output_filename, bool force) {
  if (!force && std::filesystem::exists(output_filename)) {
    throw std::runtime_error(
        std::format("File '{}' exists. Use `--force` to overwrite.", output_filename));
  }

  auto output_stream = std::ofstream(output_filename, std::ios_base::binary);
  if (!output_stream) {
    throw std::runtime_error(std::format("Error opening '{}' for writing. Because: \"{}\".\n",
                                         output_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return output_stream;
}

void convert_to_binary(const std::string& input_filename, const std::string& output_filename,
                       bool force, std::size_t limit) {
  std::cerr << std::format("Reading `have i been pawned` text database from {} "
                           "converting to binary format and writing to {}.",
                           input_filename, output_filename);

  auto output_stream = get_output_stream(output_filename, force);

  auto writer = flat_file::stream_writer<hibp::pawned_pw>(output_stream);

  auto input_stream = get_input_stream(input_filename);

  std::size_t count = 0;
  for (std::string line; std::getline(input_stream, line) && count != limit; count++) {
    writer.write(hibp::convert_to_binary(line));
  }
}

void convert_to_text(const std::string& input_filename, const std::string& output_filename,
                     bool force, std::size_t limit) {
  std::cerr << std::format("Reading `have i been pawned` binary database from {} "
                           "converting to text format and writing to {}.\n",
                           input_filename, output_filename);

  auto output_stream = get_output_stream(output_filename, force);

  flat_file::database<hibp::pawned_pw> db{input_filename, 4096 / sizeof(hibp::pawned_pw)};

  std::size_t count = 0;
  for (const auto& record: db) {
    auto buf = record.to_string();
    output_stream << record << '\n';
    count++;
    if (count == limit) break;
  }
}

int main(int argc, char* argv[]) {
  cli_config_t cli; // NOLINT non-const global

  CLI::App app;
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    if ((cli.text_out && cli.bin_out) || (!cli.text_out && !cli.bin_out)) {
      throw std::runtime_error(
          "Please use exactly one of --text-out and --bin-out, not both, and not neither.");
    }

    if (cli.bin_out) {
      convert_to_binary(cli.input_filename, cli.output_filename, cli.force, cli.limit);
    } else if (cli.text_out) {
      convert_to_text(cli.input_filename, cli.output_filename, cli.force, cli.limit);
    }

  } catch (const std::exception& e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
