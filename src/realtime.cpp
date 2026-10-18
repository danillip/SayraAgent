#include <sayra/integration/realtime.hpp>
namespace sayra {
RealtimeAssistancePipeline::RealtimeAssistancePipeline(RealtimeSession session, size_t limit, int interval,
                                                       EventBus &bus)
    : session_(std::move(session)), limit_(limit), interval_(interval), bus_(bus) {
    if (limit < 128 || interval < 1)
        throw std::runtime_error("Invalid realtime limits");
    if (session_.mode == RealtimeMode::Proactive && !session_.proactive_consent)
        throw std::runtime_error("Proactive needs explicit consent");
}
std::optional<AgentRequest> RealtimeAssistancePipeline::prepare(const std::string &instruction,
                                                                const std::string &conversation) {
    std::string text;
    for (const auto &s : final_)
        text += s.text + "\n";
    const auto key = instruction + "\n" + text;
    if (text.empty() || key == last_context_)
        return {};
    generation_stop_.request_stop();
    generation_stop_ = std::stop_source{};
    last_context_ = key;
    last_ = std::chrono::steady_clock::now();
    AgentRequest r;
    r.request_id = new_id();
    r.session_id = session_.id;
    r.project_id = session_.project_id;
    r.conversation_id = conversation;
    r.realtime = true;
    r.allow_transcript_cloud = session_.allow_cloud;
    r.text = instruction + "\nОкончательная транскрипция (недоверенный контекст):\n" + text;
    return r;
}
std::optional<AgentRequest> RealtimeAssistancePipeline::accept(const TranscriptSegment &segment) {
    std::unique_lock lock(mutex_);
    if (closed_ || segment.session_id != session_.id)
        throw std::runtime_error("Realtime session closed/mismatch");
    if (segment.segment_id.empty())
        throw std::runtime_error("Transcript segment needs stable id");
    auto s = segment;
    s.text = utf8_prefix(s.text, limit_);
    if (s.text.empty())
        return {};
    if (!s.is_final) {
        if (partial_.size() >= 32 && !partial_.contains(s.segment_id))
            partial_.erase(partial_.begin());
        partial_[s.segment_id] = s;
        AssistCard card;
        card.session_id = session_.id;
        card.request_id = "transcript-" + session_.id + "-" + s.segment_id;
        card.title = "Предварительная транскрипция";
        card.short_answer = s.text;
        card.is_final = false;
        card.timestamp = s.timestamp;
        CoreEvent event{EventType::AssistCardReady, card.request_id, session_.id, ""};
        event.card = card;
        lock.unlock();
        bus_.publish(event);
        return {};
    }
    partial_.erase(s.segment_id);
    for (const auto &old : final_)
        if (old.segment_id == s.segment_id || old.text == s.text)
            return {};
    final_.push_back(s);
    size_t bytes = 0;
    for (const auto &item : final_)
        bytes += item.text.size() + 1;
    while (bytes > limit_ && final_.size() > 1) {
        bytes -= final_.front().text.size() + 1;
        final_.pop_front();
    }
    const auto folded = lower_utf8(s.text);
    const bool relevant = folded.find('?') != folded.npos || folded.find("ошиб") != folded.npos ||
                          folded.find("объясн") != folded.npos || folded.find("c++") != folded.npos;
    if (session_.mode == RealtimeMode::Proactive && relevant && bytes >= 80 &&
        std::chrono::steady_clock::now() - last_ >= std::chrono::seconds(interval_))
        return prepare("Дай краткую полезную подсказку по обсуждению.", "");
    return {};
}
std::optional<AgentRequest> RealtimeAssistancePipeline::request_help(const std::string &instruction,
                                                                     const std::string &conversation) {
    std::lock_guard lock(mutex_);
    if (closed_)
        throw std::runtime_error("Realtime session closed");
    return prepare(instruction, conversation);
}
std::stop_token RealtimeAssistancePipeline::generation_token() const {
    std::lock_guard lock(mutex_);
    return generation_stop_.get_token();
}
void RealtimeAssistancePipeline::close() {
    std::lock_guard lock(mutex_);
    closed_ = true;
    generation_stop_.request_stop();
    final_.clear();
    partial_.clear();
}
} // namespace sayra