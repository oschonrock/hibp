#include "srv/server.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "toc.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <optional>
#include <restinio/http_headers.hpp>
#include <restinio/http_server_run.hpp>
#include <restinio/router/express.hpp>
#include <restinio/traits.hpp>
#include <sha1.h>
#include <string>
#include <string_view>
#include <utility>

namespace hibp::srv {

auto search_and_respond(flat_file::database<hibp::pawned_pw_sha1>& db,
                        const hibp::pawned_pw_sha1& needle, auto req) {
  std::optional<hibp::pawned_pw_sha1> maybe_ppw;

  if (cli.toc) {
    maybe_ppw = hibp::toc_search(db, needle, cli.toc_bits);
  } else {
    auto iter = std::lower_bound(db.begin(), db.end(), needle);
    if (iter != db.end() && *iter == needle) {
      maybe_ppw = *iter;
    }
  }
  const int count = maybe_ppw ? maybe_ppw->count : -1;

  const std::string content_type = cli.json ? "application/json" : "text/plain";

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
    try {
      // one db object (ie set of buffers and pointers) per thread
      thread_local flat_file::database<hibp::pawned_pw_sha1> db(
          db_filename, 4096 / sizeof(hibp::pawned_pw_sha1));

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
      const hibp::pawned_pw_sha1 needle{sha1_txt_pw};

      return search_and_respond(db, needle, req);

    } catch (const std::exception& e) {
      // TODO log error to std::cerr with thread mutex
      return req->create_response(restinio::status_internal_server_error())
          .set_body(e.what())
          .connection_close()
          .done();
    }
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
} // namespace hibp::srv
