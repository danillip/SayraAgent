#pragma once
#include <sayra/integration/knowledge.hpp>
#include <sayra/models/providers.hpp>
namespace sayra {
std::vector<double> normalized_vector(const std::vector<double> &values);
double cosine_similarity(const std::vector<double> &a, const std::vector<double> &b);
class OllamaEmbeddingProvider final : public IEmbeddingProvider {
    Configuration config_;
    std::shared_ptr<IHttpTransport> http_;

  public:
    OllamaEmbeddingProvider(Configuration c, std::shared_ptr<IHttpTransport> h)
        : config_(std::move(c)), http_(std::move(h)) {}
    EmbeddingResult embed(const EmbeddingRequest &, std::stop_token) override;
};
} // namespace sayra