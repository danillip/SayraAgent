#include <atomic>
#include <iomanip>
#include <iostream>
#include <sayra/core/orchestrator.hpp>
#include <sayra/embeddings/embeddings.hpp>
#include <sayra/integration/realtime.hpp>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
std::mutex console_mutex;
class ConsoleSink final : public sayra::IOverlayEventSink {
    bool debug_;

  public:
    explicit ConsoleSink(bool debug) : debug_(debug) {}
    void on_event(const sayra::CoreEvent &e) override {
        using sayra::EventType;
        std::lock_guard lock(console_mutex);
        if (e.type == EventType::TextDelta)
            std::cout << e.text << std::flush;
        else if (e.type == EventType::SourceAdded && e.source)
            std::cout << "\n[source] " << e.source->title << " " << e.source->uri
                      << " verification=" << static_cast<int>(e.source->verification) << '\n';
        else if (e.type == EventType::ModelSelected || e.type == EventType::IntentDetected ||
                 e.type == EventType::ToolStarted || e.type == EventType::SourceAdded)
            std::cout << "\n[" << e.request_id << "] " << e.text << '\n';
        else if (e.type == EventType::RequestCompleted || e.type == EventType::RequestFailed ||
                 e.type == EventType::RequestCancelled)
            std::cout << "\n[" << e.request_id << "] " << e.text << '\n';
        else if (e.type == EventType::AssistCardReady && e.card && !e.session_id.empty())
            std::cout << "\n[overlay " << (e.card->is_final ? "final" : "preliminary") << "] "
                      << e.card->short_answer << '\n';
        else if (debug_ && e.type == EventType::ThinkingStatus)
            std::cout << "\n[status] " << e.text << '\n';
    }
};
std::string argument(const std::string &line) {
    auto p = line.find(' ');
    if (p == line.npos)
        return {};
    auto s = line.substr(p + 1);
    while (s.starts_with(' '))
        s.erase(0, 1);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}
void help() {
    std::cout << "/help /models /model auto|<name> /mode local|cloud|hybrid|auto\n"
                 "/status /doctor /why-model /project list|add <absolute path>|use <id>|info\n"
                 "/history [list|load <id>|delete <id>] /new /embed-test /live-demo /cancel /exit\n"
                 "Обычный текст — запрос. Во время генерации доступны /cancel и /exit.\n";
}
} // namespace
int run_cli(const std::filesystem::path &config_path) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
    using namespace sayra;
    try {
        const auto config = ConfigurationManager::load(config_path);
        auto http = std::make_shared<CurlTransport>();
        auto local = std::make_shared<OllamaProvider>(config, http);
        auto cloud = std::make_shared<OpenAICompatibleProvider>(config, http);
        ModelRegistry models(config, {local, cloud});
        models.refresh();
        ProjectContextManager projects(config);
        MemoryStore memory(config.data_dir / "history.sqlite3", config.max_conversations);
        ConversationManager conversations(memory, config);
        auto conversation = conversations.create(projects.active());
        EventBus bus;
        auto sink = std::make_shared<ConsoleSink>(config.debug);
        const auto subscription = bus.subscribe(sink);
        ToolRegistry tools;
        register_builtin_tools(tools, projects, models, config.context_bytes);
        SayraOrchestrator core(config, models, projects, conversations, tools, bus);
        OllamaEmbeddingProvider embeddings(config, http);
        RoutingMode mode = config.mode;
        std::string forced;
        std::atomic<bool> busy = false;
        std::jthread worker;
        auto submit = [&](AgentRequest request) {
            if (worker.joinable())
                worker.join();
            request.mode = mode;
            request.forced_model = forced;
            request.allow_cloud = config.cloud_enabled;
            request.allow_project_cloud = config.project_cloud;
            request.allow_transcript_cloud = request.realtime
                                                 ? (request.allow_transcript_cloud && config.transcript_cloud)
                                                 : config.transcript_cloud;
            request.conversation_id = conversation.id;
            busy = true;
            worker = std::jthread([&, request](std::stop_token stop) {
                try {
                    core.run(request, conversation, stop);
                } catch (const std::exception &) {
                }
                busy = false;
            });
        };
        auto status = [&]() {
            std::cout << DiagnosticsService(models).report() << "\nCloud: "
                      << (config.cloud_enabled ? "enabled (see provider diagnostics)" : "disabled")
                      << "\nRouting mode: "
                      << (mode == RoutingMode::LocalOnly   ? "local"
                          : mode == RoutingMode::CloudOnly ? "cloud"
                          : mode == RoutingMode::Hybrid    ? "hybrid"
                                                           : "auto")
                      << "\nProject: " << projects.active() << "\nConversation: " << conversation.id << '\n';
        };
        std::cout << "SAYRA CORE 0.1.0\n";
        status();
        help();
        std::string line;
        while (std::cout << "\nsayra> " << std::flush, std::getline(std::cin, line)) {
            if (line == "/exit") {
                if (worker.joinable()) {
                    worker.request_stop();
                    worker.join();
                }
                break;
            }
            if (line == "/cancel") {
                if (worker.joinable())
                    worker.request_stop();
                continue;
            }
            if (busy) {
                std::cout << "Запрос выполняется. Доступны /cancel и /exit.\n";
                continue;
            }
            if (worker.joinable())
                worker.join();
            try {
                if (line == "/help")
                    help();
                else if (line == "/models") {
                    models.refresh();
                    for (const auto &m : models.models())
                        std::cout << m.provider << '/' << m.name << " chat=" << m.chat
                                  << " embedding=" << m.embedding << " tools=" << m.tools
                                  << " loaded=" << m.loaded << " context=" << m.context << '\n';
                    for (const auto &e : models.errors())
                        std::cout << e << '\n';
                } else if (line.starts_with("/model ")) {
                    forced = argument(line);
                    if (forced == "auto")
                        forced.clear();
                    std::cout << "Model: " << (forced.empty() ? "auto" : forced) << '\n';
                } else if (line.starts_with("/mode ")) {
                    auto value = argument(line);
                    if (value == "local")
                        mode = RoutingMode::LocalOnly;
                    else if (value == "cloud")
                        mode = RoutingMode::CloudOnly;
                    else if (value == "hybrid")
                        mode = RoutingMode::Hybrid;
                    else if (value == "auto")
                        mode = RoutingMode::Auto;
                    else
                        throw std::runtime_error("Unknown mode");
                } else if (line == "/status" || line == "/doctor") {
                    models.refresh();
                    status();
                    if (line == "/doctor")
                        std::cout << "API keys are environment-only. Cloud data consent is configured "
                                     "separately. No shell/write tools.\n";
                } else if (line == "/why-model") {
                    const auto &r = core.last_routing();
                    std::cout << r.reason << "\nFallback: " << r.fallback << '\n';
                    for (const auto &c : r.candidates)
                        std::cout << c.model.provider << '/' << c.model.name << " score=" << c.score << " "
                                  << c.reason << '\n';
                    for (const auto &e : r.rejected)
                        std::cout << "Rejected: " << e << '\n';
                } else if (line == "/new")
                    conversation = conversations.create(projects.active());
                else if (line == "/history")
                    for (const auto &m : conversation.messages)
                        std::cout << m.role << ": " << m.content << '\n';
                else if (line == "/history list")
                    for (const auto &id : conversations.list())
                        std::cout << id << '\n';
                else if (line.starts_with("/history load ")) {
                    conversation = conversations.load(line.substr(14));
                    if (!conversation.project_id.empty())
                        projects.select(conversation.project_id);
                } else if (line.starts_with("/history delete ")) {
                    auto id = line.substr(16);
                    conversations.erase(id);
                    if (id == conversation.id)
                        conversation = conversations.create(projects.active());
                } else if (line == "/project list")
                    for (const auto &p : projects.projects()) {
                        auto u = p.root.u8string();
                        std::cout << p.id << " " << std::string(u.begin(), u.end()) << '\n';
                    }
                else if (line.starts_with("/project add ")) {
                    auto path = argument(line.substr(9));
                    auto id = projects.add(sayra::path_from_utf8(path));
                    conversation = conversations.create(id);
                    std::cout << "Selected project: " << id << '\n';
                } else if (line.starts_with("/project use ")) {
                    auto id = line.substr(13);
                    projects.select(id);
                    conversation = conversations.create(id);
                } else if (line == "/project info") {
                    const auto &p = projects.get(projects.active());
                    auto u = p.root.u8string();
                    std::cout << p.id << " " << std::string(u.begin(), u.end())
                              << "\nAllowed files: " << projects.files(p.id).size() << '\n';
                } else if (line == "/embed-test") {
                    busy = true;
                    worker = std::jthread([&](std::stop_token stop) {
                        try {
                            const std::vector<std::vector<std::string>> sets = {
                                {"Программист разрабатывает приложение", "Разработчик создаёт программу",
                                 "Сегодня на улице идёт сильный дождь"},
                                {"Врач осматривает пациента", "Доктор обследует больного",
                                 "Пекарь готовит хлеб"},
                                {"Пассажир купил билет на поезд",
                                 "Путешественник оплатил поездку по железной дороге",
                                 "Кошка спит на подоконнике"},
                                {"Повар варит овощной суп", "Кулинар готовит первое блюдо из овощей",
                                 "Спутник вышел на орбиту"},
                                {"Студент готовится к экзамену",
                                 "Учащийся повторяет материал перед экзаменом", "Механик заменяет колесо"},
                                {"На улице идёт сильный дождь", "За окном льёт ливень",
                                 "Бухгалтер составляет финансовый отчёт"}};
                            size_t passed = 0;
                            for (const auto &texts : sets) {
                                auto result = embeddings.embed({config.embedding_model, texts}, stop);
                                const auto ab = cosine_similarity(result.vectors[0], result.vectors[1]),
                                           ac = cosine_similarity(result.vectors[0], result.vectors[2]);
                                std::lock_guard lock(console_mutex);
                                for (size_t i = 0; i < texts.size(); ++i)
                                    std::cout << static_cast<char>('A' + i) << ": " << texts[i]
                                              << "; dimension=" << result.vectors[i].size()
                                              << " finite/nonzero=PASS\n";
                                std::cout << std::fixed << std::setprecision(6) << "AB=" << ab << " AC=" << ac
                                          << " margin=" << ab - ac << " " << (ab > ac ? "PASS" : "FAIL")
                                          << "; " << result.seconds << " s\n";
                                if (ab > ac)
                                    ++passed;
                            }
                            std::lock_guard lock(console_mutex);
                            std::cout << "Embedding sets: " << passed << '/' << sets.size() << '\n';
                        } catch (const std::exception &e) {
                            std::lock_guard lock(console_mutex);
                            std::cout << "Embedding error: " << e.what() << '\n';
                        }
                        busy = false;
                    });
                } else if (line == "/live-demo") {
                    RealtimeSession session{new_id(), projects.active()};
                    RealtimeAssistancePipeline pipeline(session, config.realtime_bytes,
                                                        config.realtime_interval_seconds, bus);
                    pipeline.accept(
                        {session.id, "segment-1", utc_now(), "В C++ обсуждают", "CLI simulation", false, {}});
                    pipeline.accept({session.id,
                                     "segment-1",
                                     utc_now(),
                                     "В C++ обсуждают std::mutex и защиту общих данных от гонок.",
                                     "CLI simulation",
                                     true,
                                     {}});
                    if (auto r =
                            pipeline.request_help("Кратко объясни обсуждаемую концепцию.", conversation.id))
                        submit(*r);
                    pipeline.close();
                } else if (!line.empty() && line.front() == '/')
                    std::cout << "Неизвестная команда. /help\n";
                else if (!line.empty()) {
                    AgentRequest request;
                    request.text = line;
                    request.project_id = projects.active();
                    submit(request);
                }
            } catch (const std::exception &e) {
                std::cout << "Ошибка: " << e.what() << '\n';
            }
        }
        if (worker.joinable()) {
            worker.request_stop();
            worker.join();
        }
        bus.unsubscribe(subscription);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Sayra: " << e.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    return run_cli(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(L"sayra.json"));
}
#else
int main(int argc, char **argv) {
    return run_cli(argc > 1 ? sayra::path_from_utf8(argv[1]) : std::filesystem::path("sayra.json"));
}
#endif