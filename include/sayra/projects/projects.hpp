#pragma once
#include <map>
#include <sayra/core/config.hpp>
namespace sayra {
struct Project {
    std::string id;
    std::filesystem::path root;
};
class ProjectContextManager {
    Configuration config_;
    std::map<std::string, Project> projects_;
    std::string active_;
    void persist() const;
    std::filesystem::path checked(const Project &, const std::string &) const;

  public:
    explicit ProjectContextManager(Configuration);
    std::string add(const std::filesystem::path &root);
    void select(const std::string &id);
    std::string active() const {
        return active_;
    }
    std::vector<Project> projects() const;
    const Project &get(const std::string &id) const;
    std::vector<std::string> files(const std::string &id, std::stop_token = {}) const;
    std::string read(const std::string &id, const std::string &relative, std::stop_token = {}) const;
    Json search(const std::string &id, const std::string &text, std::stop_token = {}) const;
};
} // namespace sayra