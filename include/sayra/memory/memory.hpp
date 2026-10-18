#pragma once
#include <mutex>
#include <sayra/core/config.hpp>
struct sqlite3;
namespace sayra {
struct Conversation {
    std::string id, project_id, model, timestamp;
    std::vector<ChatMessage> messages;
    bool transcript = false, sensitive = false;
};
class IMemoryStore {
  public:
    virtual ~IMemoryStore() = default;
    virtual void save(const Conversation &) = 0;
    virtual Conversation load(const std::string &) = 0;
    virtual std::vector<std::string> list() = 0;
    virtual void erase(const std::string &) = 0;
};
class MemoryStore final : public IMemoryStore {
    size_t max_conversations_;
    sqlite3 *db_ = nullptr;
    std::mutex mutex_;

  public:
    explicit MemoryStore(const std::filesystem::path &, size_t max_conversations = 100);
    ~MemoryStore() override;
    MemoryStore(const MemoryStore &) = delete;
    MemoryStore &operator=(const MemoryStore &) = delete;
    void save(const Conversation &) override;
    Conversation load(const std::string &) override;
    std::vector<std::string> list() override;
    void erase(const std::string &) override;
};
class ConversationManager {
    IMemoryStore &store_;
    Configuration config_;

  public:
    ConversationManager(IMemoryStore &s, Configuration c) : store_(s), config_(std::move(c)) {}
    Conversation create(const std::string &project = {});
    Conversation load(const std::string &id) {
        return store_.load(id);
    }
    void append(Conversation &, const ChatMessage &, const std::string &model);
    void erase(const std::string &id) {
        store_.erase(id);
    }
    std::vector<std::string> list() {
        return store_.list();
    }
};
} // namespace sayra