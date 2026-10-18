#pragma once
#include <deque>
#include <mutex>
#include <sayra/events/events.hpp>
namespace sayra {
enum class RealtimeMode { OnDemand, ContextAware, Proactive };
struct TranscriptSegment {
    std::string session_id, segment_id, timestamp, text, source;
    bool is_final = false;
    std::optional<std::string> speaker;
};
struct RealtimeSession {
    std::string id, project_id;
    RealtimeMode mode = RealtimeMode::OnDemand;
    bool allow_cloud = false, proactive_consent = false;
};
struct RealtimeAssistanceRequest {
    RealtimeSession session;
    std::string instruction, conversation_id;
};
class ITranscriptSource {
  public:
    virtual ~ITranscriptSource() = default;
    virtual void start(std::function<void(TranscriptSegment)>) = 0;
    virtual void stop() = 0; // must return after all callbacks complete
};
// Lightweight ingress. Host schedules returned requests on a worker, cancelling obsolete ones.
class RealtimeAssistancePipeline {
    mutable std::mutex mutex_;
    RealtimeSession session_;
    std::deque<TranscriptSegment> final_;
    std::map<std::string, TranscriptSegment> partial_;
    size_t limit_;
    int interval_;
    EventBus &bus_;
    std::stop_source generation_stop_;
    std::chrono::steady_clock::time_point last_{};
    std::string last_context_;
    bool closed_ = false;
    std::optional<AgentRequest> prepare(const std::string &, const std::string &);

  public:
    RealtimeAssistancePipeline(RealtimeSession, size_t limit, int interval, EventBus &);
    std::optional<AgentRequest> accept(const TranscriptSegment &);
    std::optional<AgentRequest> request_help(const std::string &instruction, const std::string &conversation);
    std::stop_token generation_token() const;
    void close();
};
} // namespace sayra