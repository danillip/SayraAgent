#include <algorithm>
#include <fstream>
#include <memory>
#include <sayra/projects/projects.hpp>
#include <set>
#ifdef _WIN32
#include <windows.h>
#endif
namespace sayra {
namespace {
std::string path_text(const std::filesystem::path &p) {
    auto s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}
bool linklike(const std::filesystem::path &p) {
#ifdef _WIN32
    const auto a = GetFileAttributesW(p.c_str());
    return a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return std::filesystem::is_symlink(std::filesystem::symlink_status(p));
#endif
}
bool allowed(const std::filesystem::path &relative) {
    static const std::set<std::string> blocked{
        ".git",  ".vs",     ".codex", ".agents",         ".aws",         ".ssh", "build", "out", "x64",
        "debug", "release", "tools",  "vcpkg_installed", "node_modules", "data"};
    for (const auto &part : relative) {
        const auto s = lower_utf8(path_text(part));
        if (blocked.contains(s) || s.starts_with('.') || s.find("secret") != s.npos ||
            s.find("credential") != s.npos || s.find("token") != s.npos || s.find("password") != s.npos)
            return false;
    }
    static const std::set<std::string> extensions{".h",        ".hpp",     ".c",    ".cpp",   ".cc",  ".cxx",
                                                  ".txt",      ".md",      ".json", ".cmake", ".ini", ".cfg",
                                                  ".uproject", ".uplugin", ".cs",   ".yaml",  ".yml"};
    return extensions.contains(lower_utf8(path_text(relative.extension()))) ||
           lower_utf8(path_text(relative.filename())) == "cmakelists.txt";
}
} // namespace
ProjectContextManager::ProjectContextManager(Configuration c) : config_(std::move(c)) {
    auto source = config_.projects;
    const auto state = config_.data_dir / "projects.json";
    if (std::filesystem::exists(state)) {
        std::ifstream f(state);
        const auto j = Json::parse(f);
        source = j.at("projects");
        active_ = j.value("active", "");
    } else
        active_ = config_.active_project;
    for (const auto &row : source) {
        auto root =
            std::filesystem::weakly_canonical(sayra::path_from_utf8(row.at("path").get<std::string>()));
        if (!std::filesystem::is_directory(root) || linklike(root))
            continue;
        auto id = row.at("id").get<std::string>();
        projects_[id] = {id, root};
    }
    if (!projects_.contains(active_))
        active_.clear();
}
void ProjectContextManager::persist() const {
    std::filesystem::create_directories(config_.data_dir);
    Json rows = Json::array();
    for (const auto &[id, p] : projects_)
        rows.push_back({{"id", id}, {"path", path_text(p.root)}});
    std::ofstream f(config_.data_dir / "projects.json", std::ios::binary | std::ios::trunc);
    if (!f || !(f << Json{{"projects", rows}, {"active", active_}}.dump(2)))
        throw std::runtime_error("Cannot save project settings");
}
std::string ProjectContextManager::add(const std::filesystem::path &path) {
    if (!path.is_absolute())
        throw std::runtime_error("Укажите абсолютный путь проекта");
    auto root = std::filesystem::canonical(path);
    if (!std::filesystem::is_directory(root) || linklike(path))
        throw std::runtime_error("Разрешается обычный каталог проекта");
    for (const auto &[id, p] : projects_)
        if (p.root == root)
            return id;
    auto id = new_id();
    projects_[id] = {id, root};
    active_ = id;
    persist();
    return id;
}
void ProjectContextManager::select(const std::string &id) {
    get(id);
    active_ = id;
    persist();
}
std::vector<Project> ProjectContextManager::projects() const {
    std::vector<Project> out;
    for (const auto &[id, p] : projects_)
        out.push_back(p);
    return out;
}
const Project &ProjectContextManager::get(const std::string &id) const {
    auto it = projects_.find(id);
    if (it == projects_.end())
        throw std::runtime_error("Проект не выбран/не зарегистрирован");
    return it->second;
}
std::filesystem::path ProjectContextManager::checked(const Project &p, const std::string &name) const {
    auto rel = sayra::path_from_utf8(name);
    if (rel.empty() || rel.is_absolute() || rel.has_root_name() || name.find(':') != name.npos ||
        !allowed(rel))
        throw std::runtime_error("Путь запрещён политикой проекта");
    auto current = p.root;
    for (const auto &part : rel) {
        if (part == ".." || part == ".")
            throw std::runtime_error("Path traversal запрещён");
        current /= part;
        if (linklike(current))
            throw std::runtime_error("Ссылки/reparse points запрещены");
    }
    const auto real = std::filesystem::canonical(current);
    auto root_it = p.root.begin(), file_it = real.begin();
    for (; root_it != p.root.end(); ++root_it, ++file_it)
        if (file_it == real.end() || lower_utf8(path_text(*root_it)) != lower_utf8(path_text(*file_it)))
            throw std::runtime_error("Файл вне проекта");
    if (!std::filesystem::is_regular_file(real))
        throw std::runtime_error("Не обычный файл");
    return real;
}
std::vector<std::string> ProjectContextManager::files(const std::string &id, std::stop_token stop) const {
    const auto &p = get(id);
    std::vector<std::string> out;
    auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(p.root, options), end; it != end; ++it) {
        check_cancelled(stop);
        const auto rel = it->path().lexically_relative(p.root);
        const auto name = path_text(rel);
        if (linklike(it->path())) {
            if (it->is_directory())
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory()) {
            if (!allowed(rel / "probe.txt"))
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file() && allowed(rel) && it->file_size() <= config_.file_bytes)
            out.push_back(name);
        if (out.size() >= config_.max_files)
            break;
    }
    std::sort(out.begin(), out.end());
    return out;
}
std::string ProjectContextManager::read(const std::string &id, const std::string &relative,
                                        std::stop_token stop) const {
    check_cancelled(stop);
    const auto file = checked(get(id), relative);
    if (std::filesystem::file_size(file) > config_.file_bytes)
        throw std::runtime_error("Файл превышает лимит размера");
    std::string text;
#ifdef _WIN32
    // Validate the opened handle, not only a path checked before opening (TOCTOU).
    HANDLE raw = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Невозможно открыть файл проекта");
    struct CloseHandleDeleter {
        void operator()(void *p) const noexcept {
            CloseHandle(p);
        }
    };
    std::unique_ptr<void, CloseHandleDeleter> handle(raw);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(raw, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        info.nNumberOfLinks > 1)
        throw std::runtime_error("Reparse points, directories and hard links are forbidden");
    std::wstring final_name(32768, L'\0');
    DWORD length = GetFinalPathNameByHandleW(raw, final_name.data(), static_cast<DWORD>(final_name.size()),
                                             FILE_NAME_NORMALIZED);
    if (!length || length >= final_name.size())
        throw std::runtime_error("Cannot validate opened file path");
    final_name.resize(length);
    if (final_name.starts_with(L"\\\\?\\UNC\\"))
        final_name = L"\\\\" + final_name.substr(8);
    else if (final_name.starts_with(L"\\\\?\\"))
        final_name.erase(0, 4);
    const auto actual = lower_utf8(path_text(std::filesystem::path(final_name)));
    const auto expected = lower_utf8(path_text(file));
    if (actual != expected)
        throw std::runtime_error("Opened file changed location or escaped project");
    text.resize(config_.file_bytes + 1);
    DWORD read_bytes = 0;
    if (!ReadFile(raw, text.data(), static_cast<DWORD>(text.size()), &read_bytes, nullptr))
        throw std::runtime_error("Cannot read project file");
    text.resize(read_bytes);
#else
    std::ifstream f(file, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open project file");
    text.resize(config_.file_bytes + 1);
    f.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(f.gcount()));
#endif
    if (text.size() > config_.file_bytes || text.find('\0') != text.npos)
        throw std::runtime_error("Большой/бинарный файл запрещён");
    (void)Json(text).dump();
    check_cancelled(stop);
    return text; // validates UTF-8 without treating file data as JSON
}
Json ProjectContextManager::search(const std::string &id, const std::string &query,
                                   std::stop_token stop) const {
    if (query.empty() || query.size() > 512)
        throw std::runtime_error("Поисковая строка: 1..512 байт");
    Json result = Json::array();
    size_t bytes = 0;
    for (const auto &name : files(id, stop)) {
        try {
            const auto text = read(id, name, stop);
            const auto pos = text.find(query);
            if (pos == text.npos)
                continue;
            const auto excerpt = utf8_prefix(text.substr(pos), 512);
            bytes += name.size() + excerpt.size();
            if (bytes > config_.context_bytes)
                break;
            result.push_back({{"path", name}, {"excerpt", excerpt}});
        } catch (const ProviderError &e) {
            if (e.cancelled)
                throw;
        } catch (const std::exception &) {
        }
    }
    return result;
}
} // namespace sayra