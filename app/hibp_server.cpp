#include "flat_file.hpp"
#include "hibp.hpp"
#include "toc.hpp"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <restinio/http_headers.hpp>
#include <restinio/http_server_run.hpp>
#include <restinio/router/express.hpp>
#include <restinio/traits.hpp>
#include <sha1.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

struct cli_config_t {
  std::string   db_filename;
  std::string   bind_address = "localhost";
  std::uint16_t port         = 8082;
  unsigned int  threads      = std::thread::hardware_concurrency();
  bool          json         = false;
  bool          perf_test    = false;
  bool          toc          = false;
  std::size_t   toc_entries  = 1U << 16U; // 64k chapters
  bool          toc2         = false;
  unsigned      toc2_bits    = 20; // 1Mega chapters
};


void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("db_filename", cli.db_filename,
                 "The file that contains the binary database you downloaded")
      ->required();

  app.add_option(
      "--bind-address", cli.bind_address,
      fmt::format("The IP4 address the server will bind to. (default: {})", cli.bind_address));

  app.add_option("--port", cli.port,
                 fmt::format("The port the server will bind to (default: {})", cli.port));

  app.add_option("--threads", cli.threads,
                 fmt::format("The number of threads to use (default: {})", cli.threads))
      ->check(CLI::Range(1U, cli.threads));

  app.add_flag("--json", cli.json, "Output a json response.");

  app.add_flag(
      "--perf-test", cli.perf_test,
      "Use this to uniquefy the password provided for each query, "
      "thereby defeating the cache. The results will be wrong, but good for performance tests");

  app.add_flag("--toc", cli.toc, "Use a table of contents for extra performance.");

  app.add_flag("--toc2", cli.toc2, "Use a table of contents for extra performance.");

  app.add_option(
      "--toc-entries", cli.toc_entries,
      fmt::format("Specify how may table of contents entries to use. default {}", cli.toc_entries));
}

namespace {
cli_config_t cli;
} // namespace

auto search_and_respond(flat_file::database<hibp::pawned_pw>& db, const hibp::pawned_pw& needle,
                        auto req) {
  std::optional<hibp::pawned_pw> maybe_ppw;

  if (cli.toc) {
    maybe_ppw = toc_search(db, needle);
  } else if (cli.toc2) {
    maybe_ppw = toc2_search(db, needle, cli.toc2_bits);
  } else if (auto iter = std::lower_bound(db.begin(), db.end(), needle);
             iter != db.end() && *iter == needle) {
    maybe_ppw = *iter;
  }

  int count = maybe_ppw ? maybe_ppw->count : -1;

  std::string content_type = cli.json ? "application/json" : "text/plain";

  auto response = req->create_response().append_header(
      restinio::http_field::content_type, fmt::format("{}; charset=utf-8", content_type));

  if (cli.json) {
    response.set_body(fmt::format("{{count:{}}}\n", count));
  } else {
    response.set_body(fmt::format("{}\n", count));
  }
  return response.done();
}

auto get_router(const std::string& db_filename) {
  auto router = std::make_unique<restinio::router::express_router_t<>>();
  router->http_get(R"(/check/:format/:password)", [&](auto req, auto params) {
    // one db object (ie set of buffers and pointers) per thread
    thread_local flat_file::database<hibp::pawned_pw> db(db_filename,
                                                         4096 / sizeof(hibp::pawned_pw));

    static std::atomic<int> uniq{}; // for performance testing, make uniq_pw for each request

    if (params["format"] != "plain" && params["format"] != "sha1") {
      return req->create_response(restinio::status_not_found()).connection_close().done();
    }

    std::string sha1_txt_pw;
    if (params["format"] == "plain") {
      SHA1 sha1;
      auto pw = std::string(params["password"]);
      if (cli.perf_test) {
        pw += std::to_string(uniq++);
      }
      sha1_txt_pw = sha1(pw);
    } else {
      if (params["password"].size() != 40 ||
          params["password"].find_first_not_of("0123456789ABCDEF") != std::string_view::npos) {
        return req->create_response(restinio::status_bad_request()).connection_close().done();
      }
      sha1_txt_pw = std::string{params["password"]};
    }
    const hibp::pawned_pw needle = hibp::convert_to_binary(sha1_txt_pw);

    return search_and_respond(db, needle, req);
  });

  router->non_matched_request_handler([](auto req) {
    return req->create_response(restinio::status_not_found()).connection_close().done();
  });

  return router;
}

void run_server() {
  // Launching a server with custom traits.
  struct my_server_traits : public restinio::default_traits_t {
    using request_handler_t = restinio::router::express_router_t<>;
  };

  std::cerr << fmt::format("Serving from {0}:{1}\n"
                           "Make a request to either of:\n"
                           "http://{0}:{1}/check/plain/password123\n"
                           "http://{0}:{1}/check/sha1/CBFDAC6008F9CAB4083784CBD1874F76618D2A97\n",
                           cli.bind_address, cli.port);

  auto settings = restinio::on_thread_pool<my_server_traits>(cli.threads)
                      .address(cli.bind_address)
                      .port(cli.port)
                      .request_handler(get_router(cli.db_filename));

  restinio::run(std::move(settings));
}

int main(int argc, char* argv[]) {
  CLI::App app("Have I been pawned Server");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    if (cli.toc) {
      build_toc(cli.db_filename, cli.toc_entries);
    } else if (cli.toc2) {
      build_toc2(cli.db_filename, cli.toc2_bits);
    } else {
      auto input_stream = std::ifstream(cli.db_filename);
      if (!input_stream) {
        throw std::runtime_error(fmt::format("Error opening '{}' for reading. Because: \"{}\".\n",
                                             cli.db_filename,
                                             std::strerror(errno))); // NOLINT errno
      }
      // let stream die, was just to test because we branch into threads, and open it there N times
    }

    run_server();
  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
