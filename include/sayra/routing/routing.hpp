#pragma once
#include <sayra/models/providers.hpp>
namespace sayra {
struct IntentDecision {
    TaskType primary = TaskType::Unknown;
    std::vector<TaskType> secondary;
    bool needs_tools = false, needs_web = false, private_data = false, fast = false, ambiguous = false;
    size_t required_context = 0;
    std::string explanation;
    bool has(TaskType t) const;
};
class IntentClassifier {
  public:
    IntentDecision classify(const AgentRequest &, const std::vector<ChatMessage> &history) const;
    // Уточнение неоднозначного намерения через выбранную и разрешённую модель.
    IntentDecision refine(IntentDecision, const AgentRequest &, IModelProvider &, const ModelInfo &,
                          std::stop_token) const;
};
struct RoutingCandidate {
    ModelInfo model;
    double score;
    std::string reason;
};
struct RoutingDecision {
    ModelInfo selected;
    std::vector<RoutingCandidate> candidates;
    std::vector<std::string> rejected;
    std::string reason;
    bool fallback = false;
};
class ModelRouter {
    Configuration config_;

  public:
    explicit ModelRouter(Configuration c) : config_(std::move(c)) {}
    RoutingDecision route(const AgentRequest &, const IntentDecision &, const std::vector<ModelInfo> &) const;
};
class TaskPlanner {
  public:
    std::vector<std::string> plan(const IntentDecision &) const;
};
} // namespace sayra