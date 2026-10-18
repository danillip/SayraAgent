#include <curl/curl.h>
#include <exception>
#include <limits>
#include <sayra/models/transport.hpp>
namespace sayra {
namespace {
struct CurlGlobal {
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT))
            throw std::runtime_error("libcurl init failed");
    }
    ~CurlGlobal() {
        curl_global_cleanup();
    }
};
struct Context {
    CURL *easy;
    HttpResult result;
    ChunkCallback callback;
    std::stop_token stop;
    std::exception_ptr failure;
    size_t received = 0;
};
size_t write_chunk(char *ptr, size_t size, size_t count, void *raw) noexcept {
    auto &c = *static_cast<Context *>(raw);
    try {
        check_cancelled(c.stop);
        if (size && count > (std::numeric_limits<size_t>::max)() / size)
            throw std::runtime_error("HTTP size overflow");
        const auto bytes = size * count;
        if (bytes > 16 * 1024 * 1024 || c.received > 16 * 1024 * 1024 - bytes)
            throw std::runtime_error("HTTP response exceeds limit");
        c.received += bytes;
        long status = 0;
        curl_easy_getinfo(c.easy, CURLINFO_RESPONSE_CODE, &status);
        if (c.callback && status >= 200 && status < 300)
            c.callback(std::string_view(ptr, bytes));
        else
            c.result.body.append(ptr, bytes);
        return bytes;
    } catch (...) {
        c.failure = std::current_exception();
        return 0;
    }
}
int progress(void *p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
    return static_cast<Context *>(p)->stop.stop_requested() ? 1 : 0;
}
} // namespace
HttpResult CurlTransport::perform(const HttpRequest &request, ChunkCallback callback, std::stop_token stop) {
    static CurlGlobal global;
    check_cancelled(stop);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), curl_easy_cleanup);
    if (!easy)
        throw std::runtime_error("libcurl allocation failed");
    Context c{easy.get(), {}, std::move(callback), stop};
    curl_slist *raw = nullptr;
    for (const auto &h : request.headers) {
        auto *next = curl_slist_append(raw, h.c_str());
        if (!next) {
            curl_slist_free_all(raw);
            throw std::bad_alloc();
        }
        raw = next;
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(raw, curl_slist_free_all);
    char errors[CURL_ERROR_SIZE]{};
    auto set = [&](CURLoption opt, auto value) {
        auto rc = curl_easy_setopt(easy.get(), opt, value);
        if (rc)
            throw ProviderError(curl_easy_strerror(rc));
    };
    set(CURLOPT_URL, request.url.c_str());
    set(CURLOPT_HTTPHEADER, headers.get());
    set(CURLOPT_PROTOCOLS_STR, "http,https");
    set(CURLOPT_FOLLOWLOCATION, 0L);
    set(CURLOPT_CONNECTTIMEOUT, request.connect_timeout);
    set(CURLOPT_TIMEOUT, request.timeout);
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_WRITEFUNCTION, &write_chunk);
    set(CURLOPT_WRITEDATA, &c);
    set(CURLOPT_XFERINFOFUNCTION, &progress);
    set(CURLOPT_XFERINFODATA, &c);
    set(CURLOPT_NOPROGRESS, 0L);
    set(CURLOPT_ERRORBUFFER, errors);
    if (request.url.starts_with("http://127.0.0.1:") || request.url.starts_with("http://localhost:"))
        set(CURLOPT_NOPROXY, "*");
    if (request.method == "POST") {
        set(CURLOPT_POST, 1L);
        set(CURLOPT_POSTFIELDS, request.body.c_str());
        set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
    }
    const auto start = std::chrono::steady_clock::now();
    const auto code = curl_easy_perform(easy.get());
    c.result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &c.result.status);
    check_cancelled(stop);
    if (c.failure)
        std::rethrow_exception(c.failure);
    if (code)
        throw ProviderError("Ошибка соединения с провайдером (curl " + std::to_string(code) + ")", 0, true);
    return c.result;
}
void StreamParser::line(std::string value) {
    if (!value.empty() && value.back() == '\r')
        value.pop_back();
    if (!sse_) {
        if (!value.empty())
            frame_(std::move(value));
        return;
    }
    if (value.empty()) {
        if (!data_.empty()) {
            data_.pop_back();
            auto frame = std::move(data_);
            data_.clear();
            frame_(std::move(frame));
        }
    } else if (value.starts_with("data:")) {
        auto part = value.substr(5);
        if (part.starts_with(' '))
            part.erase(0, 1);
        data_ += part + '\n';
    }
}
void StreamParser::feed(std::string_view bytes) {
    if (pending_.size() + bytes.size() + data_.size() > 2 * 1024 * 1024)
        throw ProviderError("Stream frame too large");
    pending_.append(bytes);
    size_t pos;
    while ((pos = pending_.find('\n')) != std::string::npos) {
        auto value = pending_.substr(0, pos);
        pending_.erase(0, pos + 1);
        line(std::move(value));
    }
}
void StreamParser::finish() {
    if (!pending_.empty()) {
        auto last = std::move(pending_);
        pending_.clear();
        line(std::move(last));
    }
    if (sse_)
        line("");
}
} // namespace sayra