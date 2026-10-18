#include <memory>
#include <sayra/memory/memory.hpp>
#include <sqlite3.h>
namespace sayra {
namespace {
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Statement prepare(sqlite3 *db, const char *sql) {
    sqlite3_stmt *p = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &p, nullptr) != SQLITE_OK)
        throw std::runtime_error("SQLite prepare failed");
    return Statement(p, sqlite3_finalize);
}
void bind_text(sqlite3_stmt *p, int i, const std::string &v) {
    if (sqlite3_bind_text(p, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        throw std::runtime_error("SQLite bind failed");
}
std::string col(sqlite3_stmt *p, int i) {
    const auto *v = sqlite3_column_text(p, i);
    return v ? reinterpret_cast<const char *>(v) : "";
}
} // namespace
MemoryStore::MemoryStore(const std::filesystem::path &path, size_t max_conversations)
    : max_conversations_(max_conversations) {
    if (max_conversations < 1 || max_conversations > 10000)
        throw std::runtime_error("Invalid memory retention limit");
    std::filesystem::create_directories(path.parent_path());
    const auto u8 = path.u8string();
    const std::string name(u8.begin(), u8.end());
    if (sqlite3_open_v2(name.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Cannot open SQLite history");
    }
    sqlite3_busy_timeout(db_, 3000);
    if (sqlite3_exec(db_,
                     "CREATE TABLE IF NOT EXISTS conversations(id TEXT PRIMARY KEY, project TEXT, model "
                     "TEXT, timestamp TEXT, messages TEXT NOT NULL)",
                     nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Cannot initialize history");
    }
}
MemoryStore::~MemoryStore() {
    if (db_)
        sqlite3_close(db_);
}
void MemoryStore::save(const Conversation &c) {
    Json rows = Json::array();
    for (const auto &m : c.messages)
        rows.push_back({{"role", m.role}, {"content", m.content}});
    std::lock_guard lock(mutex_);
    auto p = prepare(db_, "INSERT OR REPLACE INTO conversations VALUES(?,?,?,?,?)");
    bind_text(p.get(), 1, c.id);
    bind_text(p.get(), 2, c.project_id);
    bind_text(p.get(), 3, c.model);
    bind_text(p.get(), 4, c.timestamp);
    bind_text(p.get(), 5,
              Json{{"messages", rows}, {"transcript", c.transcript}, {"sensitive", c.sensitive}}.dump());
    if (sqlite3_step(p.get()) != SQLITE_DONE)
        throw std::runtime_error("Cannot save history");
    auto prune = prepare(db_, "DELETE FROM conversations WHERE id NOT IN (SELECT id FROM conversations "
                              "ORDER BY timestamp DESC,rowid DESC LIMIT ?)");
    if (sqlite3_bind_int64(prune.get(), 1, static_cast<sqlite3_int64>(max_conversations_)) != SQLITE_OK)
        throw std::runtime_error("Cannot bind history retention limit");
    if (sqlite3_step(prune.get()) != SQLITE_DONE)
        throw std::runtime_error("Cannot apply history retention");
}
Conversation MemoryStore::load(const std::string &id) {
    std::lock_guard lock(mutex_);
    auto p = prepare(db_, "SELECT project,model,timestamp,messages FROM conversations WHERE id=?");
    bind_text(p.get(), 1, id);
    if (sqlite3_step(p.get()) != SQLITE_ROW)
        throw std::runtime_error("Conversation not found");
    Conversation c;
    c.id = id;
    c.project_id = col(p.get(), 0);
    c.model = col(p.get(), 1);
    c.timestamp = col(p.get(), 2);
    const auto document = Json::parse(col(p.get(), 3));
    const auto &rows = document.is_array() ? document : document.at("messages");
    if (document.is_object()) {
        c.transcript = document.value("transcript", false);
        c.sensitive = document.value("sensitive", false);
    }
    for (const auto &row : rows)
        c.messages.push_back({row.at("role").get<std::string>(), row.at("content").get<std::string>()});
    return c;
}
std::vector<std::string> MemoryStore::list() {
    std::lock_guard lock(mutex_);
    auto p = prepare(db_, "SELECT id FROM conversations ORDER BY timestamp DESC");
    std::vector<std::string> out;
    int rc;
    while ((rc = sqlite3_step(p.get())) == SQLITE_ROW)
        out.push_back(col(p.get(), 0));
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Cannot list history");
    return out;
}
void MemoryStore::erase(const std::string &id) {
    std::lock_guard lock(mutex_);
    auto p = prepare(db_, "DELETE FROM conversations WHERE id=?");
    bind_text(p.get(), 1, id);
    if (sqlite3_step(p.get()) != SQLITE_DONE)
        throw std::runtime_error("Cannot delete history");
}
Conversation ConversationManager::create(const std::string &project) {
    Conversation c;
    c.id = new_id();
    c.project_id = project;
    c.timestamp = utc_now();
    store_.save(c);
    return c;
}
void ConversationManager::append(Conversation &c, const ChatMessage &message, const std::string &model) {
    c.messages.push_back(message);
    c.model = model;
    c.timestamp = utc_now();
    size_t total = 0;
    for (const auto &m : c.messages)
        total += m.content.size();
    while (c.messages.size() > 1 &&
           (total > config_.history_bytes || c.messages.size() > config_.max_history_messages)) {
        total -= c.messages.front().content.size();
        c.messages.erase(c.messages.begin());
    }
    if (!c.messages.empty())
        c.messages.back().content = utf8_prefix(c.messages.back().content, config_.history_bytes);
    store_.save(c);
}
} // namespace sayra
