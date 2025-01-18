#include "bytearray_cast.hpp"
#include "binfuse/sharded_filter.hpp"
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
               "Attempt to resume an earlier download. Not with --txt-out or --binfuse(9|16)-out. "
               "And not with --force.");

  app.add_flag("--ntlm", cli.ntlm, "Download the NTLM format password hashes instead of SHA1.");

  app.add_flag("--sha1t64", cli.sha1t64,
               "Download the sha1 format password hashes, but truncate them to 64bits in binary "
               "output format.");

  app.add_flag("--txt-out", cli.txt_out,
               "Output text format, rather than the default custom binary format.");

  app.add_flag("--binfuse8-out", cli.binfuse8_out,
               "Output a binary_fuse8 filter, for space saving probabilistic queries.");

  app.add_flag("--binfuse16-out", cli.binfuse16_out,
               "Output a binary_fuse16 filter, for space saving probabilistic queries.");

  app.add_flag("--force", cli.force, "Overwrite any existing file! Not with --resume.");

  app.add_option("--parallel-max", cli.parallel_max,
                 "The maximum number of requests that will be started concurrently (default: 300)");

  app.add_option("--limit", cli.index_limit,
                 "The maximum number (prefix) files that will be downloaded (default: 100 000 hex "
                 "or 1 048 576 dec)");

  app.add_flag("--testing", cli.testing,
               "Download from a local test server instead of public api.");
}

template <hibp::pw_type PwType>
void launch_bin_db(std::ofstream& output_db_stream, const hibp::dnl::cli_config_t& cli,
                   std::size_t start_index) {
  // use a largegish output buffer ~240kB for efficient writes
  // keep stream instance alive here
  auto ffsw = flat_file::stream_writer<PwType>(output_db_stream, 10'000);
  hibp::dnl::run([&](const std::string& line) { ffsw.write(PwType{line}); }, start_index,
                 cli.testing);
}

template <hibp::pw_type PwType>
std::size_t compute_start_index(const hibp::dnl::cli_config_t& cli) {
  return hibp::dnl::get_last_prefix<PwType>(cli.output_db_filename, cli.testing) + 1;
}

std::size_t get_start_index(const hibp::dnl::cli_config_t& cli) {
  std::size_t start_index = 0;
  if (cli.resume) {
    if (cli.ntlm) {
      start_index = compute_start_index<hibp::pawned_pw_ntlm>(cli);
    } else if (cli.sha1t64) {
      start_index = compute_start_index<hibp::pawned_pw_sha1t64>(cli);
    } else {
      start_index = compute_start_index<hibp::pawned_pw_sha1>(cli);
    }

    if (cli.index_limit <= start_index) {
      throw std::runtime_error(fmt::format("File '{}' contains {} records already, but you have "
                                           "specified --limit={}. Nothing to do. Aborting.",
                                           cli.output_db_filename, start_index, cli.index_limit));
    }
    std::cerr << fmt::format("Resuming from file {}\n", start_index);
  }
  return start_index;
}

void launch_stream(const hibp::dnl::cli_config_t& cli) {
  std::size_t             start_index = get_start_index(cli);

  std::ios_base::openmode mode        = cli.txt_out ? std::ios_base::out : std::ios_base::binary;
  if (cli.resume) {
    mode |= std::ios_base::app;
  }
  
  auto output_db_stream = std::ofstream(cli.output_db_filename, mode);
  if (!output_db_stream) {
    throw std::runtime_error(fmt::format("Error opening '{}' for writing. Because: \"{}\".",
                                         cli.output_db_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  
  if (cli.txt_out) {
    auto tw = hibp::dnl::text_writer(output_db_stream);
    hibp::dnl::run([&](const std::string& line) { tw.write(line); }, start_index, cli.testing);
  } else {
    if (cli.ntlm) {
      launch_bin_db<hibp::pawned_pw_ntlm>(output_db_stream, cli, start_index);
    } else if (cli.sha1t64) {
      launch_bin_db<hibp::pawned_pw_sha1t64>(output_db_stream, cli, start_index);
    } else {
      launch_bin_db<hibp::pawned_pw_sha1>(output_db_stream, cli, start_index);
    }
  }
}

template <typename ShardedFilterType>
void launch_filter(const hibp::dnl::cli_config_t& cli) {
  if (std::filesystem::exists(cli.output_db_filename) && cli.force) {
    // there can be no --resume of any sort, user suplied force, start from scratch
    std::filesystem::remove(cli.output_db_filename);
  }
  ShardedFilterType filter(cli.output_db_filename);
  filter.stream_prepare();
  hibp::dnl::run(
      [&](const std::string& line) {
        auto pw = hibp::pawned_pw_sha1{line};
        filter.stream_add(hibp::bytearray_cast<std::uint64_t>(pw.hash.data()));
      },
      0, cli.testing); // always start at 0
  filter.stream_finalize();
}

void check_options(const hibp::dnl::cli_config_t& cli) {
  if (cli.txt_out && cli.resume) {
    throw std::runtime_error("can't use `--resume` and `--txt-out` together");
  }

  if ((cli.binfuse8_out || cli.binfuse16_out) && cli.resume) {
    throw std::runtime_error("can't use `--resume` on binfuse filters");
  }

  if ((cli.binfuse8_out || cli.binfuse16_out) && (cli.txt_out || cli.ntlm || cli.sha1t64)) {
    throw std::runtime_error("can't use `--binfuse(8|16)-out` with a hash format selector");
  }

  if (cli.force && cli.resume) {
    throw std::runtime_error("can't use `--resume` and `--force` together");
  }

  if (cli.ntlm && cli.sha1t64) {
    throw std::runtime_error("can't use `--ntlm` and `--sha1t64` together");
  }

  if (!cli.resume && !cli.force && std::filesystem::exists(cli.output_db_filename)) {
    throw std::runtime_error(fmt::format("File '{}' exists. Use `--force` to overwrite, or "
                                         "`--resume` to resume a previous download.",
                                         cli.output_db_filename));
  }
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
    check_options(cli);

    if (cli.binfuse8_out || cli.binfuse16_out) {
      if (cli.binfuse8_out) {
        launch_filter<binfuse::sharded_filter8_sink>(cli);
      } else {
        launch_filter<binfuse::sharded_filter16_sink>(cli);
      }
    } else {
      launch_stream(cli);
    }
  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
