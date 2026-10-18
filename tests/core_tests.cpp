#include <cmath>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <sayra/core/orchestrator.hpp>
#include <sayra/embeddings/embeddings.hpp>
#include <sayra/integration/realtime.hpp>
using namespace sayra;
namespace {
void require(bool condition, const std::string &why) {
    if (!condition)
        throw std::runtime_error(why);
}
template <class F> void throws(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Expected exception");
}
struct TestEnvironment {
    std::string name = "SAYRA_FAKE_HTTP_TEST_KEY", previous;
    bool existed = false;
    TestEnvironment() {
        const auto p = environment_variable(name);
        if (p) {
            existed = true;
            previous = *p;
        }
        set("offline-test-placeholder");
    }
    void set(const std::string &value) {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        if (value.empty())
            unsetenv(name.c_str());
        else
            setenv(name.c_str(), value.c_str(), 1);
#endif
    }
    ~TestEnvironment() {
        set(existed ? previous : "");
    }
};
struct TemporaryDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / sayra::path_from_utf8("sayra-tests-" + new_id());
    TemporaryDirectory() {
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
class FakeHttp final : public IHttpTransport {
  public:
    std::deque<HttpResult> replies;
    std::vector<HttpRequest> requests;
    std::vector<std::string> chunks;
    bool unavailable = false;
    HttpResult perform(const HttpRequest &r, ChunkCallback callback, std::stop_token stop) override {
        check_cancelled(stop);
        requests.push_back(r);
        if (unavailable)
            throw ProviderError("offline", 0, true);
        if (replies.empty())
            throw std::runtime_error("No fake response queued");
        auto result = replies.front();
        replies.pop_front();
        if (callback && result.status == 200) {
            for (const auto &c : chunks) {
                check_cancelled(stop);
                callback(c);
            }
            check_cancelled(stop);
        }
        return result;
    }
};
class FakeProvider final : public IModelProvider {
  public:
    bool loop = false, fail = false;
    int generations = 0;
    std::vector<GenerationRequest> seen;
    std::string id() const override {
        return "ollama";
    }
    std::vector<ModelInfo> discover(std::stop_token stop) override {
        check_cancelled(stop);
        ModelInfo m;
        m.name = "fake-chat";
        m.provider = id();
        m.chat = true;
        m.tools = true;
        m.available = true;
        m.context = 8192;
        return {m};
    }
    GenerationResult generate(const GenerationRequest &r, DeltaCallback delta,
                              std::stop_token stop) override {
        check_cancelled(stop);
        ++generations;
        seen.push_back(r);
        if (fail)
            throw ProviderError("model offline", 0, true);
        GenerationResult out;
        out.model = r.model;
        out.provider = id();
        if (loop)
            out.tool_calls.push_back({new_id(), "get_runtime_status", Json::object()});
        else {
            out.text = "Fake provider output for isolated tests";
            if (delta)
                delta(out.text, false);
        }
        return out;
    }
};
struct Sink final : IOverlayEventSink {
    std::vector<CoreEvent> events;
    void on_event(const CoreEvent &e) override {
        events.push_back(e);
    }
};
ModelInfo model(std::string name, std::string provider = "ollama") {
    ModelInfo m;
    m.name = std::move(name);
    m.provider = std::move(provider);
    m.chat = true;
    m.available = true;
    m.context = 16000;
    return m;
}
struct CoreFixture {
    TemporaryDirectory temp;
    Configuration config;
    std::shared_ptr<FakeProvider> provider = std::make_shared<FakeProvider>();
    std::unique_ptr<ModelRegistry> models;
    std::unique_ptr<ProjectContextManager> projects;
    std::unique_ptr<MemoryStore> memory;
    std::unique_ptr<ConversationManager> history;
    ToolRegistry tools;
    EventBus events;
    std::unique_ptr<SayraOrchestrator> core;
    Conversation conversation;
    CoreFixture() {
        config.data_dir = temp.path / "data";
        config.generation.context = 8192;
        models =
            std::make_unique<ModelRegistry>(config, std::vector<std::shared_ptr<IModelProvider>>{provider});
        models->refresh();
        projects = std::make_unique<ProjectContextManager>(config);
        memory = std::make_unique<MemoryStore>(config.data_dir / "history.sqlite3");
        history = std::make_unique<ConversationManager>(*memory, config);
        conversation = history->create();
        register_builtin_tools(tools, *projects, *models, config.context_bytes);
        core = std::make_unique<SayraOrchestrator>(config, *models, *projects, *history, tools, events);
    }
};
} // namespace
int main() {
    std::vector<std::pair<std::string, std::function<void()>>> tests;
    auto add = [&](std::string name, std::function<void()> test) {
        tests.emplace_back(std::move(name), std::move(test));
    };
    add("casual classification", [] {
        AgentRequest r;
        r.text = "Привет, как дела?";
        require(IntentClassifier{}.classify(r, {}).has(TaskType::CasualConversation), "casual");
    });
    add("coding classification", [] {
        AgentRequest r;
        r.text = "Напиши многопоточный сервер на C++20";
        auto d = IntentClassifier{}.classify(r, {});
        require(d.has(TaskType::Coding) && d.has(TaskType::ComplexReasoning), "multi intent");
    });
    add("project classification", [] {
        AgentRequest r;
        r.text = "Объясни файл main.cpp из проекта";
        r.project_id = "p";
        require(IntentClassifier{}.classify(r, {}).has(TaskType::ProjectAnalysis), "project");
    });
    add("freshness classification", [] {
        AgentRequest r;
        r.text = "Какая последняя версия Unreal Engine и что изменилось?";
        require(IntentClassifier{}.classify(r, {}).needs_web, "web");
    });
    add("configured coding preference", [] {
        Configuration c;
        AgentRequest r;
        r.text = "C++";
        auto a = model("general"), b = model("special");
        b.roles = {"coding"};
        auto d = ModelRouter(c).route(r, IntentClassifier{}.classify(r, {}), {a, b});
        require(d.selected.name == "special", "coding route");
    });
    add("specialist fallback", [] {
        AgentRequest r;
        r.text = "C++";
        auto d =
            ModelRouter(Configuration{}).route(r, IntentClassifier{}.classify(r, {}), {model("general")});
        require(d.fallback && d.selected.name == "general", "fallback");
    });
    add("Ollama unavailable", [] {
        auto h = std::make_shared<FakeHttp>();
        h->unavailable = true;
        ModelRegistry registry(Configuration{}, {std::make_shared<OllamaProvider>(Configuration{}, h)});
        registry.refresh();
        require(registry.models().empty() && !registry.errors().empty(), "offline diagnostics");
    });
    add("missing API key", [] {
        Configuration c;
        c.cloud_enabled = true;
        c.cloud_url = "https://example.invalid/v1";
        c.cloud_key_env = "SAYRA_TEST_INTENTIONALLY_MISSING_KEY_9D2F";
        auto h = std::make_shared<FakeHttp>();
        OpenAICompatibleProvider p(c, h);
        throws([&] { p.discover({}); });
        require(h->requests.empty(), "must not send without key");
    });
    add("cloud disabled before HTTP", [] {
        auto h = std::make_shared<FakeHttp>();
        OpenAICompatibleProvider p(Configuration{}, h);
        GenerationRequest r;
        r.model = "cloud";
        throws([&] { p.generate(r, {}, {}); });
        require(h->requests.empty(), "disabled cloud called network");
    });
    add("router privacy and cost", [] {
        Configuration c;
        c.cloud_enabled = true;
        c.max_cloud_cost = 1;
        auto m = model("remote", "cloud");
        m.cost = 0.1;
        AgentRequest r;
        r.mode = RoutingMode::CloudOnly;
        r.text = "Привет";
        auto intent = IntentClassifier{}.classify(r, {});
        throws([&] { ModelRouter(c).route(r, intent, {m}); });
        r.allow_cloud = true;
        require(ModelRouter(c).route(r, intent, {m}).selected.provider == "cloud", "authorized cloud");
        r.project_id = "private-project";
        throws([&] { ModelRouter(c).route(r, intent, {m}); });
    });
    add("NDJSON arbitrary byte boundaries", [] {
        std::vector<std::string> frames;
        StreamParser parser(false, [&](std::string s) { frames.push_back(s); });
        const std::string stream = "{\"message\":{\"content\":\"Привет\"}}\r\n{\"done\":true}\n";
        for (char c : stream)
            parser.feed(std::string(1, c));
        parser.finish();
        require(frames.size() == 2 && Json::parse(frames[0]).at("message").at("content") == "Привет",
                "NDJSON boundary");
    });
    add("SSE arbitrary chunks and multiline", [] {
        std::vector<std::string> frames;
        StreamParser p(true, [&](std::string s) { frames.push_back(s); });
        std::string s = ": comment\r\ndata: {\"x\":\r\ndata: 1}\r\n\r\ndata: [DONE]\n\n";
        for (size_t i = 0; i < s.size(); i += 3)
            p.feed(std::string_view(s).substr(i, 3));
        p.finish();
        require(frames.size() == 2 && Json::parse(frames[0]).at("x") == 1 && frames[1] == "[DONE]", "SSE");
    });
    add("Ollama streaming and hidden thinking", [] {
        auto h = std::make_shared<FakeHttp>();
        h->replies.push_back({200, "", 0.1});
        h->chunks = {"{\"model\":\"m\",\"message\":{\"thinking\":\"SECRET THOUGHT\",\"content\":\"Hi\"}}\n",
                     "{\"done\":true,\"message\":{\"content\":\"!\"}}\n"};
        OllamaProvider p(Configuration{}, h);
        GenerationRequest r;
        r.model = "m";
        std::string text;
        auto result = p.generate(r,
                                 [&](const auto &s, bool thinking) {
                                     if (!thinking)
                                         text += s;
                                 },
                                 {});
        require(text == "Hi!" && result.text == text, "thinking exposed");
    });
    add("OpenAI SSE fragmented tool arguments", [] {
        TestEnvironment env;
        Configuration c;
        c.cloud_enabled = true;
        c.cloud_url = "https://example.invalid/v1";
        c.cloud_key_env = env.name;
        auto h = std::make_shared<FakeHttp>();
        h->replies.push_back({200, "", 0.1});
        const std::string stream =
            "data: "
            "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call-1\","
            "\"function\":{\"name\":\"read_project_file\",\"arguments\":\"{\\\"path\\\":\"}}]},\"finish_"
            "reason\":null}]}\n\n"
            "data: "
            "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":"
            "\"\\\"main.cpp\\\"}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\ndata: [DONE]\n\n";
        for (size_t i = 0; i < stream.size(); i += 7)
            h->chunks.push_back(stream.substr(i, 7));
        OpenAICompatibleProvider p(c, h);
        GenerationRequest r;
        r.model = "test-model";
        auto out = p.generate(r, {}, {});
        require(out.tool_calls.size() == 1 && out.tool_calls[0].arguments.at("path") == "main.cpp",
                "SSE tool aggregation");
    });
    add("Cloud HTTP401 has no retry", [] {
        TestEnvironment env;
        Configuration c;
        c.cloud_enabled = true;
        c.cloud_url = "https://example.invalid/v1";
        c.cloud_key_env = env.name;
        auto h = std::make_shared<FakeHttp>();
        h->replies.push_back({401, "private server body", 0});
        OpenAICompatibleProvider p(c, h);
        throws([&] { p.generate(GenerationRequest{}, {}, {}); });
        require(h->requests.size() == 1, "401 retried");
    });
    add("unexpected Ollama stream end", [] {
        auto h = std::make_shared<FakeHttp>();
        h->replies.push_back({200, "", 0});
        h->chunks = {"{\"message\":{\"content\":\"unfinished\"}}\n"};
        OllamaProvider p(Configuration{}, h);
        throws([&] { p.generate(GenerationRequest{}, {}, {}); });
    });
    add("cancel during streaming", [] {
        auto h = std::make_shared<FakeHttp>();
        h->replies.push_back({200, "", 0});
        h->chunks = {"{\"message\":{\"content\":\"a\"}}\n", "{\"done\":true}\n"};
        OllamaProvider p(Configuration{}, h);
        std::stop_source stop;
        throws([&] {
            p.generate(
                GenerationRequest{}, [&](const auto &, bool) { stop.request_stop(); }, stop.get_token());
        });
    });
    add("tool loop is bounded", [] {
        CoreFixture f;
        f.provider->loop = true;
        AgentRequest r;
        r.text = "Помоги";
        throws([&] { f.core->run(r, f.conversation, {}); });
        require(f.provider->generations <= f.config.max_iterations, "unbounded agent");
    });
    add("project traversal and secret exclusion", [] {
        TemporaryDirectory t;
        Configuration c;
        c.data_dir = t.path / "data";
        auto root = t.path / "project";
        std::filesystem::create_directories(root);
        std::ofstream(root / "main.cpp") << "int value=7;";
        std::ofstream(root / ".env") << "private";
        std::ofstream(t.path / "outside.txt") << "outside";
        ProjectContextManager p(c);
        auto id = p.add(root);
        require(p.read(id, "main.cpp") == "int value=7;", "file read");
        throws([&] { p.read(id, "../outside.txt"); });
        throws([&] { p.read(id, ".env"); });
        throws([&] { p.read(id, "main.cpp:stream"); });
        require(p.files(id).size() == 1, "secret listed");
    });
    add("binary files rejected", [] {
        TemporaryDirectory t;
        Configuration c;
        c.data_dir = t.path / "data";
        std::ofstream f(t.path / "binary.txt", std::ios::binary);
        f.write("a\0b", 3);
        f.close();
        ProjectContextManager p(c);
        auto id = p.add(t.path);
        throws([&] { p.read(id, "binary.txt"); });
    });
    add("SQLite roundtrip and deletion", [] {
        TemporaryDirectory t;
        std::string id;
        {
            MemoryStore store(t.path / "history.sqlite3");
            Configuration c;
            ConversationManager h(store, c);
            auto conversation = h.create("p");
            conversation.transcript = true;
            id = conversation.id;
            h.append(conversation, {"user", "Привет"}, "model");
        }
        {
            MemoryStore store(t.path / "history.sqlite3");
            auto c = store.load(id);
            require(c.messages.size() == 1 && c.messages[0].content == "Привет" && c.transcript,
                    "history restore");
            store.erase(id);
            throws([&] { store.load(id); });
        }
    });
    add("manual unknown model", [] {
        AgentRequest r;
        r.forced_model = "unknown";
        throws([&] { ModelRouter(Configuration{}).route(r, IntentDecision{}, {model("known")}); });
    });
    add("embedding finite nonzero validation", [] {
        throws([] { normalized_vector({0, 0}); });
        throws([] { normalized_vector({1, std::numeric_limits<double>::infinity()}); });
        throws([] { normalized_vector({}); });
        require(normalized_vector({1e308, 1e308}).size() == 2, "stable normalization");
    });
    add("cosine mathematical fixture", [] {
        require(std::abs(cosine_similarity({1, 0}, {1, 0}) - 1) < 1e-12, "same");
        require(std::abs(cosine_similarity({1, 0}, {0, 1})) < 1e-12, "orthogonal");
        require(cosine_similarity({1, 1}, {1, 0.9}) > cosine_similarity({1, 1}, {-1, 0}), "ordered");
    });
    add("BGE-M3 real shape parser with fake response", [] {
        auto h = std::make_shared<FakeHttp>();
        Json vector = std::vector<double>(1024, 0.01);
        h->replies.push_back(
            {200, Json{{"model", "bge-m3:latest"}, {"embeddings", Json::array({vector})}}.dump(), 0.1});
        OllamaEmbeddingProvider p(Configuration{}, h);
        require(p.embed({"bge-m3:latest", {"text"}}, {}).vectors[0].size() == 1024, "1024");
        h->replies.push_back(
            {200, Json{{"model", "bge-m3:latest"}, {"embeddings", Json::array({Json::array({1, 2})})}}.dump(),
             0});
        throws([&] { p.embed({"bge-m3:latest", {"text"}}, {}); });
    });
    add("transcript partial final and dedup", [] {
        EventBus bus;
        auto sink = std::make_shared<Sink>();
        bus.subscribe(sink);
        RealtimeSession s{"s", ""};
        RealtimeAssistancePipeline p(s, 1024, 5, bus);
        p.accept({"s", "1", utc_now(), "предварительный", "fake", false, {}});
        require(!p.request_help("explain", "c"), "partial must not trigger model");
        p.accept({"s", "1", utc_now(), "окончательный текст", "fake", true, {}});
        auto r = p.request_help("explain", "c");
        require(r && r->text.find("окончательный") != std::string::npos &&
                    r->text.find("предварительный") == std::string::npos,
                "final context");
        require(!p.request_help("explain", "c"), "dedup");
        require(!sink->events.empty() && sink->events[0].card && !sink->events[0].card->is_final,
                "preliminary marker");
        p.close();
        throws([&] { p.accept({"s", "2", utc_now(), "late", "fake", true, {}}); });
    });
    add("overlay event identifiers and unverified web", [] {
        CoreFixture f;
        auto sink = std::make_shared<Sink>();
        auto token = f.events.subscribe(sink);
        AgentRequest r;
        r.request_id = "request-42";
        r.session_id = "session-1";
        r.text = "Какие последние новости C++?";
        auto result = f.core->run(r, f.conversation, {});
        require(result.verification == VerificationStatus::Unverified && result.sources.empty() &&
                    result.text.find("не проверен") != std::string::npos,
                "web honesty");
        bool card = false, delta = false, done = false;
        for (const auto &e : sink->events) {
            require(e.request_id == r.request_id, "request id");
            if (e.card)
                card = e.card->is_final;
            if (e.type == EventType::TextDelta)
                delta = true;
            if (e.type == EventType::RequestCompleted)
                done = true;
        }
        require(card && delta && done, "overlay events");
        f.events.unsubscribe(token);
    });
    add("tool argument and call budget", [] {
        CoreFixture f;
        ToolExecutor executor(f.tools, PermissionPolicy{}, 2048);
        int budget = 1;
        auto result =
            executor.execute({"1", "get_runtime_status", {{"unexpected", "x"}}}, {"r", "", {}}, budget);
        require(!result.success && budget == 0, "argument validation");
        throws([&] { executor.execute({"2", "get_runtime_status", Json::object()}, {"r", "", {}}, budget); });
    });
    size_t passed = 0;
    for (const auto &[name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    }
    std::cout << passed << '/' << tests.size() << " passed\n";
    return passed == tests.size() ? 0 : 1;
}