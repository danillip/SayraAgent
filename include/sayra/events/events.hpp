#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <sayra/core/types.hpp>
namespace sayra {
enum class EventType {
    RequestStarted,
    IntentDetected,
    ModelSelected,
    ThinkingStatus,
    ToolStarted,
    ToolCompleted,
    TextDelta,
    SourceAdded,
    AssistCardReady,
    RequestCompleted,
    RequestFailed,
    RequestCancelled
};
struct AssistCard {
    std::string request_id, session_id, title, short_answer, details, used_model, used_provider, project_id,
        timestamp;
    TaskType task_type = TaskType::Unknown;
    std::vector<SourceReference> sources;
    VerificationStatus verification = VerificationStatus::Unverified;
    bool is_final = false;
};
struct CoreEvent {
    EventType type;
    std::string request_id, session_id, text, timestamp = utc_now();
    std::optional<AssistCard> card;
    std::optional<SourceReference> source;
};
class IEventSink {
  public:
    virtual ~IEventSink() = default;
    virtual void on_event(const CoreEvent &) = 0;
};
class IOverlayEventSink : public IEventSink {};
// Подписчиками владеет приложение. Обработчики вызываются в потоке публикации.
class EventBus {
    mutable std::mutex mutex_;
    size_t next_ = 0;
    std::map<size_t, std::weak_ptr<IEventSink>> sinks_;

  public:
    size_t subscribe(const std::shared_ptr<IEventSink> &sink);
    void unsubscribe(size_t id);
    void publish(const CoreEvent &event) const noexcept;
};
} // namespace sayra