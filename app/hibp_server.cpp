#include "CLI/CLI.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "sha1.h"
#include "toc.hpp"
#include <cstdint>
#include <cstdlib>
#include <restinio/core.hpp>

struct cli_config_t {
  std::string   db_filename;
  std::string   bind_address = "localhost";
  std::uint16_t port         = 8082;
  unsigned int  threads      = std::thread::hardware_concurrency();
  bool          json         = false;
  bool          perf_test    = false;
  bool          toc          = false;
  std::size_t   toc_entries  = 1U << 16U; // 64k chapters
};

static cli_config_t cli_config; // NOLINT non-const global

static std::atomic<int> uniq{}; // NOLINT for performance testing, uniq across threads

//
void define_options(CLI::App& app) {

  app.add_option("db_filename", cli_config.db_filename,
                 "The file that contains the binary database you downloaded")
      ->required();

  app.add_option("--bind-address", cli_config.bind_address,
                 std::format("The IP4 address the server will bind to. (default: {})",
                             cli_config.bind_address));

  app.add_option("--port", cli_config.port,
                 std::format("The port the server will bind to (default: {})", cli_config.port));

  app.add_option("--threads", cli_config.threads,
                 std::format("The number of threads to use (default: {})", cli_config.threads))
      ->check(CLI::Range(1U, cli_config.threads));

  app.add_flag("--json", cli_config.json, "Output a json response.");

  app.add_flag(
      "--perf-test", cli_config.perf_test,
      "Use this to uniquefy the password provided for each query, "
      "thereby defeating the cache. The results will be wrong, but good for performance tests");

  app.add_flag("--toc", cli_config.toc, "Use a table of contents for extra performance.");

  app.add_option("--toc-entries", cli_config.toc_entries,
                 std::format("Specify how may table of contents entries to use. default {}",
                             cli_config.toc_entries));
}

auto get_router(const std::string& db_filename) {
  auto router = std::make_unique<restinio::router::express_router_t<>>();
  router->http_get(R"(/:password)", [&](auto req, auto params) {
    // one db object (ie set of buffers and pointers) per thread
    thread_local flat_file::database<hibp::pawned_pw> db(db_filename,
                                                         4096 / sizeof(hibp::pawned_pw));

    SHA1 sha1;
    auto pw = std::string(params["password"]);
    if (cli_config.perf_test) {
      pw += std::to_string(uniq++);
    }
    hibp::pawned_pw needle = hibp::convert_to_binary(sha1(pw));

    std::optional<hibp::pawned_pw> maybe_ppw;

    if (cli_config.toc) {
      maybe_ppw = toc_search(db, needle);
    } else if (auto iter = std::lower_bound(db.begin(), db.end(), needle);
               iter != db.end() && *iter == needle) {
      maybe_ppw = *iter;
    }

    int count = maybe_ppw ? maybe_ppw->count : -1;

    std::string content_type = cli_config.json ? "application/json" : "text/plain";

    auto response = req->create_response().append_header(
        restinio::http_field::content_type, std::format("{}; charset=utf-8", content_type));

    if (cli_config.json) {
      response.set_body(std::format("{{count:{}}}\n", count));
    } else {
      response.set_body(std::format("{}\n", count));
    }
    return response.done();
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

  std::cerr << std::format("Serving from {}:{}\nMake a request to http://{}:{}/some-password\n",
                           cli_config.bind_address, cli_config.port, cli_config.bind_address,
                           cli_config.port);

  auto settings = restinio::on_thread_pool<my_server_traits>(cli_config.threads)
                      .address(cli_config.bind_address)
                      .port(cli_config.port)
                      .request_handler(get_router(cli_config.db_filename));

  restinio::run(std::move(settings));
}

int main(int argc, char* argv[]) {
  CLI::App app("Have I been Pawned Server");
  define_options(app);
  CLI11_PARSE(app, argc, argv);

  try {
    if (cli_config.toc) {
      // local, temp db separate from the thread_local ones
      flat_file::database<hibp::pawned_pw> db(cli_config.db_filename,
                                              4096 / sizeof(hibp::pawned_pw));
      build_toc(db, cli_config.db_filename, cli_config.toc_entries);
    }

    run_server();
  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
