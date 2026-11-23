#pragma once

#include "omegaconf/common/source_info.hpp"
#include <stdexcept>
#include <string>
#include <utility>

namespace omegaconf {

/**
 * @brief Base exception for all OmegaConf operations, carrying source file and line/column location diagnostics.
 */
class OmegaConfException : public std::runtime_error {
public:
    explicit OmegaConfException(std::string message, SourceInfo source = SourceInfo::unknown())
        : std::runtime_error(format_message(message, source)),
          source_location_(std::move(source)),
          raw_message_(std::move(message)) {}

    [[nodiscard]] const SourceInfo& source_location() const noexcept {
        return source_location_;
    }

    [[nodiscard]] const std::string& raw_message() const noexcept {
        return raw_message_;
    }

private:
    SourceInfo source_location_{};
    std::string raw_message_{};

    static std::string format_message(const std::string& msg, const SourceInfo& src) {
        if (!src.is_unknown()) {
            return msg + " at " + src.to_string();
        }
        return msg;
    }
};

/**
 * @brief Thrown when configuration syntax (YAML, JSON, TOML, or CLI expression) is malformed.
 */
class ConfigSyntaxException : public OmegaConfException {
public:
    explicit ConfigSyntaxException(std::string message, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Syntax error: " + message, std::move(source)) {}
};

/**
 * @brief Thrown when an operator (+=, -=, *=, etc.) is applied to an incompatible data type or division by zero occurs.
 */
class InvalidOperatorException : public OmegaConfException {
public:
    InvalidOperatorException(std::string op, std::string target_type, std::string message, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Operator '" + op + "' is invalid for target type '" + target_type + "': " + message, std::move(source)),
          operator_(std::move(op)),
          target_type_(std::move(target_type)) {}

    [[nodiscard]] const std::string& op() const noexcept { return operator_; }
    [[nodiscard]] const std::string& target_type() const noexcept { return target_type_; }

private:
    std::string operator_{};
    std::string target_type_{};
};

/**
 * @brief Thrown when a mandatory configuration property is missing.
 */
class MissingRequiredConfigException : public OmegaConfException {
public:
    explicit MissingRequiredConfigException(std::string config_path, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Required configuration property '" + config_path + "' was not provided.", std::move(source)),
          config_path_(std::move(config_path)) {}

    [[nodiscard]] const std::string& config_path() const noexcept { return config_path_; }

private:
    std::string config_path_{};
};

/**
 * @brief Thrown when a configuration value is marked as '???' (mandatory placeholder) but was never overridden.
 */
class MissingValueMarkerException : public OmegaConfException {
public:
    explicit MissingValueMarkerException(std::string key, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Configuration key '" + key + "' is marked as mandatory ('???') but was never supplied with a concrete value.", std::move(source)),
          key_(std::move(key)) {}

    [[nodiscard]] const std::string& key() const noexcept { return key_; }

private:
    std::string key_{};
};

/**
 * @brief Thrown when a configuration value cannot be converted to the target type during binding.
 */
class ConfigTypeMismatchException : public OmegaConfException {
public:
    ConfigTypeMismatchException(std::string config_path, std::string expected_type, std::string actual_type, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Type mismatch at '" + config_path + "'. Expected '" + expected_type + "', but found value of type '" + actual_type + "'.", std::move(source)),
          config_path_(std::move(config_path)),
          expected_type_(std::move(expected_type)),
          actual_type_(std::move(actual_type)) {}

    [[nodiscard]] const std::string& config_path() const noexcept { return config_path_; }
    [[nodiscard]] const std::string& expected_type() const noexcept { return expected_type_; }
    [[nodiscard]] const std::string& actual_type() const noexcept { return actual_type_; }

private:
    std::string config_path_{};
    std::string expected_type_{};
    std::string actual_type_{};
};

/**
 * @brief Thrown when variable interpolation fails (e.g., circular reference, missing referenced key, or invalid eval expression).
 */
class ConfigInterpolationException : public OmegaConfException {
public:
    explicit ConfigInterpolationException(std::string message, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Interpolation error: " + message, std::move(source)) {}
};

/**
 * @brief Thrown when inheritance or composition resolution fails (file not found or circular inheritance cycle).
 */
class ConfigInheritanceException : public OmegaConfException {
public:
    explicit ConfigInheritanceException(std::string message, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Inheritance error: " + message, std::move(source)) {}
};

/**
 * @brief Thrown in Struct Mode when an override attempts to set or delete a key that does not exist in the schema,
 * or when a +key append operator targets a key that already exists.
 */
class ConfigStructException : public OmegaConfException {
public:
    ConfigStructException(std::string config_path, std::string message, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Struct mode error at '" + config_path + "': " + message, std::move(source)),
          config_path_(std::move(config_path)) {}

    [[nodiscard]] const std::string& config_path() const noexcept { return config_path_; }

private:
    std::string config_path_{};
};

/**
 * @brief Thrown when attempting to mutate a ConfigNode that has been locked as read-only.
 */
class ConfigReadOnlyException : public OmegaConfException {
public:
    explicit ConfigReadOnlyException(std::string message, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Read-only configuration error: " + message, std::move(source)) {}
};

/**
 * @brief Thrown when configuration division by zero occurs.
 */
class ConfigDivideByZeroException : public OmegaConfException {
public:
    explicit ConfigDivideByZeroException(std::string message, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Division by zero: " + message, std::move(source)) {}
};

/**
 * @brief Thrown when arithmetic overflow occurs in configuration operations.
 */
class ConfigOverflowException : public OmegaConfException {
public:
    explicit ConfigOverflowException(std::string message, SourceInfo source = SourceInfo::unknown())
        : OmegaConfException("Arithmetic overflow: " + message, std::move(source)) {}
};

} // namespace omegaconf
