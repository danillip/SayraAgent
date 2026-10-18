#include <sayra/events/events.hpp>
namespace sayra {
size_t EventBus::subscribe(const std::shared_ptr<IEventSink> &sink) {
    std::lock_guard lock(mutex_);
    sinks_[++next_] = sink;
    return next_;
}
void EventBus::unsubscribe(size_t id) {
    std::lock_guard lock(mutex_);
    sinks_.erase(id);
}
void EventBus::publish(const CoreEvent &event) const noexcept {
    try {
        std::vector<std::shared_ptr<IEventSink>> copy;
        {
            std::lock_guard lock(mutex_);
            for (const auto &[id, weak] : sinks_)
                if (auto s = weak.lock())
                    copy.push_back(std::move(s));
        }
        for (const auto &s : copy) {
            try {
                s->on_event(event);
            } catch (...) {
            }
        }
    } catch (...) {
    }
}
} // namespace sayra