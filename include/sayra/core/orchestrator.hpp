#pragma once
#include <sayra/events/events.hpp>
#include <sayra/integration/knowledge.hpp>
#include <sayra/memory/memory.hpp>
#include <sayra/routing/routing.hpp>
#include <sayra/tools/tools.hpp>
namespace sayra {
class PromptBuilder {
  public:
    std::vector<ChatMessage> build(const Conversation &, const AgentRequest &,
                                   const std::string &context) const;
};
class DiagnosticsService {
    ModelRegistry &registry_;

  public:
    explicit DiagnosticsService(ModelRegistry &r) : registry_(r) {}
    std::string report() const;
};
// Calls are blocking on a worker thread supplied by the host; one in-flight request per instance.
class SayraOrchestrator {
    Configuration config_;
    ModelRegistry &models_;
    ProjectContextManager &projects_;
    ConversationManager &conversations_;
    ToolRegistry &tools_;
    EventBus &events_;
    ModelRouter router_;
    std::shared_ptr<IKnowledgeRetriever> knowledge_;
    std::shared_ptr<IResearchService> research_;
    RoutingDecision last_;

  public:
    SayraOrchestrator(Configuration, ModelRegistry &, ProjectContextManager &, ConversationManager &,
                      ToolRegistry &, EventBus &);
    void set_knowledge(std::shared_ptr<IKnowledgeRetriever> k) {
        knowledge_ = std::move(k);
    }
    void set_research(std::shared_ptr<IResearchService> r) {
        research_ = std::move(r);
    }
    const RoutingDecision &last_routing() const {
        return last_;
    }
    AgentResponse run(AgentRequest, Conversation &, std::stop_token);
};
} // namespace sayra