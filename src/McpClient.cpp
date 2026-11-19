#include "NoxMcp/McpClient.h"
#include "NoxMcp/McpLog.h"

#include <atomic>
#include <chrono>
#include <format>
#include <thread>

#include "httplib.h"

namespace NoxMcp
{
    ClientReply CallServer(uint16_t port, const std::string& token, const std::string& method, const Json& params, int timeoutSeconds)
    {
        static std::atomic<uint64_t> nextId{ 1 };
        ClientReply reply;
        httplib::Client client("127.0.0.1", port);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(timeoutSeconds, 0);
        client.set_write_timeout(5, 0);
        httplib::Headers headers{
            { "Accept", "application/json, text/event-stream" },
            { "MCP-Protocol-Version", LatestProtocolVersion }
        };
        if (!token.empty())
            headers.emplace("Authorization", "Bearer " + token);

        const Json request{ { "jsonrpc", "2.0" }, { "id", static_cast<int64_t>(nextId++) }, { "method", method }, { "params", params } };
        const httplib::Result response = client.Post("/mcp", headers, request.dump(), "application/json");
        if (!response)
        {
            reply.Error = std::format("no answer from 127.0.0.1:{} ({}) -- is that game running with --mcp?", port, httplib::to_string(response.error()));
            return reply;
        }
        if (response->status != 200)
        {
            reply.Error = std::format("127.0.0.1:{} answered HTTP {}: {}", port, response->status, response->body.substr(0, 300));
            return reply;
        }
        Json message;
        try
        {
            message = Json::parse(response->body);
        }
        catch (const std::exception&)
        {
            reply.Error = std::format("127.0.0.1:{} did not answer JSON", port);
            return reply;
        }
        if (message.contains("error"))
        {
            const Json& error = message["error"];
            reply.Error = std::format("127.0.0.1:{}: {}", port, error.is_object() ? error.value("message", error.dump()) : error.dump());
            return reply;
        }
        if (!message.contains("result"))
        {
            reply.Error = std::format("127.0.0.1:{} answered without a result", port);
            return reply;
        }
        reply.Result = message["result"];
        return reply;
    }

    std::shared_ptr<ServerProbe> ServerProbe::Start(std::string name, uint16_t port, std::string token, double timeoutSeconds)
    {
        auto probe = std::make_shared<ServerProbe>();
        std::thread([probe, name = std::move(name), port, token = std::move(token), timeoutSeconds]()
        {
            const auto start = std::chrono::steady_clock::now();
            while (!probe->m_Cancelled)
            {
                const ClientReply reply = CallServer(port, token, "tools/list", Json::object(), 5);
                if (probe->m_Cancelled)
                    break;
                if (reply.Error.empty())
                {
                    probe->m_State = static_cast<int>(State::Up);
                    LogMessage(LogLevel::Info, std::format("MCP: {}'s MCP server answers on http://127.0.0.1:{}/mcp", name, port));
                    return;
                }
                if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= timeoutSeconds)
                {
                    probe->m_State = static_cast<int>(State::Absent);
                    LogMessage(LogLevel::Warn, std::format("MCP: {} did not open its MCP server on port {} within {:.0f} s ({})",
                                                           name, port, timeoutSeconds, reply.Error));
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            probe->m_State = static_cast<int>(State::Cancelled);
        }).detach();
        return probe;
    }

    const char* ToString(ServerProbe::State state)
    {
        switch (state)
        {
        case ServerProbe::State::Starting: return "starting";
        case ServerProbe::State::Up: return "ready";
        case ServerProbe::State::Absent: return "not listening";
        default: return "stopped";
        }
    }

    ToolResult ToolResultFromJson(const Json& result)
    {
        ToolResult tool;
        if (result.is_object())
        {
            if (result.contains("content") && result["content"].is_array())
                tool.Content = result["content"];
            if (result.contains("structuredContent"))
                tool.Structured = result["structuredContent"];
            tool.IsError = result.value("isError", false);
        }
        return tool;
    }
}
