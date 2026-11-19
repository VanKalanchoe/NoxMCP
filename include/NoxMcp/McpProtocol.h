#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#else
#include "json.hpp"
#endif

namespace NoxMcp
{
    using Json = nlohmann::json;
    using JsonValue = Json;

    // Permission level required to see and invoke a tool over MCP.
    enum class ToolAccess : uint8_t
    {
        Read = 0,
        Write = 1,
        Execute = 2
    };
    using ConsoleAccess = ToolAccess;

    inline const char* ToString(ToolAccess access)
    {
        switch (access)
        {
        case ToolAccess::Read: return "read";
        case ToolAccess::Write: return "write";
        case ToolAccess::Execute: return "execute";
        default: return "read";
        }
    }

    constexpr const char* LatestProtocolVersion = "2025-06-18";
    const std::vector<std::string>& SupportedProtocolVersions();

    // A tool's answer: content blocks for the model (text, images), optionally the same as structured JSON,
    // and whether it failed (a failed tool is an answer the model reads and corrects itself from, not a protocol error).
    struct ToolResult
    {
        Json Content = Json::array();
        Json Structured;      // null: none
        bool IsError = false;

        static ToolResult Text(const std::string& text);
        static ToolResult Error(const std::string& text);
        static ToolResult Data(const Json& value);
        ToolResult& AddImage(const std::string& base64, const std::string& mimeType);
        Json ToJson() const;
    };
    using McpToolResult = ToolResult;

    using ToolDone = std::function<void(ToolResult)>;

    struct Tool
    {
        std::string Name;
        std::string Title;
        std::string Description;
        Json InputSchema = Json{ { "type", "object" }, { "properties", Json::object() } };
        ToolAccess Access = ToolAccess::Read;
        bool Destructive = false;
        // Runs the call; `done` is called once with the result, now or later (e.g. a screenshot a frame or two after).
        std::function<void(const Json& arguments, ToolDone done)> Handler;
    };

    // High-level synchronous tool registration schema & callback (MODULES_EXTRACTION_PLAN.md §4).
    struct McpToolSchema
    {
        std::string Name;
        std::string Description;
        std::string InputJsonSchema; // JSON Schema object string (or empty for default empty object schema)
    };

    using McpToolHandler = std::function<McpToolResult(const JsonValue& arguments)>;

    // Fluent builder for a tool's input schema (JSON Schema subset checked by Protocol::Validate).
    class Schema
    {
    public:
        Schema& String(const std::string& name, const std::string& description, bool required = false);
        Schema& Integer(const std::string& name, const std::string& description, bool required = false,
                        std::optional<int64_t> minimum = {}, std::optional<int64_t> maximum = {});
        Schema& Number(const std::string& name, const std::string& description, bool required = false,
                       std::optional<double> minimum = {}, std::optional<double> maximum = {});
        Schema& Boolean(const std::string& name, const std::string& description, bool required = false);
        Schema& Array(const std::string& name, const std::string& description, const std::string& itemType, bool required = false);
        Schema& Enum(const std::string& name, const std::string& description, const std::vector<std::string>& values, bool required = false);
        Json Build() const { return m_Schema; }
        operator Json() const { return m_Schema; }

    private:
        void Add(const std::string& name, Json property, bool required);
        Json m_Schema = Json{ { "type", "object" }, { "properties", Json::object() }, { "additionalProperties", false } };
    };

    struct HttpRequest
    {
        std::string Method;                         // "POST", "GET", ...
        std::map<std::string, std::string> Headers; // lower-case names
        std::string Body;
    };

    struct HttpReply
    {
        int Status = 200;
        std::string ContentType = "application/json";
        std::map<std::string, std::string> Headers;
        std::string Body;
    };

    struct Settings
    {
        std::string Token;                         // bearer token clients must send when RequireAuth is true
        bool RequireAuth = true;                   // enforce bearer token authentication
        uint16_t Port = 8765;                      // loopback port (for Host / Origin checks)
        ToolAccess Access = ToolAccess::Write;     // granted to clients
        std::string ServerName = "nox-editor";
        std::string ServerTitle = "NoxEditor";
        std::string ServerVersion = "0.1";
        std::string Instructions;                  // initialize's instructions
    };

    class Protocol
    {
    public:
        explicit Protocol(Settings settings);

        void AddTool(Tool tool);
        const std::vector<Tool>& GetTools() const { return m_Tools; }
        void SetAccess(ToolAccess access);
        ToolAccess GetAccess() const;

        using Dispatcher = std::function<ToolResult(const Tool& tool, const Json& arguments)>;
        void SetDispatcher(Dispatcher dispatcher) { m_Dispatcher = std::move(dispatcher); }

        HttpReply Handle(const HttpRequest& request);

        size_t GetCallCount() const { return m_Calls; }

        static std::string Validate(const Json& schema, const Json& arguments);

    private:
        std::optional<HttpReply> CheckRequest(const HttpRequest& request) const;
        Json HandleRequest(const Json& message);
        Json CallTool(const Json& params, const Json& id);

        Settings m_Settings;
        mutable std::mutex m_SettingsMutex;
        std::vector<Tool> m_Tools;
        Dispatcher m_Dispatcher;
        std::atomic<size_t> m_Calls{ 0 };
    };

    namespace RpcError
    {
        constexpr int ParseError = -32700;
        constexpr int InvalidRequest = -32600;
        constexpr int MethodNotFound = -32601;
        constexpr int InvalidParams = -32602;
        constexpr int InternalError = -32603;
    }

    bool ConstantTimeEquals(const std::string& a, const std::string& b);
    std::string GenerateToken();
    std::string Base64(const uint8_t* data, size_t size);
}

namespace Nox
{
    using ConsoleAccess = NoxMcp::ToolAccess;
    inline const char* ToString(NoxMcp::ToolAccess access) { return NoxMcp::ToString(access); }
    namespace Mcp = NoxMcp;
}
