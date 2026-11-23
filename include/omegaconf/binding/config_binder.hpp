#pragma once

#include "omegaconf/common/config.hpp"
#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include "omegaconf/ast/config_list_node.hpp"
#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/serialization/json_serializer.hpp"
#include "omegaconf/common/exceptions.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <map>
#include <optional>
#include <type_traits>
#include <cctype>
#include <cstring>

#if OMEGACONF_CPP20
#include <glaze/glaze.hpp>
#endif

namespace omegaconf {

class ConfigBinder;

namespace detail {

template <typename T>
struct is_optional : std::false_type {};

template <typename U>
struct is_optional<std::optional<U>> : std::true_type {};

template <typename T>
struct is_vector : std::false_type {};

template <typename U, typename Alloc>
struct is_vector<std::vector<U, Alloc>> : std::true_type {};

template <typename T>
struct is_map : std::false_type {};

template <typename K, typename V, typename H, typename E, typename Alloc>
struct is_map<std::unordered_map<K, V, H, E, Alloc>> : std::true_type {};

template <typename K, typename V, typename C, typename Alloc>
struct is_map<std::map<K, V, C, Alloc>> : std::true_type {};

template <typename T, typename = void>
struct is_reflected : std::false_type {};

template <typename T>
struct is_reflected<T, std::void_t<decltype(omegaconf_reflect_bind(std::declval<T&>(), std::declval<const ::omegaconf::ConfigObjectNode*>()))>> : std::true_type {};

template <typename T>
void bind_value(T& target, const ConfigNode* node);

template <typename Struct, typename Field>
inline void bind_member(Struct& target, const ConfigObjectNode* obj_node, const char* name, Field Struct::* member) {
    if (!obj_node) return;
    auto child = obj_node->get_child(name);
    if (!child) {
        std::string sc;
        // snake case conversion
        for (size_t i = 0; i < std::strlen(name); ++i) {
            char c = name[i];
            if (std::isupper(static_cast<unsigned char>(c))) {
                if (i > 0 && name[i - 1] != '_') {
                    bool prev_is_upper = std::isupper(static_cast<unsigned char>(name[i - 1]));
                    bool next_is_lower = (i + 1 < std::strlen(name)) && std::islower(static_cast<unsigned char>(name[i + 1]));
                    if (!prev_is_upper || next_is_lower) {
                        sc.push_back('_');
                    }
                }
                sc.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            } else {
                sc.push_back(c);
            }
        }
        if (sc != name) {
            child = obj_node->get_child(sc);
        }
    }
    if (child) {
        bind_value(target.*member, child.get());
    }
}

template <typename T>
void bind_value(T& target, const ConfigNode* node) {
    if (!node) return;

    if constexpr (std::is_same_v<T, std::string>) {
        if (auto vn = dynamic_cast<const ConfigValueNode*>(node)) {
            target = vn->to_string();
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        if (auto vn = dynamic_cast<const ConfigValueNode*>(node)) {
            target = vn->as_bool();
        }
    } else if constexpr (std::is_integral_v<T>) {
        if (auto vn = dynamic_cast<const ConfigValueNode*>(node)) {
            target = static_cast<T>(vn->as_integer());
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        if (auto vn = dynamic_cast<const ConfigValueNode*>(node)) {
            target = static_cast<T>(vn->as_double());
        }
    } else if constexpr (is_optional<T>::value) {
        if (auto vn = dynamic_cast<const ConfigValueNode*>(node)) {
            if (vn->is_null() || vn->to_string() == "null" || vn->to_string() == "~") {
                target = std::nullopt;
                return;
            }
        }
        typename T::value_type inner{};
        bind_value(inner, node);
        target = std::move(inner);
    } else if constexpr (is_vector<T>::value) {
        if (auto ln = dynamic_cast<const ConfigListNode*>(node)) {
            target.clear();
            target.reserve(ln->size());
            for (size_t i = 0; i < ln->size(); ++i) {
                typename T::value_type item{};
                bind_value(item, (*ln)[i].get());
                target.push_back(std::move(item));
            }
        }
    } else if constexpr (is_map<T>::value) {
        if (auto on = dynamic_cast<const ConfigObjectNode*>(node)) {
            target.clear();
            for (const auto& kv : on->children()) {
                typename T::mapped_type item{};
                bind_value(item, kv.second.get());
                target[kv.first] = std::move(item);
            }
        }
    } else if constexpr (is_reflected<T>::value) {
        if (auto on = dynamic_cast<const ConfigObjectNode*>(node)) {
            omegaconf_reflect_bind(target, on);
        }
    }
}

} // namespace detail

class ConfigBinder {
public:
    static std::string to_snake_case(std::string_view input) {
        if (input.empty()) return "";
        std::string result;
        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            if (std::isupper(static_cast<unsigned char>(c))) {
                if (i > 0 && input[i - 1] != '_') {
                    bool prev_is_upper = std::isupper(static_cast<unsigned char>(input[i - 1]));
                    bool next_is_lower = (i + 1 < input.size()) && std::islower(static_cast<unsigned char>(input[i + 1]));
                    if (!prev_is_upper || next_is_lower) {
                        result.push_back('_');
                    }
                }
                result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            } else {
                result.push_back(c);
            }
        }
        return result;
    }

    template <typename T>
    static T bind(const std::shared_ptr<ConfigNode>& node) {
        if (!node) return T{};

#if OMEGACONF_CPP20
        std::string json = JsonSerializer::serialize(node.get());
        T obj{};
        static constexpr glz::opts options{.error_on_unknown_keys = false};
        auto ec = glz::read<options>(obj, json);
        if (ec) {
            throw ConfigTypeMismatchException(node->source().to_string(), typeid(T).name(), glz::format_error(ec, json), node->source());
        }
        return obj;
#else
        T obj{};
        detail::bind_value(obj, node.get());
        return obj;
#endif
    }
};

} // namespace omegaconf

#define OMEGACONF_EXPAND(x) x

#define OMEGACONF_BIND_MEMBER_OP(Type, m) ::omegaconf::detail::bind_member(target, node, #m, &Type::m);

#define OMEGACONF_FE_0(ACTION, Type)
#define OMEGACONF_FE_1(ACTION, Type, x) ACTION(Type, x)
#define OMEGACONF_FE_2(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_1(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_3(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_2(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_4(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_3(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_5(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_4(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_6(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_5(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_7(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_6(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_8(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_7(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_9(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_8(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_10(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_9(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_11(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_10(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_12(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_11(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_13(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_12(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_14(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_13(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_15(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_14(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_16(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_15(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_17(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_16(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_18(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_17(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_19(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_18(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_20(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_19(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_21(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_20(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_22(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_21(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_23(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_22(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_24(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_23(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_25(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_24(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_26(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_25(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_27(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_26(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_28(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_27(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_29(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_28(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_30(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_29(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_31(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_30(ACTION, Type, __VA_ARGS__))
#define OMEGACONF_FE_32(ACTION, Type, x, ...) ACTION(Type, x) OMEGACONF_EXPAND(OMEGACONF_FE_31(ACTION, Type, __VA_ARGS__))

#define OMEGACONF_GET_MACRO(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,_17,_18,_19,_20,_21,_22,_23,_24,_25,_26,_27,_28,_29,_30,_31,_32,NAME,...) NAME

#define OMEGACONF_FOR_EACH(ACTION, Type, ...) \
    OMEGACONF_EXPAND(OMEGACONF_GET_MACRO(__VA_ARGS__, \
        OMEGACONF_FE_32, OMEGACONF_FE_31, OMEGACONF_FE_30, OMEGACONF_FE_29, \
        OMEGACONF_FE_28, OMEGACONF_FE_27, OMEGACONF_FE_26, OMEGACONF_FE_25, \
        OMEGACONF_FE_24, OMEGACONF_FE_23, OMEGACONF_FE_22, OMEGACONF_FE_21, \
        OMEGACONF_FE_20, OMEGACONF_FE_19, OMEGACONF_FE_18, OMEGACONF_FE_17, \
        OMEGACONF_FE_16, OMEGACONF_FE_15, OMEGACONF_FE_14, OMEGACONF_FE_13, \
        OMEGACONF_FE_12, OMEGACONF_FE_11, OMEGACONF_FE_10, OMEGACONF_FE_9, \
        OMEGACONF_FE_8, OMEGACONF_FE_7, OMEGACONF_FE_6, OMEGACONF_FE_5, \
        OMEGACONF_FE_4, OMEGACONF_FE_3, OMEGACONF_FE_2, OMEGACONF_FE_1, \
        OMEGACONF_FE_0)(ACTION, Type, __VA_ARGS__))

#define OMEGACONF_REFLECT(Type, ...) \
    inline void omegaconf_reflect_bind(Type& target, const ::omegaconf::ConfigObjectNode* node) { \
        OMEGACONF_FOR_EACH(OMEGACONF_BIND_MEMBER_OP, Type, __VA_ARGS__) \
    }
