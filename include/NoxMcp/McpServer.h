#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "NoxMcp/McpProtocol.h"

namespace httplib
{
    class Server;
}

namespace NoxMcp
{
    // The MCP endpoint over HTTP (cpp-httplib): http://127.0.0.1:<port>/mcp, loopback only.
    // Requests are answered on the HTTP library's threads; a tool call is queued to the host's main thread
    // (Pump / PollMainThreadQueue, once a frame) and its HTTP thread waits for the answer.
    class McpServer
    {
    public:
        McpServer();
        ~McpServer();
        McpServer(const McpServer&) = delete;
        McpServer& operator=(const McpServer&) = delete;

        // High-level registration API (MODULES_EXTRACTION_PLAN.md §4).
        void RegisterTool(McpToolSchema schema, McpToolHandler handler);
        void RegisterTool(Tool tool);

        // Optional default settings configuration before calling Start(port).
        void SetToken(std::string token) { m_DefaultSettings.Token = std::move(token); }
        const std::string& GetToken() const { return m_DefaultSettings.Token; }
        void SetServerInfo(std::string name, std::string title, std::string version = "0.1.0");
        void SetInstructions(std::string instructions) { m_DefaultSettings.Instructions = std::move(instructions); }

        // Starts listening on 127.0.0.1:port with any tools registered via RegisterTool.
        bool Start(uint16_t port = 8765);

        // Full settings start (used by NoxEditor / NoxRuntime and multi-tool suites).
        bool Start(const Settings& settings, std::vector<Tool> tools, std::string& error);
        bool Start(const Settings& settings, std::string& error);

        // Stops listening; calls still queued are answered "the server stopped".
        void Stop();
        bool IsRunning() const { return m_Http != nullptr; }

        void SetAccess(ToolAccess access);
        size_t GetCallCount() const { return m_Protocol ? m_Protocol->GetCallCount() : 0; }
        uint16_t GetPort() const { return m_BoundPort; }

        // Main thread, once a frame: runs the tool calls that came in.
        void Pump();
        void PollMainThreadQueue() { Pump(); }

        static constexpr size_t MaxQueued = 64;
        static constexpr int CallTimeoutSeconds = 60;

    private:
        struct Pending
        {
            std::function<void()> Run;
            std::function<void(const std::string&)> Fail;
        };
        ToolResult Dispatch(const Tool& tool, const Json& arguments);

        Settings m_DefaultSettings;
        std::vector<Tool> m_RegisteredTools;
        uint16_t m_BoundPort = 0;

        std::unique_ptr<Protocol> m_Protocol;
        std::unique_ptr<httplib::Server> m_Http;
        std::thread m_Thread;
        std::mutex m_QueueMutex;
        std::deque<Pending> m_Queue;
        std::atomic<bool> m_Stopping{ false };
    };
}
