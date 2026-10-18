#pragma once
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace sayra {
using Json = nlohmann::json;
inline constexpr int contract_version = 1;
// Construct a native path from UTF-8 without deprecated C++20 u8path.
std::filesystem::path path_from_utf8(std::string_view text);
// Owns its copied value; no borrowed CRT environment pointer escapes.
std::optional<std::string> environment_variable(const std::string &name);
std::string new_id();
std::string utc_now();
std::string lower_utf8(std::string text);
std::string utf8_prefix(const std::string &text, size_t bytes);
enum class TaskType {
    CasualConversation,
    GeneralQuestion,
    Coding,
    CodeExplanation,
    ProjectAnalysis,
    DocumentQuestion,
    WebResearch,
    ComplexReasoning,
    RealtimeAssistance,
    TaskExecution,
    Unknown
};
std::string to_string(TaskType type);
enum class RoutingMode { Auto, LocalOnly, CloudOnly, Hybrid, Manual };
enum class VerificationStatus { Unverified, Verified, Partial };
struct ProviderError : std::runtime_error {
    long http_status;
    bool retryable;
    bool cancelled;
    ProviderError(std::string message, long status = 0, bool temporary = false, bool stopped = false)
        : runtime_error(std::move(message)), http_status(status), retryable(temporary), cancelled(stopped) {}
};
void check_cancelled(std::stop_token stop);
struct ToolCall {
    std::string id, name;
    Json arguments = Json::object();
};
struct ChatMessage {
    std::string role, content, tool_call_id, tool_name;
    std::vector<ToolCall> tool_calls;
};
struct ModelInfo {
    std::string name, provider;
    bool available = false, loaded = false, chat = false, embedding = false, tools = false;
    size_t context = 0;
    int priority = 0;
    std::optional<double> quality, speed, cost;
    std::vector<std::string> roles;
};
struct GenerationOptions {
    bool stream = true;
    int max_tokens = 1024;
    int context = 4096;
    double temperature = 0.5;
    std::string keep_alive = "5m";
};
struct GenerationRequest {
    std::string request_id, model;
    std::vector<ChatMessage> messages;
    GenerationOptions options;
    Json tools = Json::array();
};
struct GenerationMetrics {
    double seconds = 0;
    size_t output_tokens = 0;
};
struct GenerationResult {
    std::string text, model, provider;
    std::vector<ToolCall> tool_calls;
    GenerationMetrics metrics;
};
using DeltaCallback = std::function<void(const std::string &, bool thinking)>;
struct SourceReference {
    std::string id, title, uri, published_at, evidence;
    VerificationStatus verification = VerificationStatus::Unverified;
};
struct AgentRequest {
    std::string request_id, conversation_id, project_id, text, forced_model, session_id;
    RoutingMode mode = RoutingMode::Auto;
    bool transcript_context = false, allow_internet = false;
    bool realtime = false, allow_cloud = false, allow_project_cloud = false, allow_transcript_cloud = false;
};
struct AgentResponse {
    std::string request_id, text, model, provider, project_id;
    std::vector<SourceReference> sources;
    VerificationStatus verification = VerificationStatus::Unverified;
    GenerationMetrics metrics;
};
} // namespace sayra