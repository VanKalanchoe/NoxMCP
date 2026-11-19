#pragma once

#include <string>

#include "NoxMcp/McpProtocol.h"

#if defined(NOXMCP_HAS_YAML_CPP)
#include <yaml-cpp/yaml.h>
#endif

namespace NoxMcp
{
    // An id as the tools write it: a string of 16 to 20 digits (a 64-bit UUID or handle).
    bool IsIdText(const Json& value);

    // The first way `value` does not fit `example` ("" when it fits): unknown fields (with the valid ones listed),
    // a different kind, a vector of another length, an id that is not a digit string.
    std::string CheckAgainstExample(const Json& example, const Json& value, const std::string& path = {});

    // Applies JSON-Patch operations (RFC 6902: add, remove, replace, test; paths like "/Translation" or "/Color/0")
    // to `current` into `result`; "" or why it could not.
    std::string ApplyPatch(const Json& current, const Json& operations, Json& result);

    // Where what was stored differs from what was asked (numbers within a relative 1e-5): [{ path, requested, stored }].
    Json Differences(const Json& requested, const Json& stored, const std::string& path = {});

    // `base` with `overlay`'s fields on top, recursively for objects.
    Json Merge(const Json& base, const Json& overlay);
    Json MergeExample(const Json& base, const Json& overlay);

    // The JSON kind of a field, for schemas and messages: "number", "bool", "string", "id", "vector3", "list", "object".
    std::string KindOf(const Json& value);
    // A component's schema from its example: { field: { kind, default } } (objects nested).
    Json SchemaFromExample(const Json& example);

    // Converts a scalar string ("true", "false", numbers, 64-bit id digit strings, or text) to a JSON scalar.
    Json ScalarToJson(const std::string& text);

#if defined(NOXMCP_HAS_YAML_CPP)
    Json NodeToJson(const YAML::Node& node);
    YAML::Node JsonToYaml(const Json& value);
#endif
}
