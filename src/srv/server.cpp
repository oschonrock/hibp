#include "bytearray_cast.hpp"
#include "binfuse.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "ntlm.hpp"
#include "srv/server.hpp"
#include "toc.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
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
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace hibp::srv {

auto respond(int count, auto req) {
  const std::string content_type = cli.json ? "application/json" : "text/plain";

  auto response = req->create_response().append_header(
      restinio::http_field::content_type, fmt::format("{}; charset=utf-8", content_type));

  if (cli.json) {
    response.set_body(fmt::format("{{\"count\":{}}}", count));
  } else {
    response.set_body(fmt::format("{}\n", count));
  }
  return response.done();
}

template <pw_type PwType>
auto search_and_respond(flat_file::database<PwType>& db, const PwType& needle, auto req) {
  std::optional<PwType> maybe_ppw;

  if (cli.toc) {
    maybe_ppw = hibp::toc_search(db, needle, cli.toc_bits);
  } else {
    auto iter = std::lower_bound(db.begin(), db.end(), needle);
    if (iter != db.end() && *iter == needle) {
      maybe_ppw = *iter;
    }
  }
  const int count = maybe_ppw ? maybe_ppw->count : -1;
  return respond(count, req);
}

auto bad_request(const std::string& msg, auto req) {
  return req->create_response(restinio::status_bad_request())
      .set_body(msg + "\n")
      .connection_close()
      .done();
}

auto fail_missing_db_for_format(auto req, const std::string& option, const std::string& endpoint) {
  return bad_request("You need to pass " + option + " for a " + endpoint + " request.", req);
}

void uniqefy_plain(std::string& plain_password) {
  // for performance testing, optionally make uniq_pw across threads
  static std::atomic<int> uniq{};
  if (cli.perf_test) {
    plain_password += std::to_string(uniq++);
  }
}

template <pw_type PwType>
auto handle_plain_search(flat_file::database<PwType>& sha1_db, std::string plain_password,
                         auto req) {
  uniqefy_plain(plain_password);

  PwType needle;
  if constexpr (std::is_same_v<PwType, pawned_pw_ntlm>) {
    needle.hash = ntlm(plain_password);
  } else {
    // note that sha1t64 can also be constructed from sha1 text hash
    needle = PwType{SHA1{}(plain_password)};
  }
  return search_and_respond<PwType>(sha1_db, needle, req);
}

template <hibp::binfuse_filter_source_type FilterType>
auto handle_filter_search(FilterType& filter, std::uint64_t needle, auto req) {
  const bool result = filter.contains(needle);
  const int  count  = result ? 1 : -1; // stay consistent with other dbs
  return respond(count, req);
}

template <hibp::binfuse_filter_source_type FilterType>
auto handle_plain_filter_search(FilterType& filter, std::string plain_password, auto req) {
  uniqefy_plain(plain_password);

  // TODO this is ineffecient, use binary SHA1 directly
  hibp::pawned_pw_sha1t64 pw{SHA1{}(plain_password)};
  auto                    needle = hibp::bytearray_cast<std::uint64_t>(pw.hash.data());
  return handle_filter_search(filter, needle, req);
}

template <hibp::binfuse_filter_source_type FilterType>
auto handle_hash_filter_search(FilterType& filter, const std::string& password, auto req) {
  if (!is_valid_hash<pawned_pw_sha1t64>(password)) {
    return bad_request("Invalid hash provided. Check type of hash.", req);
  }
  hibp::pawned_pw_sha1t64 pw{password};
  auto                    needle = hibp::bytearray_cast<std::uint64_t>(pw.hash.data());
  return handle_filter_search(filter, needle, req);
}

template <pw_type PwType>
auto handle_hash_search(flat_file::database<PwType>& db, const std::string& password, auto req) {

  if (!is_valid_hash<PwType>(password)) {
    return bad_request("Invalid hash provided. Check type of hash.", req);
  }
  const PwType needle{password};
  return search_and_respond<PwType>(db, needle, req);
}

// NOLINTNEXTLINE cognitive complexity
auto get_router(const std::string& sha1_db_filename, const std::string& ntlm_db_filename,
                const std::string& sha1t64_db_filename,
                const std::string& binfuse16_filter_filename,
                const std::string& binfuse8_filter_filename) {
  auto router = std::make_unique<restinio::router::express_router_t<>>();
  router->http_get(R"(/check/:format/:password)", [&](auto req, auto params) {
    try {
      // unique db object (ie set of buffers and pointers) per thread and per db file supplied
      using sha1_db_t = flat_file::database<pawned_pw_sha1>;
      thread_local auto sha1_db =
          sha1_db_filename.empty()
              ? std::unique_ptr<sha1_db_t>{}
              : std::make_unique<sha1_db_t>(sha1_db_filename, 4096 / sizeof(hibp::pawned_pw_sha1));

      using ntlm_db_t = flat_file::database<pawned_pw_ntlm>;
      thread_local auto ntlm_db =
          ntlm_db_filename.empty()
              ? std::unique_ptr<ntlm_db_t>{}
              : std::make_unique<ntlm_db_t>(ntlm_db_filename, 4096 / sizeof(hibp::pawned_pw_ntlm));

      using sha1t64_db_t = flat_file::database<pawned_pw_sha1t64>;
      thread_local auto sha1t64_db =
          sha1t64_db_filename.empty()
              ? std::unique_ptr<sha1t64_db_t>{}
              : std::make_unique<sha1t64_db_t>(sha1t64_db_filename,
                                               4096 / sizeof(hibp::pawned_pw_sha1t64));

      // only single instance across threads for binfuse filters
      static auto binfuse16_filter =
          binfuse16_filter_filename.empty()
              ? std::unique_ptr<binfuse::sharded_filter16_source>{}
              : std::make_unique<binfuse::sharded_filter16_source>(binfuse16_filter_filename);

      static auto binfuse8_filter =
          binfuse8_filter_filename.empty()
              ? std::unique_ptr<binfuse::sharded_filter8_source>{}
              : std::make_unique<binfuse::sharded_filter8_source>(binfuse8_filter_filename);

      const std::string password{params["password"]};

      if (params["format"] == "plain") {
        if (sha1_db) {
          return handle_plain_search(*sha1_db, password, req);
        }
        if (ntlm_db) {
          return handle_plain_search(*ntlm_db, password, req);
        }
        if (sha1t64_db) {
          return handle_plain_search(*sha1t64_db, password, req);
        }
        if (binfuse16_filter) {
          return handle_plain_filter_search(*binfuse16_filter, password, req);
        }
        if (binfuse8_filter) {
          return handle_plain_filter_search(*binfuse8_filter, password, req);
        }
        return fail_missing_db_for_format(
            req, "--sha1-db, --ntlm-db, --sha1t64-db, --binfuse16-filter or --binfuse8-filter, ",
            "/check/plain");
      }
      if (params["format"] == "sha1") {
        if (!sha1_db) return fail_missing_db_for_format(req, "--sha1-db", "/check/sha1");
        return handle_hash_search(*sha1_db, password, req);
      }
      if (params["format"] == "ntlm") {
        if (!ntlm_db) return fail_missing_db_for_format(req, "--ntlm-db", "/check/ntlm");
        return handle_hash_search(*ntlm_db, password, req);
      }
      if (params["format"] == "sha1t64") {
        if (!sha1t64_db) return fail_missing_db_for_format(req, "--sha1t64-db", "/check/sha1t64");
        return handle_hash_search(*sha1t64_db, password, req);
      }
      if (params["format"] == "binfuse16") {
        if (!binfuse16_filter)
          return fail_missing_db_for_format(req, "--binfuse16-filter", "/check/binfuse16");
        return handle_hash_filter_search(*binfuse16_filter, password, req);
      }
      if (params["format"] == "binfuse8") {
        if (!binfuse8_filter)
          return fail_missing_db_for_format(req, "--binfuse8-filter", "/check/binfuse8");
        return handle_hash_filter_search(*binfuse8_filter, password, req);
      }
      return req->create_response(restinio::status_not_found())
          .set_body("Bad format specified.")
          .connection_close()
          .done();
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

  std::string server = fmt::format("http://{}:{}", cli.bind_address, cli.port);
  std::string plain_using;
  if (!cli.sha1_db_filename.empty()) {
    plain_using = "sha1 db";
  } else if (!cli.ntlm_db_filename.empty()) {
    plain_using = "ntlm db";
  } else if (!cli.sha1t64_db_filename.empty()) {
    plain_using = "sha1t64 db";
  } else if (!cli.binfuse16_filter_filename.empty()) {
    plain_using = "binfuse16 filter";
  } else if (!cli.binfuse8_filter_filename.empty()) {
    plain_using = "binfuse8 filter";
  } else {
    throw std::runtime_error("cannot determine which db/filter to use for plain password queries");
  }

  std::cout << fmt::format("Serving from {0}\n"
                           "Make a request to any of:\n"
                           "{0}/check/plain/password123  [using {1}]\n",
                           server, plain_using);

  if (!cli.sha1_db_filename.empty()) {
    std::cout << fmt::format("{}/check/sha1/CBFDAC6008F9CAB4083784CBD1874F76618D2A97\n", server);
  }
  if (!cli.ntlm_db_filename.empty()) {
    std::cout << fmt::format("{}/check/ntlm/A9FDFA038C4B75EBC76DC855DD74F0DA\n", server);
  }
  if (!cli.sha1t64_db_filename.empty()) {
    std::cout << fmt::format("{}/check/sha1t64/CBFDAC6008F9CAB4\n", server);
  }
  if (!cli.binfuse16_filter_filename.empty()) {
    std::cout << fmt::format("{}/check/binfuse16/CBFDAC6008F9CAB4\n", server);
  }
  if (!cli.binfuse8_filter_filename.empty()) {
    std::cout << fmt::format("{}/check/binfuse8/CBFDAC6008F9CAB4\n", server);
  }

  auto settings = restinio::on_thread_pool<my_server_traits>(cli.threads)
                      .address(cli.bind_address)
                      .port(cli.port)
                      .request_handler(get_router(
                          cli.sha1_db_filename, cli.ntlm_db_filename, cli.sha1t64_db_filename,
                          cli.binfuse16_filter_filename, cli.binfuse8_filter_filename));

  restinio::run(std::move(settings));
}

} // namespace hibp::srv
