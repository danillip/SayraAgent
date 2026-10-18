#include <algorithm>
#include <cmath>
#include <sayra/embeddings/embeddings.hpp>
namespace sayra {
std::vector<double> normalized_vector(const std::vector<double> &input) {
    if (input.empty())
        throw ProviderError("Пустой embedding");
    double scale = 0;
    for (const auto v : input) {
        if (!std::isfinite(v))
            throw ProviderError("Неконечное значение embedding");
        scale = (std::max)(scale, std::abs(v));
    }
    if (scale == 0)
        throw ProviderError("Нулевой embedding");
    auto result = input;
    double squared = 0;
    for (auto &v : result) {
        v /= scale;
        squared += v * v;
    }
    for (auto &v : result)
        v /= std::sqrt(squared);
    return result;
}
double cosine_similarity(const std::vector<double> &a, const std::vector<double> &b) {
    if (a.size() != b.size())
        throw ProviderError("Embedding dimensions differ");
    auto x = normalized_vector(a), y = normalized_vector(b);
    double sum = 0;
    for (size_t i = 0; i < x.size(); ++i)
        sum += x[i] * y[i];
    return std::clamp(sum, -1.0, 1.0);
}
EmbeddingResult OllamaEmbeddingProvider::embed(const EmbeddingRequest &req, std::stop_token stop) {
    if (req.model.empty() || req.texts.empty())
        throw ProviderError("Specify embedding model and input");
    Json payload = {{"model", req.model},
                    {"input", req.texts},
                    {"truncate", false},
                    {"keep_alive", config_.generation.keep_alive}};
    auto url = config_.ollama_url;
    while (url.ends_with('/'))
        url.pop_back();
    const auto r = http_->perform(json_request(config_, url + "/api/embed", &payload), {}, stop);
    const auto doc = checked_json(r);
    EmbeddingResult out;
    out.model = doc.at("model").get<std::string>();
    out.seconds = r.seconds;
    if (out.model != req.model)
        throw ProviderError("Embedding model mismatch");
    const auto &rows = doc.at("embeddings");
    if (!rows.is_array() || rows.size() != req.texts.size())
        throw ProviderError("Embedding count mismatch");
    size_t dimensions = 0;
    for (const auto &row : rows) {
        if (!row.is_array())
            throw ProviderError("Embedding is not an array");
        std::vector<double> values;
        for (const auto &value : row) {
            if (!value.is_number())
                throw ProviderError("Embedding contains non-number");
            values.push_back(value.get<double>());
        }
        normalized_vector(values);
        if (!dimensions)
            dimensions = values.size();
        if (dimensions != values.size())
            throw ProviderError("Inconsistent embedding dimensions");
        if ((lower_utf8(req.model) == "bge-m3" || lower_utf8(req.model).starts_with("bge-m3:")) &&
            dimensions != 1024)
            throw ProviderError("BGE-M3 must return 1024 dimensions");
        out.vectors.push_back(std::move(values));
    }
    return out;
}
} // namespace sayra