#include <fstream>
#include <sayra/core/config.hpp>
namespace sayra {
Configuration ConfigurationManager::load(const std::filesystem::path &path) {
    Configuration c;
    if (!std::filesystem::exists(path))
        return c;
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Невозможно открыть конфигурацию");
    const auto j = Json::parse(file);
    c.data_dir = sayra::path_from_utf8(j.value("data_dir", "data"));
    c.ollama_url = j.value("ollama_url", c.ollama_url);
    const auto cloud = j.value("cloud", Json::object());
    c.cloud_enabled = cloud.value("enabled", false);
    c.cloud_url = cloud.value("base_url", "");
    c.cloud_key_env = cloud.value("key_env", c.cloud_key_env);
    c.project_cloud = cloud.value("allow_project_data", false);
    c.transcript_cloud = cloud.value("allow_transcripts", false);
    c.max_cloud_cost = cloud.value("max_cost_per_1k_tokens", 0.0);
    const auto mode = j.value("mode", "local");
    if (mode == "auto")
        c.mode = RoutingMode::Auto;
    else if (mode == "cloud")
        c.mode = RoutingMode::CloudOnly;
    else if (mode == "hybrid")
        c.mode = RoutingMode::Hybrid;
    else if (mode != "local")
        throw std::runtime_error("Неизвестный routing mode");
    c.debug = j.value("debug", false);
    c.model_profiles = j.value("models", Json::array());
    c.projects = j.value("projects", Json::array());
    c.active_project = j.value("active_project", "");
    c.embedding_model = j.value("embedding_model", c.embedding_model);
    c.classifier_model = j.value("classifier_model", "");
    const auto limits = j.value("limits", Json::object());
    c.history_bytes = limits.value("history_bytes", c.history_bytes);
    c.file_bytes = limits.value("file_bytes", c.file_bytes);
    c.context_bytes = limits.value("context_bytes", c.context_bytes);
    c.max_files = limits.value("max_files", c.max_files);
    c.max_conversations = limits.value("conversations", size_t{100});
    c.max_history_messages = limits.value("history_messages", c.max_history_messages);
    c.max_iterations = limits.value("agent_iterations", c.max_iterations);
    c.max_tool_calls = limits.value("tool_calls", c.max_tool_calls);
    const auto http = j.value("http", Json::object());
    c.connect_timeout = http.value("connect_timeout", 5L);
    c.request_timeout = http.value("request_timeout", 180L);
    c.retries = http.value("retries", 1);
    const auto gen = j.value("generation", Json::object());
    c.generation.stream = gen.value("stream", true);
    c.generation.max_tokens = gen.value("max_tokens", 1024);
    c.generation.context = gen.value("context", 4096);
    c.generation.temperature = gen.value("temperature", 0.5);
    c.generation.keep_alive = gen.value("keep_alive", "5m");
    const auto rt = j.value("realtime", Json::object());
    c.realtime_bytes = rt.value("context_bytes", size_t{8000});
    c.realtime_interval_seconds = rt.value("interval_seconds", 5);
    if (c.max_iterations < 1 || c.max_iterations > 16 || c.max_tool_calls < 0 || c.max_tool_calls > 64 ||
        c.retries < 0 || c.retries > 3 || c.connect_timeout < 1 || c.request_timeout < 1 ||
        c.request_timeout > 600 || c.history_bytes < 256 || c.history_bytes > 1000000 ||
        c.context_bytes < 256 || c.context_bytes > 1000000 || c.file_bytes < 1 || c.file_bytes > 10000000 ||
        c.max_files < 1 || c.max_files > 10000 || c.max_conversations < 1 || c.max_conversations > 10000 ||
        c.max_history_messages < 2 || c.generation.max_tokens < 1 || c.generation.context < 512 ||
        c.max_cloud_cost < 0 || c.realtime_bytes < 128 || c.realtime_interval_seconds < 1 ||
        !c.model_profiles.is_array() || !c.projects.is_array())
        throw std::runtime_error("Некорректные лимиты конфигурации");
    return c;
}
} // namespace sayra