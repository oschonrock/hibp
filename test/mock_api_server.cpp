#include "restinio/http_server_run.hpp"
#include "restinio/http_headers.hpp"
#include "restinio/router/express.hpp"
#include "restinio/sendfile.hpp"
#include "restinio/traits.hpp"
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <restinio/all.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace fs = std::filesystem;

auto get_router(const fs::path& static_dir) {
  auto router = std::make_unique<restinio::router::express_router_t<>>();

  router->http_get("/:file", [static_dir](auto req, auto params) {
    const fs::path requested_path(params["file"]);

    const auto qp = restinio::parse_query(req->header().query());
    fs::path   file_path;
    if (qp.has("mode") && qp["mode"] == "ntlm") {
      file_path = static_dir / "ntlm" / requested_path;
    } else {
      file_path = static_dir / "sha1" / requested_path;
    }

    if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
      return req->create_response()
          .append_header(restinio::http_field::content_type, "text/plain; charset=utf-8")
          .set_body(restinio::sendfile(file_path))
          .done();
    }
    std::cerr << "404: " << file_path << "\n";
    return req->create_response(restinio::status_not_found()).connection_close().done();
  });

  router->non_matched_request_handler([](auto req) {
    std::cerr << "404: Unmatched Request: '" << req->header().path() << "'\n";
    return req->create_response(restinio::status_not_found()).connection_close().done();
  });

  return router;
}

int main(int argc, char* argv[]) {
  try {
    if (argc < 2) {
      throw std::runtime_error("Usage: mock_api_server static_dir");
    }
    const fs::path static_dir{argv[1]};

    if (!fs::exists(static_dir) || !fs::is_directory(static_dir)) {
      throw std::runtime_error("Static directory does not exist");
    }

    struct server_traits : public restinio::default_single_thread_traits_t {
      using request_handler_t = restinio::router::express_router_t<>;
    };

    auto settings = restinio::on_this_thread<server_traits>()
                        .address("127.0.0.1")
                        .port(8090)
                        .request_handler(get_router(static_dir));

    restinio::run(std::move(settings));

  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << '\n';
    return 1;
  }
  return 0;
}
