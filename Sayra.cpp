#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h>
#endif
using nlohmann::json;
namespace {
constexpr const char* server = "http://127.0.0.1:11434";
struct Response {
    CURLcode code = CURLE_OK;
    long status = 0;
    double seconds = 0;
    std::string body, error;
};
size_t receive(char* data, size_t size, size_t count, void* user) noexcept {
    try {
        if (size && count > (std::numeric_limits<size_t>::max)() / size) return 0;
        const size_t bytes = size * count;
        constexpr size_t limit = 8 * 1024 * 1024;
        auto& body = *static_cast<std::string*>(user);
        if (bytes > limit || body.size() > limit - bytes) return 0;
        body.append(data, bytes);
        return bytes;
    } catch (...) { return 0; }
}
Response request(const std::string& path, const json* payload = nullptr) {
    Response result;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle) throw std::runtime_error("Не удалось создать HTTP-запрос.");
    const std::string url = std::string(server) + path;
    const std::string body = payload ? payload->dump() : "";
    char error[CURL_ERROR_SIZE]{};
    auto set = [&](CURLoption option, auto value) {
        const auto code = curl_easy_setopt(handle.get(), option, value);
        if (code != CURLE_OK) throw std::runtime_error(curl_easy_strerror(code));
    };
    set(CURLOPT_URL, url.c_str());
    set(CURLOPT_NOPROXY, "*");
    set(CURLOPT_CONNECTTIMEOUT, 5L);
    set(CURLOPT_TIMEOUT, payload ? 300L : 10L);
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_WRITEFUNCTION, &receive);
    set(CURLOPT_WRITEDATA, &result.body);
    set(CURLOPT_ERRORBUFFER, error);
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        curl_slist_append(nullptr, "Content-Type: application/json; charset=utf-8"), curl_slist_free_all);
    if (!headers) throw std::runtime_error("Не удалось создать HTTP-заголовок.");
    if (payload) {
        set(CURLOPT_HTTPHEADER, headers.get());
        set(CURLOPT_POSTFIELDS, body.c_str());
        set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    }
    const auto start = std::chrono::steady_clock::now();
    result.code = curl_easy_perform(handle.get());
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &result.status);
    if (result.code != CURLE_OK) result.error = error[0] ? error : curl_easy_strerror(result.code);
    return result;
}
json validate(const Response& response) {
    if (response.code != CURLE_OK) throw std::runtime_error("Ошибка подключения/HTTP: " + response.error);
    if (response.status != 200) throw std::runtime_error("HTTP " + std::to_string(response.status) + ": " + response.body);
    const auto doc = json::parse(response.body);
    if (!doc.is_object()) throw std::runtime_error("Ожидался JSON-объект.");
    if (doc.contains("error")) throw std::runtime_error("Ollama: " + doc.at("error").dump());
    return doc;
}
void metrics(const Response& r, bool success) {
    std::cout << "\nHTTP Status: " << r.status << "\nResult: " << (success ? "SUCCESS" : "FAILED")
              << "\nResponse time: " << std::fixed << std::setprecision(2) << r.seconds << " s\n";
}
std::vector<std::string> model_names(const json& doc) {
    const auto& models = doc.at("models");
    if (!models.is_array()) throw std::runtime_error("models должен быть массивом.");
    std::vector<std::string> names;
    for (const auto& model : models) names.push_back(model.at("name").get<std::string>());
    return names;
}
std::string find_model(const std::vector<std::string>& names, const std::string& target) {
    std::vector<std::string> matches;
    for (const auto& name : names) {
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.substr(0, lower.find(':')) == target) matches.push_back(name);
    }
    if (matches.empty()) throw std::runtime_error("Модель не установлена. Выполните: ollama pull " + target);
    if (matches.size() == 1) return matches.front();
    for (const auto& name : matches) if (name.ends_with(":latest")) return name;
    throw std::runtime_error("Несколько тегов модели " + target + "; требуется тег latest.");
}
bool connection() {
    std::cout << "\n=== OLLAMA TEST ===\nServer: " << server << '\n';
    Response r;
    try {
        r = request("/api/tags");
        const auto names = model_names(validate(r));
        std::cout << "Installed models:\n";
        for (const auto& name : names) std::cout << "  " << name << '\n';
        metrics(r, true); return true;
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << '\n'; metrics(r, false); return false;
    }
}
std::vector<double> normalized_embedding(const json& vector) {
    if (!vector.is_array() || vector.size() != 1024)
        throw std::runtime_error("Для каждого вектора BGE-M3 ожидалась размерность 1024.");
    std::vector<double> values;
    double scale = 0;
    for (const auto& value : vector) {
        if (!value.is_number()) throw std::runtime_error("Вектор содержит нечисловое значение.");
        const double number = value.get<double>();
        if (!std::isfinite(number)) throw std::runtime_error("Вектор содержит неконечное значение.");
        values.push_back(number);
        scale = (std::max)(scale, std::abs(number));
    }
    if (scale == 0) throw std::runtime_error("Получен нулевой вектор.");
    double squared_norm = 0;
    for (auto& value : values) {
        value /= scale;
        squared_norm += value * value;
    }
    const double norm = std::sqrt(squared_norm);
    for (auto& value : values) value /= norm;
    return values;
}

double cosine_similarity(const std::vector<double>& a, const std::vector<double>& b) {
    double result = 0;
    for (size_t i = 0; i < a.size(); ++i) result += a[i] * b[i];
    if (!std::isfinite(result)) throw std::runtime_error("Не удалось вычислить косинусное сходство.");
    return std::clamp(result, -1.0, 1.0);
}

bool semantic_case(const std::string& model, const std::vector<std::string>& sentences) {
    Response r;
    bool sent = false;
    try {
        for (size_t i = 0; i < sentences.size(); ++i)
            std::cout << static_cast<char>('A' + i) << ": " << sentences[i] << '\n';
        const json payload = {{"model", model}, {"input", sentences}, {"truncate", false}, {"keep_alive", "0"}};
        sent = true;
        r = request("/api/embed", &payload);
        const auto doc = validate(r);
        if (doc.at("model").get<std::string>() != model) throw std::runtime_error("Ответ от другой модели.");
        const auto& embeddings = doc.at("embeddings");
        if (!embeddings.is_array() || embeddings.size() != sentences.size())
            throw std::runtime_error("Ожидалось ровно три вектора в порядке A, B, C.");
        std::vector<std::vector<double>> vectors;
        for (size_t i = 0; i < embeddings.size(); ++i) {
            try {
                vectors.push_back(normalized_embedding(embeddings[i]));
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("Вектор ") + static_cast<char>('A' + i) + ": " + e.what());
            }
            std::cout << "\nVector " << static_cast<char>('A' + i)
                      << ": dimension = " << embeddings[i].size()
                      << ", finite = PASS, nonzero = PASS\n";
        }
        const double ab = cosine_similarity(vectors[0], vectors[1]);
        const double ac = cosine_similarity(vectors[0], vectors[2]);
        const bool passed = ab > ac;
        std::cout << std::fixed << std::setprecision(6)
                  << "\nsimilarity(A, B): " << ab << "\nsimilarity(A, C): " << ac
                  << "\nРазница (A,B) - (A,C): " << ab - ac
                  << "\n\nУсловие: similarity(A, B) > similarity(A, C)\n"
                  << "Большее косинусное сходство означает большую близость векторов.\n"
                  << "Semantic test: " << (passed ? "PASS" : "FAIL") << '\n';
        metrics(r, passed);
        return passed;
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\nSemantic test: FAIL\n";
        if (sent) metrics(r, false);
        return false;
    }
}

bool semantic_test() {
    std::cout << "\n=== EMBEDDING SEMANTIC TESTS ===\n"
                 "В каждом наборе A и B близки по смыслу, C относится к другой теме.\n"
                 "Для PASS требуется similarity(A, B) > similarity(A, C).\n";
    const std::vector<std::string> topics = {
        "Разработка", "Медицина", "Транспорт", "Кулинария", "Образование", "Погода"
    };
    const std::vector<std::vector<std::string>> cases = {
        {"Программист разрабатывает приложение", "Разработчик создаёт программу",
         "Сегодня на улице идёт сильный дождь"},
        {"Врач осматривает пациента и назначает лечение", "Доктор обследует больного и подбирает терапию",
         "Пекарь готовит свежий хлеб в печи"},
        {"Пассажир купил билет на поезд до Москвы", "Путешественник оплатил поездку в Москву по железной дороге",
         "Кошка спит на подоконнике"},
        {"Повар варит овощной суп", "Кулинар готовит первое блюдо из овощей",
         "Спутник вышел на орбиту Земли"},
        {"Студент готовится к экзамену по математике", "Учащийся повторяет математику перед сдачей экзамена",
         "Механик заменяет колесо автомобиля"},
        {"На улице идёт сильный дождь", "За окном льёт ливень",
         "Бухгалтер составляет годовой финансовый отчёт"}
    };
    std::string model;
    try {
        const auto tags = request("/api/tags");
        std::cout << "GET /api/tags — HTTP Status: " << tags.status << '\n';
        model = find_model(model_names(validate(tags)), "bge-m3");
        std::cout << "Model: " << model << '\n';
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\nИтог: FAIL (наборы не выполнены)\n";
        return false;
    }
    size_t passed = 0;
    for (size_t i = 0; i < cases.size(); ++i) {
        std::cout << "\n--- Набор " << i + 1 << '/' << cases.size() << ": " << topics[i] << " ---\n";
        if (semantic_case(model, cases[i])) ++passed;
    }
    const bool success = passed == cases.size();
    std::cout << "\nПройдено: " << passed << '/' << cases.size()
              << "\nНе пройдено: " << cases.size() - passed
              << "\nИтог: " << (success ? "PASS" : "FAIL")
              << "\nЭто демонстрационная проверка на шести наборах, а не полная оценка качества модели.\n";
    return success;
}

bool test(bool embedding) {
    std::cout << (embedding ? "\n=== EMBEDDING TEST ===\n" : "\n=== LLM TEST ===\n");
    Response r;
    bool sent = false;
    try {
        std::string text;
        while (true) {
            std::cout << (embedding ? "Введите текст для эмбеддинга: " : "Введите вопрос для LLM: ") << std::flush;
            if (!std::getline(std::cin, text)) throw std::runtime_error("Ввод завершён.");
            if (text.find_first_not_of(" \t\r\n") != std::string::npos) break;
            std::cout << "Текст не должен быть пустым.\n";
        }
        const auto tags = request("/api/tags");
        std::cout << "GET /api/tags — HTTP Status: " << tags.status << '\n';
        const auto model = find_model(model_names(validate(tags)), embedding ? "bge-m3" : "lukey03/qwen3.5-9b-abliterated");
        std::cout << "Model: " << model << (embedding ? "\n\nInput:\n" : "\n\nQuestion:\n") << text << '\n';
        json payload;
        if (embedding) payload = {{"model", model}, {"input", text}, {"truncate", false}, {"keep_alive", "0"}};
        else payload = {{"model", model}, {"messages", json::array({{{"role", "user"}, {"content", text}}})},
                        {"stream", false}, {"think", false}, {"keep_alive", "0"}, {"options", {{"num_ctx", 4096}, {"num_predict", 512}}}};
        sent = true;
        r = request(embedding ? "/api/embed" : "/api/chat", &payload);
        const auto doc = validate(r);
        if (doc.at("model").get<std::string>() != model) throw std::runtime_error("Ответ от другой модели.");
        if (embedding) {
            const auto& vectors = doc.at("embeddings");
            if (!vectors.is_array() || vectors.size() != 1 || !vectors[0].is_array() || vectors[0].empty())
                throw std::runtime_error("Ожидался один непустой вектор.");
            const auto& vector = vectors[0];
            for (const auto& value : vector)
                if (!value.is_number() || !std::isfinite(value.get<double>())) throw std::runtime_error("Вектор содержит некорректное число.");
            std::cout << "\nVector dimension: " << vector.size() << "\n\nFirst 10 values:\n[";
            for (size_t i = 0; i < (std::min)(size_t{10}, vector.size()); ++i)
                std::cout << (i ? ", " : "") << std::fixed << std::setprecision(6) << vector[i].get<double>();
            std::cout << "]\n";
            if (vector.size() != 1024) throw std::runtime_error("Для BGE-M3 ожидалась размерность 1024.");
        } else {
            if (!doc.at("done").is_boolean() || !doc.at("done").get<bool>()) throw std::runtime_error("Генерация не завершена.");
            const auto& message = doc.at("message");
            if (message.at("role") != "assistant") throw std::runtime_error("Некорректная роль ответа.");
            const auto content = message.at("content").get<std::string>();
            if (content.find_first_not_of(" \t\r\n") == std::string::npos) throw std::runtime_error("Пустой ответ LLM.");
            std::cout << "\nResponse:\n" << content << '\n';
            if (doc.value("done_reason", "") == "length") throw std::runtime_error("Ответ обрезан лимитом токенов.");
        }
        metrics(r, true); return true;
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << '\n';
        if (sent) metrics(r, false);
        else std::cout << "Result: FAILED (запрос к модели не отправлен)\n";
        return false;
    }
}
}
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) { std::cerr << "Не удалось инициализировать libcurl.\n"; return 1; }
    int exit_code = 0;
    std::string choice;
    while (true) {
        std::cout << "\n========================================\n        SAYRA\n========================================\n\n"
                     "[1] Проверка подключения к Ollama\n[2] Тест LLM\n[3] Тест Embedding\n[4] Тест смыслового сходства (BGE-M3)\n[0] Выход\n\nВыбор: " << std::flush;
        if (!std::getline(std::cin, choice) || choice == "0" || choice == "5") break;
        bool ok = true;
        if (choice == "1") ok = connection();
        else if (choice == "2") ok = test(false);
        else if (choice == "3") ok = test(true);
        else if (choice == "4") ok = semantic_test();
        else { std::cout << "Введите 0, 1, 2, 3 или 4.\n"; continue; }
        if (!ok) exit_code = 1;
    }
    curl_global_cleanup();
    return exit_code;
}
