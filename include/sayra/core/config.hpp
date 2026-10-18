#pragma once
#include <filesystem>
#include <sayra/core/types.hpp>
namespace sayra {
struct Configuration {
    std::filesystem::path data_dir = "data";
    std::string ollama_url = "http://127.0.0.1:11434", cloud_url, cloud_key_env = "SAYRA_CLOUD_API_KEY";
    bool cloud_enabled = false, project_cloud = false, transcript_cloud = false, debug = false;
    RoutingMode mode = RoutingMode::LocalOnly;
    long connect_timeout = 5, request_timeout = 180;
    int retries = 1, max_iterations = 4, max_tool_calls = 8;
    size_t max_conversations = 100;
    size_t history_bytes = 16000, file_bytes = 64000, context_bytes = 24000, max_files = 500,
           max_history_messages = 100;
    double max_cloud_cost = 0;
    GenerationOptions generation;
    std::string embedding_model = "bge-m3:latest", classifier_model;
    Json model_profiles = Json::array(), projects = Json::array();
    std::string active_project;
    size_t realtime_bytes = 8000;
    int realtime_interval_seconds = 5;
};
class ConfigurationManager {
  public:
    static Configuration load(const std::filesystem::path &path);
};
} // namespace sayra