#pragma once
#include <map>
#include <sayra/models/providers.hpp>
#include <sayra/projects/projects.hpp>
namespace sayra {
enum class ToolRisk { ReadOnly, RequiresConfirmation };
struct ToolDescriptor {
    std::string name, description;
    Json arguments_schema;
    ToolRisk risk = ToolRisk::ReadOnly;
    std::vector<std::string> permissions;
};
struct ToolContext {
    std::string request_id, project_id;
    std::stop_token stop;
};
struct ToolExecutionResult {
    bool success = false;
    Json data;
    std::string error;
};
struct PermissionPolicy {
    bool project_read = true;
    // Stage 1 deliberately denies dangerous tools. A future UI can implement confirmation.
    std::function<bool(const ToolDescriptor &, const ToolCall &)> confirm;
    bool permits(const ToolDescriptor &, const ToolContext &) const;
};
class ITool {
  public:
    virtual ~ITool() = default;
    virtual ToolDescriptor descriptor() const = 0;
    virtual ToolExecutionResult execute(const Json &, const ToolContext &) = 0;
};
class ToolRegistry {
    std::map<std::string, std::shared_ptr<ITool>> tools_;

  public:
    void add(std::shared_ptr<ITool> tool);
    std::shared_ptr<ITool> get(const std::string &) const;
    Json schemas(const ToolContext &, const PermissionPolicy &) const;
};
class ToolExecutor {
    ToolRegistry &registry_;
    PermissionPolicy policy_;
    size_t max_bytes_;

  public:
    ToolExecutor(ToolRegistry &r, PermissionPolicy p, size_t max_bytes)
        : registry_(r), policy_(std::move(p)), max_bytes_(max_bytes) {}
    ToolExecutionResult execute(const ToolCall &, const ToolContext &, int &budget);
};
void register_builtin_tools(ToolRegistry &, ProjectContextManager &, ModelRegistry &, size_t max_bytes);
} // namespace sayra