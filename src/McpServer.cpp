#include "NoxMcp/McpServer.h"
#include "NoxMcp/McpLog.h"

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <format>

#include "httplib.h"

namespace NoxMcp
{
    namespace
    {
        struct CallState
        {
            std::mutex Mutex;
            std::condition_variable Answered;
            bool Done = false;
            ToolResult Result;
            void Set(ToolResult result)
            {
                {
                    std::lock_guard lock(Mutex);
                    if (Done)
                        return;
                    Done = true;
                    Result = std::move(result);
                }
                Answered.notify_all();
            }
        };

        HttpRequest ToRequest(const httplib::Request& request)
        {
            HttpRequest converted;
            converted.Method = request.method;
            converted.Body = request.body;
            for (const auto& [name, value] : request.headers)
            {
                std::string lower = name;
                for (char& c : lower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                converted.Headers[lower] = value;
            }
            return converted;
        }

        void Reply(const HttpReply& reply, httplib::Response& response)
        {
            response.status = reply.Status;
            for (const auto& [name, value] : reply.Headers)
                response.set_header(name, value);
            if (!reply.Body.empty())
                response.set_content(reply.Body, reply.ContentType);
        }
    }

    McpServer::McpServer() = default;

    McpServer::~McpServer()
    {
        Stop();
    }

    void McpServer::RegisterTool(McpToolSchema schema, McpToolHandler handler)
    {
        Tool tool;
        tool.Name = std::move(schema.Name);
        tool.Description = std::move(schema.Description);
        if (!schema.InputJsonSchema.empty())
        {
            try
            {
                tool.InputSchema = Json::parse(schema.InputJsonSchema);
            }
            catch (const std::exception& e)
            {
                LogMessage(LogLevel::Warn, std::format("MCP: Invalid InputJsonSchema for tool '{}': {}", tool.Name, e.what()));
            }
        }
        tool.Access = ToolAccess::Read;
        tool.Handler = [cb = std::move(handler)](const Json& arguments, ToolDone done)
        {
            done(cb(arguments));
        };
        RegisterTool(std::move(tool));
    }

    void McpServer::RegisterTool(Tool tool)
    {
        m_RegisteredTools.push_back(std::move(tool));
    }

    void McpServer::SetServerInfo(std::string name, std::string title, std::string version)
    {
        m_DefaultSettings.ServerName = std::move(name);
        m_DefaultSettings.ServerTitle = std::move(title);
        m_DefaultSettings.ServerVersion = std::move(version);
    }

    bool McpServer::Start(uint16_t port)
    {
        Settings settings = m_DefaultSettings;
        settings.Port = port;
        settings.RequireAuth = !settings.Token.empty();
        std::string error;
        if (!Start(settings, m_RegisteredTools, error))
        {
            LogMessage(LogLevel::Error, std::format("MCP: Failed to start on port {}: {}", port, error));
            return false;
        }
        return true;
    }

    bool McpServer::Start(const Settings& settings, std::string& error)
    {
        return Start(settings, m_RegisteredTools, error);
    }

    bool McpServer::Start(const Settings& settings, std::vector<Tool> tools, std::string& error)
    {
        Stop();
        m_Stopping = false;
        m_Protocol = std::make_unique<Protocol>(settings);
        for (Tool& tool : tools)
            m_Protocol->AddTool(std::move(tool));
        m_Protocol->SetDispatcher([this](const Tool& tool, const Json& arguments) { return Dispatch(tool, arguments); });

        auto http = std::make_unique<httplib::Server>();
        http->set_socket_options([](socket_t socket)
        {
#ifdef _WIN32
            int exclusive = 1;
            setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
            int reuse = 1;
            setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
        });
        http->set_payload_max_length(8 * 1024 * 1024);
        http->set_read_timeout(30, 0);
        http->set_write_timeout(30, 0);
        const auto handler = [this](const httplib::Request& request, httplib::Response& response)
        {
            Reply(m_Protocol->Handle(ToRequest(request)), response);
        };
        http->Post("/mcp", handler);
        http->Get("/mcp", handler);
        http->Delete("/mcp", handler);
        http->Put("/mcp", handler);
        if (!http->bind_to_port("127.0.0.1", settings.Port))
        {
            error = std::format("port {} on 127.0.0.1 cannot be opened (another editor or program uses it? change mcp.Port)", settings.Port);
            m_Protocol.reset();
            return false;
        }
        m_BoundPort = settings.Port;
        m_Http = std::move(http);
        m_Thread = std::thread([server = m_Http.get()]() { server->listen_after_bind(); });
        return true;
    }

    void McpServer::Stop()
    {
        if (!m_Http)
            return;
        m_Stopping = true;
        std::deque<Pending> queued;
        {
            std::lock_guard lock(m_QueueMutex);
            queued.swap(m_Queue);
        }
        for (Pending& pending : queued)
            pending.Fail("the MCP server stopped before this call ran");
        m_Http->stop();
        if (m_Thread.joinable())
            m_Thread.join();
        m_Http.reset();
        m_Protocol.reset();
        m_BoundPort = 0;
    }

    void McpServer::SetAccess(ToolAccess access)
    {
        m_DefaultSettings.Access = access;
        if (m_Protocol)
            m_Protocol->SetAccess(access);
    }

    ToolResult McpServer::Dispatch(const Tool& tool, const Json& arguments)
    {
        auto state = std::make_shared<CallState>();
        {
            std::lock_guard lock(m_QueueMutex);
            if (m_Stopping)
                return ToolResult::Error("the MCP server is stopping");
            if (m_Queue.size() >= MaxQueued)
                return ToolResult::Error(std::format("the editor is busy ({} calls waiting); try again", m_Queue.size()));
            const Tool* toolPointer = &tool;
            m_Queue.push_back({ [toolPointer, arguments, state]()
                                {
                                    toolPointer->Handler(arguments, [state](ToolResult result) { state->Set(std::move(result)); });
                                },
                                [state](const std::string& message) { state->Set(ToolResult::Error(message)); } });
        }
        std::unique_lock lock(state->Mutex);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(CallTimeoutSeconds);
        while (!state->Done)
        {
            if (state->Answered.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(100))) ==
                    std::cv_status::timeout &&
                (std::chrono::steady_clock::now() >= deadline || m_Stopping))
                return ToolResult::Error(m_Stopping ? "the MCP server stopped" :
                                                      std::format("{} got no answer within {} s (the editor may be busy or loading)", tool.Name,
                                                                  CallTimeoutSeconds));
        }
        return state->Result;
    }

    void McpServer::Pump()
    {
        ScopedProfileZone zone("NoxMcp::McpServer::Pump");
        std::deque<Pending> calls;
        {
            std::lock_guard lock(m_QueueMutex);
            calls.swap(m_Queue);
        }
        for (Pending& call : calls)
        {
            try
            {
                call.Run();
            }
            catch (const std::exception& error)
            {
                LogMessage(LogLevel::Error, std::format("MCP: a tool failed: {}", error.what()));
                call.Fail(std::format("the tool failed: {}", error.what()));
            }
        }
    }
}
