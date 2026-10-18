#pragma once
#include <sayra/core/types.hpp>
namespace sayra {
struct KnowledgeQuery {
    std::string text, project_id;
    size_t max_context_bytes = 12000;
    bool allow_cloud = false;
};
struct KnowledgeResult {
    std::string context;
    std::vector<SourceReference> sources;
    std::vector<std::string> gaps;
};
struct ResearchRequest {
    std::string text, freshness, context;
    size_t max_sources = 5;
    bool allow_internet = false;
};
struct ResearchResult {
    std::vector<std::string> claims, gaps;
    std::vector<SourceReference> sources;
    VerificationStatus verification = VerificationStatus::Unverified;
};
class IKnowledgeRetriever {
  public:
    virtual ~IKnowledgeRetriever() = default;
    virtual KnowledgeResult retrieve(const KnowledgeQuery &, std::stop_token) = 0;
};
class IResearchService {
  public:
    virtual ~IResearchService() = default;
    virtual ResearchResult research(const ResearchRequest &, std::stop_token) = 0;
};
struct EmbeddingRequest {
    std::string model;
    std::vector<std::string> texts;
};
struct EmbeddingResult {
    std::string model;
    std::vector<std::vector<double>> vectors;
    double seconds = 0;
};
class IEmbeddingProvider {
  public:
    virtual ~IEmbeddingProvider() = default;
    virtual EmbeddingResult embed(const EmbeddingRequest &, std::stop_token) = 0;
};
} // namespace sayra