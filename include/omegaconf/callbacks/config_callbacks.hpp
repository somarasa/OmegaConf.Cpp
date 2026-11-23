#pragma once

#include "omegaconf/ast/config_object_node.hpp"

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <exception>

namespace omegaconf {

class IConfigCallback {
public:
    virtual ~IConfigCallback() = default;

    virtual void on_compose_config(const std::shared_ptr<ConfigObjectNode>& config,
                                   const std::optional<std::string>& config_name = std::nullopt,
                                   const std::vector<std::string>& cli_overrides = {}) = 0;

    virtual void on_run_start(const std::shared_ptr<ConfigObjectNode>& config,
                              const std::optional<std::string>& config_name = std::nullopt) = 0;

    virtual void on_run_end(const std::shared_ptr<ConfigObjectNode>& config,
                            const std::optional<std::string>& config_name = std::nullopt) = 0;

    virtual void on_multirun_start(const std::shared_ptr<ConfigObjectNode>& config,
                                   const std::optional<std::string>& config_name = std::nullopt) = 0;

    virtual void on_multirun_end(const std::vector<std::shared_ptr<ConfigObjectNode>>& job_configs,
                                 const std::optional<std::string>& config_name = std::nullopt) = 0;

    virtual void on_job_start(const std::shared_ptr<ConfigObjectNode>& config, int job_id = 0) = 0;

    virtual void on_job_end(const std::shared_ptr<ConfigObjectNode>& config, int job_id = 0,
                            const std::exception_ptr& error = nullptr) = 0;
};

class ConfigCallback : public IConfigCallback {
public:
    void on_compose_config(const std::shared_ptr<ConfigObjectNode>& /*config*/,
                           const std::optional<std::string>& /*config_name*/ = std::nullopt,
                           const std::vector<std::string>& /*cli_overrides*/ = {}) override {}

    void on_run_start(const std::shared_ptr<ConfigObjectNode>& /*config*/,
                      const std::optional<std::string>& /*config_name*/ = std::nullopt) override {}

    void on_run_end(const std::shared_ptr<ConfigObjectNode>& /*config*/,
                    const std::optional<std::string>& /*config_name*/ = std::nullopt) override {}

    void on_multirun_start(const std::shared_ptr<ConfigObjectNode>& /*config*/,
                           const std::optional<std::string>& /*config_name*/ = std::nullopt) override {}

    void on_multirun_end(const std::vector<std::shared_ptr<ConfigObjectNode>>& /*job_configs*/,
                         const std::optional<std::string>& /*config_name*/ = std::nullopt) override {}

    void on_job_start(const std::shared_ptr<ConfigObjectNode>& /*config*/, int /*job_id*/ = 0) override {}

    void on_job_end(const std::shared_ptr<ConfigObjectNode>& /*config*/, int /*job_id*/ = 0,
                    const std::exception_ptr& /*error*/ = nullptr) override {}
};

} // namespace omegaconf
