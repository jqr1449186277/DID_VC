#include "http_transport.hpp"

#include "did_app_common.hpp"
#include "httplib.h"

namespace {

void apply_timeout(httplib::Client* cli, int timeoutMs) {
  cli->set_connection_timeout(timeoutMs / 1000, (timeoutMs % 1000) * 1000);
  cli->set_read_timeout(timeoutMs / 1000, (timeoutMs % 1000) * 1000);
  cli->set_write_timeout(timeoutMs / 1000, (timeoutMs % 1000) * 1000);
}

bool response_ok(const httplib::Result& res, std::string* outBody) {
  if (!res) {
    if (outBody) *outBody = "transport_error_or_timeout";
    return false;
  }
  if (outBody) *outBody = res->body;
  if (res->status != 200) {
    if (outBody) *outBody = "http_status=" + std::to_string(res->status) + ";body=" + res->body;
    return false;
  }
  return true;
}

std::string request_path(const UrlParts& u, const std::string& suffix) {
  std::string out = u.basePath + suffix;
  return out.empty() ? "/" : out;
}

}  // namespace

bool http_get_text(const std::string& baseUrl,
                   const std::string& pathAndQuery,
                   std::string& outBody,
                   int timeoutMs) {
  const UrlParts u = parse_url(baseUrl);
  httplib::Client cli(u.host, u.port);
  apply_timeout(&cli, timeoutMs);
  return response_ok(cli.Get(request_path(u, pathAndQuery).c_str()), &outBody);
}

bool http_post_json(const std::string& baseUrl,
                    const std::string& path,
                    const nlohmann::json& body,
                    std::string& outBody,
                    int timeoutMs) {
  const UrlParts u = parse_url(baseUrl);
  httplib::Client cli(u.host, u.port);
  apply_timeout(&cli, timeoutMs);
  return response_ok(cli.Post(request_path(u, path).c_str(), body.dump(), "application/json"), &outBody);
}

bool http_post_json_url(const std::string& fullUrl,
                        const nlohmann::json& body,
                        std::string& outBody,
                        int timeoutMs) {
  return http_post_json(fullUrl, "", body, outBody, timeoutMs);
}
