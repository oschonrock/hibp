#include "flat_file.hpp"
#include "hibp.hpp"
#include <CLI/CLI.hpp>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <ios>
#include <iostream>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>

struct cli_config_t {
  std::string output_filename;
  std::string input_filename;
  bool        force           = false;
  bool        standard_output = false;
  bool        standard_input  = false;
  bool        bin_to_txt      = false;
  bool        txt_to_bin      = false;
  std::size_t limit           = -1; // ie max
};

void define_options(CLI::App& app, cli_config_t& cli_config) {

  app.add_flag("--txt-to-bin", cli_config.txt_to_bin,
               "From text to binary format. Choose either --txt-to-bin or --bin-to-txt");

  app.add_flag("--bin-to-txt", cli_config.bin_to_txt,
               "From binary to text format. Choose either --txt-to-bin or --bin-to-txt");

  app.add_option("-i,--input", cli_config.input_filename,
                 "The file that the downloaded binary database will be read from");

  app.add_flag("--stdin", cli_config.standard_input,
               "Instead of an input file read input from standard_input. Only for text input.");

  app.add_option("-o,--output", cli_config.output_filename,
                 "The file that the downloaded binary database will be written to");

  app.add_flag("--stdout", cli_config.standard_output,
               "Instead of an output file write output to standard output.");

  app.add_option("-l,--limit", cli_config.limit,
                 "The maximum number of records that will be converted (default: all)");

  app.add_flag("-f,--force", cli_config.force, "Overwrite any existing output file!");
}

std::ifstream get_input_stream(const std::string& input_filename) {
  auto input_stream = std::ifstream(input_filename);
  if (!input_stream) {
    throw std::runtime_error(fmt::format("Error opening '{}' for reading. Because: \"{}\".\n",
                                         input_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return input_stream;
}

std::ofstream get_output_stream(const std::string& output_filename, bool force) {
  if (!force && std::filesystem::exists(output_filename)) {
    throw std::runtime_error(
        fmt::format("File '{}' exists. Use `--force` to overwrite.", output_filename));
  }

  auto output_stream = std::ofstream(output_filename, std::ios_base::binary);
  if (!output_stream) {
    throw std::runtime_error(fmt::format("Error opening '{}' for writing. Because: \"{}\".\n",
                                         output_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return output_stream;
}

void txt_to_bin(std::istream& input_stream, std::ostream& output_stream, std::size_t limit) {

  auto writer = flat_file::stream_writer<hibp::pawned_pw>(output_stream);

  std::size_t count = 0;
  for (std::string line; std::getline(input_stream, line) && count != limit; count++) {
    writer.write(hibp::convert_to_binary(line));
  }
}

void bin_to_txt(const std::string& input_filename, std::ostream& output_stream, std::size_t limit) {

  flat_file::database<hibp::pawned_pw> db{input_filename, 4096 / sizeof(hibp::pawned_pw)};

  std::size_t count = 0;
  for (const auto& record: db) {
    output_stream << record << '\n';
    count++;
    if (count == limit) break;
  }
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app("Converting 'Have I been pawned' databases between text and binary formats");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    if ((cli.bin_to_txt && cli.txt_to_bin) || (!cli.bin_to_txt && !cli.txt_to_bin)) {
      throw std::runtime_error(
          "Please use exactly one of --bin-to-txt and --txt-to-bin, not both, and not neither.");
    }

    if ((!cli.input_filename.empty() && cli.standard_input) ||
        (cli.input_filename.empty() && !cli.standard_input)) {
      throw std::runtime_error(
          "Please use exactly one of -i|--input and --stdin, not both, and not neither.");
    }

    if (cli.bin_to_txt && cli.standard_input) {
      throw std::runtime_error(
          "Sorry, cannot read binary database from standard_input. Please use a file.");
    }

    if ((!cli.output_filename.empty() && cli.standard_output) ||
        (cli.output_filename.empty() && !cli.standard_output)) {
      throw std::runtime_error("Please use exactly one of -o|--output and --stdout, not "
                               "both, and not neither.");
    }

    std::istream* input_stream      = &std::cin;
    std::string   input_stream_name = "standard_input";
    std::ifstream ifs;
    if (!cli.standard_input) {
      ifs               = get_input_stream(cli.input_filename);
      input_stream      = &ifs;
      input_stream_name = cli.input_filename;
    }

    std::ostream* output_stream      = &std::cout;
    std::string   output_stream_name = "standard_output";
    std::ofstream ofs;
    if (!cli.standard_output) {
      ofs                = get_output_stream(cli.output_filename, cli.force);
      output_stream      = &ofs;
      output_stream_name = cli.output_filename;
    }

    if (cli.txt_to_bin) {
      std::cerr << fmt::format("Reading `have i been pawned` text database from {}, "
                               "converting to binary format and writing to {}.\n",
                               input_stream_name, output_stream_name);

      txt_to_bin(*input_stream, *output_stream, cli.limit);
    } else if (cli.bin_to_txt) {

      std::cerr << fmt::format("Reading `have i been pawned` binary database from {}, "
                               "converting to text format and writing to {}.\n",
                               input_stream_name, output_stream_name);

      bin_to_txt(cli.input_filename, *output_stream, cli.limit);
    }
  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
