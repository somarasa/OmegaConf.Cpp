#include "omegaconf/ast/config_node.hpp"
#include "omegaconf/serialization/yaml_serializer.hpp"
#include "omegaconf/serialization/json_serializer.hpp"
#include "omegaconf/ast/config_object_node.hpp"
#include "omegaconf/ast/config_value_node.hpp"
#include <sstream>

#if defined(OMEGACONF_COMPILED_LIB)

namespace omegaconf {

std::string ConfigNode::to_yaml() const {
    return YamlSerializer::serialize(this);
}

std::string ConfigNode::to_json(bool indented) const {
    return JsonSerializer::serialize(this, indented);
}

std::string ConfigNode::to_toml() const {
    if (!is_object()) return "";
    const auto* obj = as_object();
    std::ostringstream ss;
    for (const auto& key : obj->keys()) {
        auto child = obj->get_child(key);
        if (child && child->is_value()) {
            const auto* v = child->as_value();
            if (v->is_string()) {
                ss << key << " = \"" << v->as_string() << "\"\n";
            } else if (v->is_bool()) {
                ss << key << " = " << (v->as_bool() ? "true" : "false") << "\n";
            } else {
                ss << key << " = " << v->to_string() << "\n";
            }
        }
    }
    return ss.str();
}

} // namespace omegaconf

#endif // OMEGACONF_COMPILED_LIB
