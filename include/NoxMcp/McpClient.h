#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "NoxMcp/McpProtocol.h"

namespace NoxMcp
{
    struct ClientReply
    {
        std::string Error; // empty: `Result` holds the JSON-RPC result
        Json Result;
    };

    // One request ("tools/list", "tools/call", ...); `timeoutSeconds` for the whole answer.
    ClientReply CallServer(uint16_t port, const std::string& token, const std::string& method, const Json& params, int timeoutSeconds = 10);

    // Converts a tools/call result JSON object (content, structuredContent, isError) back into a ToolResult.
    ToolResult ToolResultFromJson(const Json& result);

    // Background liveness probe for another local MCP server.
    class ServerProbe
    {
    public:
        enum class State : int { Starting = 0, Up, Absent, Cancelled };
        static std::shared_ptr<ServerProbe> Start(std::string name, uint16_t port, std::string token, double timeoutSeconds);
        State Get() const { return static_cast<State>(m_State.load()); }
        void Cancel() { m_Cancelled = true; }

    private:
        std::atomic<int> m_State{ 0 };
        std::atomic<bool> m_Cancelled{ false };
    };

    const char* ToString(ServerProbe::State state);
}
