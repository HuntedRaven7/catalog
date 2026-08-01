#include "model.h"

#include <json-glib/json-glib.h>

namespace catalog {

static std::string str_member(JsonObject *obj, const char *key) {
    JsonNode *node = json_object_get_member(obj, key);
    if (node && JSON_NODE_HOLDS_VALUE(node)) {
        const char *value = json_node_get_string(node);
        if (value)
            return std::string(value);
    }
    return {};
}

bool parse_packages(const char *json, std::vector<Package> &out, std::string &error) {
    out.clear();

    JsonParser *parser = json_parser_new();
    GError *gerror = nullptr;
    if (!json_parser_load_from_data(parser, json, -1, &gerror)) {
        error = gerror && gerror->message ? gerror->message : "invalid JSON";
        g_clear_error(&gerror);
        g_object_unref(parser);
        return false;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
        error = "expected a JSON array";
        g_object_unref(parser);
        return false;
    }

    JsonArray *array = json_node_get_array(root);
    guint length = json_array_get_length(array);
    out.reserve(length);
    for (guint i = 0; i < length; i++) {
        JsonNode *node = json_array_get_element(array, i);
        if (!JSON_NODE_HOLDS_OBJECT(node))
            continue;
        JsonObject *obj = json_node_get_object(node);
        Package package;
        package.name = str_member(obj, "name");
        package.version = str_member(obj, "version");
        package.architecture = str_member(obj, "architecture");
        package.description = str_member(obj, "description");
        package.depends = str_member(obj, "depends");
        package.kind = str_member(obj, "kind");
        package.repo = str_member(obj, "repo");
        out.push_back(std::move(package));
    }

    g_object_unref(parser);
    return true;
}

}
