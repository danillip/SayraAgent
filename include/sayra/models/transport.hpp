#pragma once
#include <memory>
#include <sayra/core/types.hpp>
#include <string_view>
namespace sayra {
struct HttpRequest {
    std::string url, method = "GET", body;
    std::vector<std::string> headers;
    long connect_timeout = 5, timeout = 180;
};
struct HttpResult {
    long status = 0;
    std::string body;
    double seconds = 0;
};
using ChunkCallback = std::function<void(std::string_view)>;
class IHttpTransport {
  public:
    virtual ~IHttpTransport() = default;
    virtual HttpResult perform(const HttpRequest &, ChunkCallback, std::stop_token) = 0;
};
class CurlTransport final : public IHttpTransport {
  public:
    HttpResult perform(const HttpRequest &, ChunkCallback, std::stop_token) override;
};
// One parser per request; feed accepts arbitrary network boundaries.
class StreamParser {
    bool sse_;
    std::string pending_, data_;
    std::function<void(std::string)> frame_;
    void line(std::string line);

  public:
    StreamParser(bool sse, std::function<void(std::string)> frame) : sse_(sse), frame_(std::move(frame)) {}
    void feed(std::string_view bytes);
    void finish();
};
} // namespace sayra