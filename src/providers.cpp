#include <algorithm>
#include <cstdlib>
#include <map>
#include <sayra/models/providers.hpp>
#include <thread>
namespace sayra {
namespace {
Json parse_api_json(const std::string &text) {
    try {
        return Json::parse(text);
    } catch (const Json::exception &) {
        throw ProviderError("Некорректный JSON провайдера");
    }
}
std::string base(std::string url) {
    while (url.ends_with('/'))
        url.pop_back();
    return url;
}
bool cap(const Json &values, const std::string &name) {
    return values.is_array() && std::find(values.begin(), values.end(), name) != values.end();
}
Json messages(const std::vector<ChatMessage> &input, bool cloud) {
    Json out = Json::array();
    for (const auto &m : input) {
        Json row = {{"role", m.role}, {"content", m.content}};
        if (m.role == "tool") {
            if (cloud)
                row["tool_call_id"] = m.tool_call_id;
            else
                row["tool_name"] = m.tool_name;
        }
        if (!m.tool_calls.empty()) {
            row["tool_calls"] = Json::array();
            for (const auto &t : m.tool_calls) {
                Json function = {{"name", t.name},
                                 {"arguments", cloud ? Json(t.arguments.dump()) : t.arguments}};
                Json call = {{"function", function}};
                if (cloud) {
                    call["id"] = t.id;
                    call["type"] = "function";
                }
                row["tool_calls"].push_back(std::move(call));
            }
        }
        out.push_back(std::move(row));
    }
    return out;
}
ToolCall ollama_tool(const Json &tool) {
    const auto &f = tool.at("function");
    ToolCall out{tool.value("id", new_id()), f.at("name").get<std::string>(), f.at("arguments")};
    if (out.arguments.is_string())
        out.arguments = parse_api_json(out.arguments.get<std::string>());
    if (!out.arguments.is_object())
        throw ProviderError("Tool arguments must be an object");
    return out;
}
void require_http(const HttpResult &r) {
    if (r.status < 200 || r.status >= 300) {
        std::string text = "HTTP " + std::to_string(r.status);
        if (r.status == 401 || r.status == 403)
            text += ": проверьте ключ и права API";
        else if (r.status == 429)
            text += ": превышен лимит API";
        else if (r.status >= 500)
            text += ": сервер провайдера недоступен";
        // Never echo server bodies which might contain credentials or private input.
        throw ProviderError(text, r.status, r.status == 429 || r.status >= 500);
    }
}
} // namespace
HttpRequest json_request(const Configuration &c, const std::string &url, const Json *body) {
    HttpRequest r;
    r.url = url;
    r.connect_timeout = c.connect_timeout;
    r.timeout = c.request_timeout;
    r.headers = {"Content-Type: application/json; charset=utf-8"};
    if (body) {
        r.method = "POST";
        r.body = body->dump();
    }
    return r;
}
Json checked_json(const HttpResult &response) {
    require_http(response);
    auto doc = parse_api_json(response.body);
    if (!doc.is_object() || doc.contains("error"))
        throw ProviderError("Некорректный ответ API или ошибка модели");
    return doc;
}
std::vector<ModelInfo> OllamaProvider::discover(std::stop_token stop) {
    const auto root = base(config_.ollama_url);
    auto tags = checked_json(http_->perform(json_request(config_, root + "/api/tags"), {}, stop));
    if (!tags.at("models").is_array())
        throw ProviderError("Invalid Ollama models array");
    Json loaded = Json::array();
    try {
        loaded = checked_json(http_->perform(json_request(config_, root + "/api/ps"), {}, stop)).at("models");
    } catch (const ProviderError &e) {
        if (e.cancelled)
            throw;
    }
    std::vector<ModelInfo> result;
    for (const auto &row : tags.at("models")) {
        check_cancelled(stop);
        ModelInfo m;
        m.name = row.at("name").get<std::string>();
        m.provider = id();
        m.available = true;
        for (const auto &l : loaded)
            if (l.value("name", "") == m.name)
                m.loaded = true;
        Json metadata = row;
        try {
            Json body = {{"model", m.name}};
            metadata =
                checked_json(http_->perform(json_request(config_, root + "/api/show", &body), {}, stop));
        } catch (const ProviderError &e) {
            if (e.cancelled)
                throw;
        }
        const auto caps = metadata.value("capabilities", row.value("capabilities", Json::array()));
        m.embedding = cap(caps, "embedding");
        m.chat = cap(caps, "completion");
        m.tools = cap(caps, "tools");
        // Explicit identity is a conservative fallback for older servers without capabilities.
        if (lower_utf8(m.name).starts_with("bge-m3:")) {
            m.embedding = true;
            m.chat = false;
        }
        const auto info = metadata.value("model_info", Json::object());
        for (auto it = info.begin(); it != info.end(); ++it)
            if (it.key().ends_with(".context_length") && it.value().is_number_unsigned())
                m.context = it.value().get<size_t>();
        result.push_back(std::move(m));
    }
    return result;
}
GenerationResult OllamaProvider::generate(const GenerationRequest &req, DeltaCallback delta,
                                          std::stop_token stop) {
    Json payload = {{"model", req.model},
                    {"messages", messages(req.messages, false)},
                    {"stream", req.options.stream},
                    {"keep_alive", req.options.keep_alive},
                    {"options",
                     {{"num_ctx", req.options.context},
                      {"num_predict", req.options.max_tokens},
                      {"temperature", req.options.temperature}}}};
    if (!req.tools.empty())
        payload["tools"] = req.tools;
    GenerationResult out;
    out.model = req.model;
    out.provider = id();
    bool done = false;
    auto consume = [&](const Json &j) {
        check_cancelled(stop);
        if (j.contains("error"))
            throw ProviderError("Ollama сообщил об ошибке модели");
        if (j.contains("model") && j.at("model") != req.model)
            throw ProviderError("Ollama returned another model");
        if (j.contains("message")) {
            const auto &m = j.at("message");
            const auto text = m.value("content", "");
            out.text += text;
            if (!text.empty() && delta)
                delta(text, false);
            if (!m.value("thinking", "").empty() && delta)
                delta("Модель обрабатывает запрос", true);
            if (m.contains("tool_calls"))
                for (const auto &t : m.at("tool_calls"))
                    out.tool_calls.push_back(ollama_tool(t));
        }
        if (j.value("done", false)) {
            done = true;
            out.metrics.output_tokens = j.value("eval_count", size_t{0});
            if (j.value("done_reason", "") == "length")
                throw ProviderError("Ответ обрезан лимитом токенов");
        }
    };
    StreamParser parser(false, [&](std::string line) { consume(parse_api_json(line)); });
    auto r =
        http_->perform(json_request(config_, base(config_.ollama_url) + "/api/chat", &payload),
                       req.options.stream ? ChunkCallback([&](std::string_view chunk) { parser.feed(chunk); })
                                          : ChunkCallback{},
                       stop);
    require_http(r);
    if (req.options.stream)
        parser.finish();
    else
        consume(checked_json(r));
    if (!done || (out.text.empty() && out.tool_calls.empty()))
        throw ProviderError("Незавершённый или пустой ответ Ollama");
    out.metrics.seconds = r.seconds;
    return out;
}
std::string OpenAICompatibleProvider::key() const {
    if (!config_.cloud_enabled)
        throw ProviderError("Облачный API выключен");
    if (!config_.cloud_url.starts_with("https://") || config_.cloud_url.find('@') != std::string::npos ||
        config_.cloud_url.find('?') != std::string::npos)
        throw ProviderError("Для облака задайте HTTPS base URL без ключей в URL");
    const auto value = environment_variable(config_.cloud_key_env);
    if (!value || value->empty())
        throw ProviderError("Отсутствует переменная окружения " + config_.cloud_key_env);
    const std::string token = *value;
    if (token.find_first_of("\r\n") != std::string::npos)
        throw ProviderError("Некорректный API key");
    return token;
}
std::vector<ModelInfo> OpenAICompatibleProvider::discover(std::stop_token stop) {
    if (!config_.cloud_enabled)
        return {};
    const auto token = key();
    auto req = json_request(config_, base(config_.cloud_url) + "/models");
    req.headers.push_back("Authorization: Bearer " + token);
    const auto doc = checked_json(http_->perform(req, {}, stop));
    std::vector<ModelInfo> out;
    for (const auto &item : doc.at("data")) {
        ModelInfo m;
        m.name = item.at("id").get<std::string>();
        m.provider = id();
        m.available = true;
        // /models does not specify chat/embedding/tool capabilities; configuration must declare them.
        out.push_back(std::move(m));
    }
    return out;
}
GenerationResult OpenAICompatibleProvider::generate(const GenerationRequest &req, DeltaCallback delta,
                                                    std::stop_token stop) {
    const auto token = key();
    Json body = {{"model", req.model},
                 {"messages", messages(req.messages, true)},
                 {"stream", req.options.stream},
                 {"max_tokens", req.options.max_tokens},
                 {"temperature", req.options.temperature}};
    if (!req.tools.empty())
        body["tools"] = req.tools;
    for (int attempt = 0;; ++attempt) {
        bool emitted = false;
        try {
            GenerationResult out;
            out.model = req.model;
            out.provider = id();
            bool done = false, finished = false;
            struct Partial {
                std::string id, name, args;
            };
            std::map<int, Partial> calls;
            auto consume = [&](const Json &j) {
                check_cancelled(stop);
                if (j.contains("error"))
                    throw ProviderError("Cloud generation error");
                if (j.contains("usage") && j.at("usage").is_object())
                    out.metrics.output_tokens = j.at("usage").value("completion_tokens", size_t{0});
                if (!j.contains("choices"))
                    return;
                for (const auto &choice : j.at("choices")) {
                    if (choice.value("index", 0) != 0)
                        continue;
                    const auto &part = choice.contains("delta") ? choice.at("delta") : choice.at("message");
                    if (part.contains("content") && part.at("content").is_string()) {
                        auto text = part.at("content").get<std::string>();
                        out.text += text;
                        if (!text.empty() && delta) {
                            emitted = true;
                            delta(text, false);
                        }
                    }
                    if (part.contains("tool_calls")) {
                        int fallback_index = 0;
                        for (const auto &t : part.at("tool_calls")) {
                            auto &call = calls[t.value("index", fallback_index++)];
                            if (t.contains("id"))
                                call.id = t.at("id").get<std::string>();
                            if (t.contains("function")) {
                                const auto &f = t.at("function");
                                call.name += f.value("name", "");
                                call.args += f.value("arguments", "");
                            }
                        }
                    }
                    if (choice.contains("finish_reason") && !choice.at("finish_reason").is_null()) {
                        const auto reason = choice.at("finish_reason").get<std::string>();
                        if (reason != "stop" && reason != "tool_calls")
                            throw ProviderError("Cloud response incomplete or filtered");
                        finished = true;
                    }
                }
            };
            StreamParser parser(true, [&](std::string frame) {
                if (frame == "[DONE]")
                    done = true;
                else
                    consume(parse_api_json(frame));
            });
            auto http = json_request(config_, base(config_.cloud_url) + "/chat/completions", &body);
            http.headers.push_back("Authorization: Bearer " + token);
            const auto r =
                http_->perform(http,
                               req.options.stream ? ChunkCallback([&](std::string_view c) { parser.feed(c); })
                                                  : ChunkCallback{},
                               stop);
            require_http(r);
            if (req.options.stream)
                parser.finish();
            else {
                consume(checked_json(r));
                done = true;
            }
            if (!done || !finished)
                throw ProviderError("Cloud stream ended unexpectedly");
            for (const auto &[index, call] : calls) {
                auto arguments = parse_api_json(call.args);
                if (call.id.empty() || call.name.empty() || !arguments.is_object())
                    throw ProviderError("Invalid cloud tool call");
                out.tool_calls.push_back({call.id, call.name, std::move(arguments)});
            }
            if (out.text.empty() && out.tool_calls.empty())
                throw ProviderError("Cloud response is empty");
            out.metrics.seconds = r.seconds;
            return out;
        } catch (const ProviderError &e) {
            if (e.cancelled || !e.retryable || emitted || attempt >= config_.retries)
                throw;
            // Bounded, cancellable backoff; never retry after visible output.
            for (int i = 0; i < (attempt + 1) * 10; ++i) {
                check_cancelled(stop);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}
void ModelRegistry::refresh(std::stop_token stop) {
    models_.clear();
    errors_.clear();
    for (const auto &p : providers_) {
        try {
            auto discovered = p->discover(stop);
            for (auto &m : discovered) {
                for (const auto &profile : config_.model_profiles)
                    if (profile.at("name") == m.name && profile.value("provider", "ollama") == m.provider) {
                        m.roles = profile.value("roles", std::vector<std::string>{});
                        m.priority = profile.value("priority", 0);
                        if (profile.contains("context"))
                            m.context = profile.at("context").get<size_t>();
                        if (profile.contains("chat"))
                            m.chat = profile.at("chat").get<bool>();
                        if (profile.contains("embedding"))
                            m.embedding = profile.at("embedding").get<bool>();
                        if (profile.contains("tools"))
                            m.tools = profile.at("tools").get<bool>();
                        if (profile.contains("quality"))
                            m.quality = profile.at("quality").get<double>();
                        if (profile.contains("speed"))
                            m.speed = profile.at("speed").get<double>();
                        if (profile.contains("cost_per_1k_tokens"))
                            m.cost = profile.at("cost_per_1k_tokens").get<double>();
                    }
                if (m.embedding)
                    m.chat = false;
                models_.push_back(std::move(m));
            }
        } catch (const ProviderError &e) {
            if (e.cancelled)
                throw;
            errors_.push_back(p->id() + ": " + e.what());
        } catch (const std::exception &) {
            errors_.push_back(p->id() + ": некорректные метаданные/профиль");
        }
    }
}
std::shared_ptr<IModelProvider> ModelRegistry::provider(const std::string &id) const {
    for (const auto &p : providers_)
        if (p->id() == id)
            return p;
    throw ProviderError("Unknown provider: " + id);
}
} // namespace sayra