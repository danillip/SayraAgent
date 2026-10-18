#include <atomic>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sayra/core/types.hpp>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif
namespace sayra {
std::filesystem::path path_from_utf8(std::string_view text) {
    std::u8string encoded;
    encoded.reserve(text.size());
    for (const unsigned char value : text)
        encoded.push_back(static_cast<char8_t>(value));
    return std::filesystem::path(encoded);
}
std::optional<std::string> environment_variable(const std::string &name) {
    if (name.empty() || name.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid environment variable name");
#ifdef _MSC_VER
    char *raw = nullptr;
    size_t length = 0;
    const auto error = _dupenv_s(&raw, &length, name.c_str());
    std::unique_ptr<char, decltype(&std::free)> value(raw, &std::free);
    if (error != 0)
        throw std::runtime_error("Cannot read environment variable");
    if (!value)
        return std::nullopt;
    return std::string(value.get());
#else
    const auto *value = std::getenv(name.c_str());
    if (!value)
        return std::nullopt;
    return std::string(value);
#endif
}
std::string new_id() {
    static std::atomic<unsigned long long> seq{0};
    return std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
           std::to_string(++seq);
}
std::string utc_now() {
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm value{};
#ifdef _WIN32
    gmtime_s(&value, &time);
#else
    gmtime_r(&time, &value);
#endif
    std::ostringstream out;
    out << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
std::string lower_utf8(std::string text) {
#ifdef _WIN32
    if (text.empty())
        return text;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring wide(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), n);
    std::wstring lower(n, L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.data(), n, lower.data(), n, nullptr,
                       nullptr, 0))
        return text;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, lower.data(), n, nullptr, 0, nullptr, nullptr);
    text.resize(bytes);
    WideCharToMultiByte(CP_UTF8, 0, lower.data(), n, text.data(), bytes, nullptr, nullptr);
#else
    // Приведение ASCII и кириллицы к нижнему регистру без Windows API.
    for (size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        if (c >= 'A' && c <= 'Z')
            text[i] += 'a' - 'A';
        else if (i + 1 < text.size() && c == 0xD0) {
            auto next = static_cast<unsigned char>(text[i + 1]);
            if (next >= 0x90 && next <= 0x9F)
                text[++i] = static_cast<char>(next + 0x20);
            else if (next >= 0xA0 && next <= 0xAF) {
                text[i] = static_cast<char>(0xD1);
                text[++i] = static_cast<char>(next - 0x20);
            } else if (next == 0x81) {
                text[i] = static_cast<char>(0xD1);
                text[++i] = static_cast<char>(0x91);
            }
        }
    }
#endif
    return text;
}
std::string utf8_prefix(const std::string &text, size_t bytes) {
    if (text.size() <= bytes)
        return text;
    while (bytes && (static_cast<unsigned char>(text[bytes]) & 0xC0) == 0x80)
        --bytes;
    return text.substr(0, bytes);
}
void check_cancelled(std::stop_token stop) {
    if (stop.stop_requested())
        throw ProviderError("Запрос отменён", 0, false, true);
}
std::string to_string(TaskType t) {
    static const char *names[] = {"CasualConversation", "GeneralQuestion",  "Coding",      "CodeExplanation",
                                  "ProjectAnalysis",    "DocumentQuestion", "WebResearch", "ComplexReasoning",
                                  "RealtimeAssistance", "TaskExecution",    "Unknown"};
    return names[static_cast<size_t>(t)];
}
} // namespace sayra