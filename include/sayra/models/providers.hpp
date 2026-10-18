#pragma once
#include <sayra/core/config.hpp>
#include <sayra/models/transport.hpp>
namespace sayra {
class IModelProvider {
  public:
    virtual ~IModelProvider() = default;
    virtual std::string id() const = 0;
    virtual std::vector<ModelInfo> discover(std::stop_token) = 0;
    virtual GenerationResult generate(const GenerationRequest &, DeltaCallback, std::stop_token) = 0;
};
class OllamaProvider final : public IModelProvider {
    Configuration config_;
    std::shared_ptr<IHttpTransport> http_;

  public:
    OllamaProvider(Configuration c, std::shared_ptr<IHttpTransport> h)
        : config_(std::move(c)), http_(std::move(h)) {}
    std::string id() const override {
        return "ollama";
    }
    std::vector<ModelInfo> discover(std::stop_token) override;
    GenerationResult generate(const GenerationRequest &, DeltaCallback, std::stop_token) override;
};
class OpenAICompatibleProvider final : public IModelProvider {
    Configuration config_;
    std::shared_ptr<IHttpTransport> http_;
    std::string key() const;

  public:
    OpenAICompatibleProvider(Configuration c, std::shared_ptr<IHttpTransport> h)
        : config_(std::move(c)), http_(std::move(h)) {}
    std::string id() const override {
        return "cloud";
    }
    std::vector<ModelInfo> discover(std::stop_token) override;
    GenerationResult generate(const GenerationRequest &, DeltaCallback, std::stop_token) override;
};
class ModelRegistry {
    std::vector<std::shared_ptr<IModelProvider>> providers_;
    std::vector<ModelInfo> models_;
    std::vector<std::string> errors_;
    Configuration config_;

  public:
    ModelRegistry(Configuration config, std::vector<std::shared_ptr<IModelProvider>> providers)
        : providers_(std::move(providers)), config_(std::move(config)) {}
    void refresh(std::stop_token = {});
    const std::vector<ModelInfo> &models() const {
        return models_;
    }
    const std::vector<std::string> &errors() const {
        return errors_;
    }
    std::shared_ptr<IModelProvider> provider(const std::string &id) const;
};
HttpRequest json_request(const Configuration &, const std::string &url, const Json *body = nullptr);
Json checked_json(const HttpResult &response);
} // namespace sayra