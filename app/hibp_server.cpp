#include <restinio/core.hpp>

#include "flat_file.hpp"
#include "hibp.hpp"
#include "os/bch.hpp"
#include "restinio/traits.hpp"
#include "sha1.hpp"
#include <cstdlib>
#include <string_view>

int main(int argc, char* argv[]) {

  try {
    if (argc < 2) {
      throw std::domain_error("USAGE: " + std::string(argv[0]) + " dbfile.bin");
    }
    auto db_file = std::string(argv[1]);

    auto router = std::make_unique<restinio::router::express_router_t<>>();
    router->http_get(R"(/:password)", [&](auto req, auto params) {
      thread_local flat_file::database<hibp::pawned_pw> db(db_file, 4096 / sizeof(hibp::pawned_pw));
      const auto qp = restinio::parse_query(req->header().query());

      SHA1 sha1;
      sha1.update(std::string(params["password"]));
      hibp::pawned_pw needle = hibp::convert_to_binary(sha1.final());

      std::optional<hibp::pawned_pw> maybe_ppw;

      if (auto iter = std::lower_bound(db.begin(), db.end(), needle);
          iter != db.end() && *iter == needle) {
        maybe_ppw = *iter;
      }

      int count = maybe_ppw ? maybe_ppw->count : -1;
      return req->create_response().set_body(fmt::format("count={}", count)).done();
    });

    router->non_matched_request_handler([](auto req) {
      return req->create_response(restinio::status_not_found()).connection_close().done();
    });

    // Launching a server with custom traits.
    struct my_server_traits : public restinio::default_traits_t {
      using request_handler_t = restinio::router::express_router_t<>;
    };

    restinio::run(restinio::on_thread_pool<my_server_traits>(std::thread::hardware_concurrency())
                  .address("localhost")
                  .port(8082)
                  .request_handler(std::move(router)));

  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
