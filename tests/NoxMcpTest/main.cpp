#include "NoxMcp/NoxMcp.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"

namespace
{
    int g_Passed = 0;
    int g_Failed = 0;

    template <typename... Args>
    void CheckImpl(bool condition, int line, std::format_string<Args...> fmt, Args&&... args)
    {
        const std::string message = std::format(fmt, std::forward<Args>(args)...);
        if (condition)
        {
            ++g_Passed;
            std::printf("  [PASS] %s\n", message.c_str());
        }
        else
        {
            ++g_Failed;
            std::fprintf(stderr, "  [FAIL] (line %d) %s\n", line, message.c_str());
        }
    }
}

#define CHECK(cond, ...) CheckImpl(static_cast<bool>(cond), __LINE__, __VA_ARGS__)

namespace McpTest
{
    using namespace NoxMcp;
    constexpr const char* Token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    HttpRequest Post(const Json& body, const char* token = Token, const char* host = "127.0.0.1:8765")
    {
        HttpRequest request;
        request.Method = "POST";
        request.Headers["host"] = host;
        if (token)
            request.Headers["authorization"] = std::string("Bearer ") + token;
        request.Headers["content-type"] = "application/json";
        request.Body = body.dump();
        return request;
    }

    Json Rpc(const std::string& method, const Json& params = Json::object(), int id = 1)
    {
        return Json{ { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", params } };
    }

    Json BodyOf(const HttpReply& reply)
    {
        try { return Json::parse(reply.Body); } catch (...) { return Json(); }
    }
}

static void TestMcpProtocol()
{
    using namespace McpTest;
    std::printf("\n== MCP protocol\n");

    Settings settings;
    settings.Token = Token;
    settings.Port = 8765;
    settings.Access = ToolAccess::Write;
    settings.Instructions = "Test server";
    Protocol protocol(settings);

    Tool echo;
    echo.Name = "echo";
    echo.Description = "Repeats a text";
    echo.InputSchema = Schema().String("text", "What to repeat", true).Integer("times", "How often", false, 1, 5);
    echo.Handler = [](const Json& arguments, ToolDone done)
    {
        std::string text;
        for (int index = 0; index < arguments.value("times", 1); ++index)
            text += arguments["text"].get<std::string>();
        done(ToolResult::Data(Json{ { "text", text } }));
    };
    protocol.AddTool(echo);

    Tool later;
    later.Name = "later";
    later.Description = "Answers from another thread a little later";
    later.Handler = [](const Json&, ToolDone done)
    {
        std::thread([done]() { std::this_thread::sleep_for(std::chrono::milliseconds(30)); done(ToolResult::Text("late")); }).detach();
    };
    protocol.AddTool(later);

    Tool quit;
    quit.Name = "quit";
    quit.Description = "Needs execute access";
    quit.Access = ToolAccess::Execute;
    quit.Destructive = true;
    quit.Handler = [](const Json&, ToolDone done) { done(ToolResult::Text("quit")); };
    protocol.AddTool(quit);

    // HTTP checks: token, Host, Origin, method, version header.
    HttpReply reply = protocol.Handle(Post(Rpc("ping"), nullptr));
    CHECK(reply.Status == 401 && reply.Headers.count("WWW-Authenticate"), "no token: 401 ({})", reply.Status);
    reply = protocol.Handle(Post(Rpc("ping"), "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeX"));
    CHECK(reply.Status == 401, "a wrong token: 401 ({})", reply.Status);
    reply = protocol.Handle(Post(Rpc("ping"), Token, "evil.example:8765"));
    CHECK(reply.Status == 403, "another Host (DNS rebinding): 403 ({})", reply.Status);
    HttpRequest fromPage = Post(Rpc("ping"));
    fromPage.Headers["origin"] = "https://evil.example";
    CHECK(protocol.Handle(fromPage).Status == 403, "a web page's Origin: 403");
    HttpRequest ownOrigin = Post(Rpc("ping"));
    ownOrigin.Headers["origin"] = "http://localhost:8765";
    ownOrigin.Headers["host"] = "localhost:8765";
    CHECK(protocol.Handle(ownOrigin).Status == 200, "its own origin and localhost: accepted");
    HttpRequest get = Post(Rpc("ping"));
    get.Method = "GET";
    reply = protocol.Handle(get);
    CHECK(reply.Status == 405 && reply.Headers["Allow"] == "POST", "GET (no event stream): 405, Allow: POST");
    HttpRequest oldVersion = Post(Rpc("ping"));
    oldVersion.Headers["mcp-protocol-version"] = "2024-11-05";
    CHECK(protocol.Handle(oldVersion).Status == 400, "an unsupported MCP-Protocol-Version header: 400");

    // JSON-RPC: parse errors, batches, notifications.
    HttpRequest garbage = Post(Json::object());
    garbage.Body = "{not json";
    reply = protocol.Handle(garbage);
    CHECK(reply.Status == 400 && BodyOf(reply)["error"]["code"] == RpcError::ParseError, "not JSON: parse error -32700");
    reply = protocol.Handle(Post(Json::array({ Rpc("ping") })));
    CHECK(BodyOf(reply)["error"]["code"] == RpcError::InvalidRequest, "a batch: invalid request (none in 2025-06-18)");
    reply = protocol.Handle(Post(Json{ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } }));
    CHECK(reply.Status == 202 && reply.Body.empty(), "a notification: 202 without a body ({})", reply.Status);
    reply = protocol.Handle(Post(Rpc("nonsense")));
    CHECK(BodyOf(reply)["error"]["code"] == RpcError::MethodNotFound, "an unknown method: -32601");

    // initialize: the client's revision when supported, else the newest.
    reply = protocol.Handle(Post(Rpc("initialize", Json{ { "protocolVersion", "2025-03-26" }, { "capabilities", Json::object() },
                                                         { "clientInfo", Json{ { "name", "test-client" }, { "version", "1" } } } })));
    Json body = BodyOf(reply);
    CHECK(body["result"]["protocolVersion"] == "2025-03-26" && body["result"]["capabilities"].contains("tools") &&
          body["result"]["serverInfo"]["name"] == "nox-editor" && body["result"]["instructions"] == "Test server",
          "initialize with 2025-03-26: answered with it, tools capability, server info, instructions");
    reply = protocol.Handle(Post(Rpc("initialize", Json{ { "protocolVersion", "2099-01-01" } })));
    CHECK(BodyOf(reply)["result"]["protocolVersion"] == LatestProtocolVersion, "an unknown revision: the server's newest ({})",
          BodyOf(reply)["result"]["protocolVersion"].dump());
    CHECK(BodyOf(protocol.Handle(Post(Rpc("ping", Json::object(), 7))))["id"] == 7, "ping answers with the request's id");

    // tools/list: only what the access grants, with annotations.
    body = BodyOf(protocol.Handle(Post(Rpc("tools/list"))));
    const Json& tools = body["result"]["tools"];
    bool hasQuit = false;
    for (const Json& tool : tools)
        hasQuit |= tool["name"] == "quit";
    CHECK(tools.size() == 2 && !hasQuit && tools[0]["inputSchema"]["required"][0] == "text" && tools[0]["annotations"]["readOnlyHint"] == true,
          "tools/list: {} tools for write access (the execute one hidden), schema and annotations", tools.size());

    // tools/call: result, structured content, validation, unknown tool, access, late answers.
    body = BodyOf(protocol.Handle(Post(Rpc("tools/call", Json{ { "name", "echo" }, { "arguments", Json{ { "text", "ab" }, { "times", 3 } } } }))));
    CHECK(body["result"]["isError"] == false && body["result"]["structuredContent"]["text"] == "ababab" &&
          body["result"]["content"][0]["type"] == "text", "a call: structured and text content ({})", body["result"]["structuredContent"].dump());
    body = BodyOf(protocol.Handle(Post(Rpc("tools/call", Json{ { "name", "echo" }, { "arguments", Json{ { "times", 2 } } } }))));
    CHECK(body["result"]["isError"] == true && body["result"]["content"][0]["text"].get<std::string>().find("'text' is required") != std::string::npos,
          "a missing required argument: an error result the model can read");
    body = BodyOf(protocol.Handle(Post(Rpc("tools/call", Json{ { "name", "echo" }, { "arguments", Json{ { "text", "a" }, { "times", 9 } } } }))));
    CHECK(body["result"]["isError"] == true, "out of the schema's range: refused ({})", body["result"]["content"][0]["text"].dump());
    body = BodyOf(protocol.Handle(Post(Rpc("tools/call", Json{ { "name", "echo" }, { "arguments", Json{ { "text", 5 } } } }))));
    CHECK(body["result"]["isError"] == true, "a wrong type: refused ({})", body["result"]["content"][0]["text"].dump());
    body = BodyOf(protocol.Handle(Post(Rpc("tools/call", Json{ { "name", "echo" }, { "arguments", Json{ { "text", "a" }, { "extra", 1 } } } }))));
    CHECK(body["result"]["isError"] == true, "an unknown argument: refused ({})", body["result"]["content"][0]["text"].dump());
    body = BodyOf(protocol.Handle(Post(Rpc("tools/call", Json{ { "name", "missing" } }))));
    CHECK(body["error"]["code"] == RpcError::InvalidParams, "an unknown tool: -32602");
    body = BodyOf(protocol.Handle(Post(Rpc("tools/call", Json{ { "name", "quit" } }))));
    CHECK(body["result"]["isError"] == true && body["result"]["content"][0]["text"].get<std::string>().find("execute") != std::string::npos,
          "a tool above the granted access: refused");
    protocol.SetAccess(ToolAccess::Execute);
    body = BodyOf(protocol.Handle(Post(Rpc("tools/call", Json{ { "name", "quit" } }))));
    CHECK(body["result"]["isError"] == false, "granted execute: it runs");
    body = BodyOf(protocol.Handle(Post(Rpc("tools/call", Json{ { "name", "later" } }))));
    CHECK(body["result"]["content"][0]["text"] == "late", "a tool that answers later from another thread");

    // Several clients at once.
    std::vector<std::thread> clients;
    std::atomic<int> good{ 0 };
    for (int client = 0; client < 8; ++client)
        clients.emplace_back([&, client]()
        {
            for (int call = 0; call < 25; ++call)
            {
                Json answer = BodyOf(protocol.Handle(Post(Rpc("tools/call", Json{ { "name", "echo" }, { "arguments", Json{ { "text", std::to_string(client) } } } },
                                                              call))));
                good += answer["id"] == call && answer["result"]["structuredContent"]["text"] == std::to_string(client);
            }
        });
    for (std::thread& thread : clients)
        thread.join();
    CHECK(good == 200, "8 clients x 25 calls at once: {} of 200 right", good.load());

    // Helpers.
    const std::string token = GenerateToken();
    CHECK(token.size() == 64 && token != GenerateToken(), "a token: 64 hex characters, different each time");
    const uint8_t bytes[] = { 'M', 'a', 'n', 'y' };
    CHECK(Base64(bytes, 3) == "TWFu" && Base64(bytes, 4) == "TWFueQ==" && Base64(bytes, 2) == "TWE=", "base64 ({} {} {})", Base64(bytes, 3),
          Base64(bytes, 4), Base64(bytes, 2));
    CHECK(ConstantTimeEquals("abc", "abc") && !ConstantTimeEquals("abc", "abd") && !ConstantTimeEquals("abc", "ab"), "constant-time compare");
}

static void TestMcpServer()
{
    using namespace McpTest;
    std::printf("\n== MCP server over HTTP\n");
    constexpr uint16_t port = 18765;

    std::atomic<bool> running{ true };
    std::atomic<int> frames{ 0 };
    std::vector<ToolDone> deferred;
    std::mutex deferredMutex;
    std::thread::id mainThread;

    Tool where;
    where.Name = "where";
    where.Description = "Says whether it ran on the main thread";
    where.Handler = [&](const Json&, ToolDone done) { done(ToolResult::Data(Json{ { "main", std::this_thread::get_id() == mainThread } })); };
    Tool nextFrame;
    nextFrame.Name = "next_frame";
    nextFrame.Description = "Answers on a later frame";
    nextFrame.Handler = [&](const Json&, ToolDone done)
    {
        std::lock_guard lock(deferredMutex);
        deferred.push_back(done);
    };
    Tool never;
    never.Name = "never";
    never.Description = "Never answers (Stop must still end the wait)";
    never.Handler = [](const Json&, ToolDone) {};

    Settings settings;
    settings.Token = Token;
    settings.Port = port;
    NoxMcp::McpServer server;
    std::string error;
    const bool started = server.Start(settings, { where, nextFrame, never }, error);
    CHECK(started, "listens on 127.0.0.1:{} {}", port, error);
    if (!started)
        return;
    NoxMcp::McpServer second;
    std::string secondError;
    CHECK(!second.Start(settings, {}, secondError) && !secondError.empty(), "a second server on the same port is refused: '{}'", secondError);

    std::thread editor([&]()
    {
        mainThread = std::this_thread::get_id();
        while (running)
        {
            std::vector<ToolDone> due;
            {
                std::lock_guard lock(deferredMutex);
                due.swap(deferred);
            }
            server.Pump();
            for (ToolDone& done : due)
                done(ToolResult::Text(std::format("answered at frame {}", frames.load())));
            ++frames;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    });
    while (mainThread == std::thread::id())
        std::this_thread::yield();

    httplib::Client client("127.0.0.1", port);
    client.set_read_timeout(10, 0);
    const httplib::Headers headers{ { "Authorization", std::string("Bearer ") + Token } };
    auto post = [&](const Json& body, const httplib::Headers& with) { return client.Post("/mcp", with, body.dump(), "application/json"); };

    auto noToken = post(Rpc("ping"), {});
    CHECK(noToken && noToken->status == 401, "over HTTP without the token: 401");
    auto init = post(Rpc("initialize", Json{ { "protocolVersion", "2025-06-18" } }), headers);
    CHECK(init && init->status == 200 && BodyOf(HttpReply{ 200, "", {}, init->body })["result"]["protocolVersion"] == "2025-06-18",
          "initialize over HTTP");
    auto notification = post(Json{ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } }, headers);
    CHECK(notification && notification->status == 202, "the initialized notification: 202");
    auto call = post(Rpc("tools/call", Json{ { "name", "where" } }), headers);
    Json answer = call ? Json::parse(call->body) : Json();
    CHECK(answer["result"]["structuredContent"]["main"] == true, "a tool runs on the editor's thread, not the HTTP one");
    const int before = frames;
    auto later = post(Rpc("tools/call", Json{ { "name", "next_frame" } }), headers);
    answer = later ? Json::parse(later->body) : Json();
    CHECK(answer["result"]["content"][0]["text"].get<std::string>().rfind("answered at frame", 0) == 0 && frames > before,
          "a tool answering on a later frame: '{}'", answer["result"]["content"][0]["text"].get<std::string>());

    std::vector<std::thread> clients;
    std::atomic<int> good{ 0 };
    for (int index = 0; index < 6; ++index)
        clients.emplace_back([&, index]()
        {
            httplib::Client own("127.0.0.1", port);
            for (int request = 0; request < 10; ++request)
            {
                auto reply = own.Post("/mcp", headers, Rpc("tools/call", Json{ { "name", "where" } }, index * 100 + request).dump(), "application/json");
                if (reply && reply->status == 200 && Json::parse(reply->body)["id"] == index * 100 + request)
                    ++good;
            }
        });
    for (std::thread& thread : clients)
        thread.join();
    CHECK(good == 60, "6 HTTP clients x 10 calls: {} of 60 answered", good.load());

    std::thread stuck([&]() { post(Rpc("tools/call", Json{ { "name", "never" } }), headers); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto stopStart = std::chrono::steady_clock::now();
    server.Stop();
    stuck.join();
    const double stopSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - stopStart).count();
    CHECK(stopSeconds < 2.0 && !server.IsRunning(), "Stop with a call waiting returns in {:.2f} s", stopSeconds);
    running = false;
    editor.join();
}

static void TestMcpClientAndHighLevelApi()
{
    using namespace McpTest;
    std::printf("\n== MCP client, token file, and high-level RegisterTool API\n");
    constexpr uint16_t port = 18766;

    NoxMcp::McpServer server;
    server.SetToken(Token);
    server.RegisterTool(
        McpToolSchema{
            .Name = "echo",
            .Description = "Answers its arguments, with an image",
            .InputJsonSchema = R"({"type":"object","properties":{"text":{"type":"string"}},"required":["text"],"additionalProperties":false})"
        },
        [](const JsonValue& arguments) -> McpToolResult
        {
            ToolResult result = ToolResult::Data(Json{ { "said", arguments["text"] } });
            result.AddImage("iVBORw0KGgo=", "image/png");
            return result;
        }
    );
    server.RegisterTool(
        McpToolSchema{ .Name = "fails", .Description = "A tool error", .InputJsonSchema = "" },
        [](const JsonValue&) -> McpToolResult
        {
            return ToolResult::Error("it failed");
        }
    );

    const bool started = server.Start(port);
    CHECK(started, "high-level McpServer::Start({}) succeeds", port);
    if (!started)
        return;

    std::atomic<bool> running{ true };
    std::thread appLoop([&]()
    {
        while (running)
        {
            server.PollMainThreadQueue();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    });

    const ClientReply list = CallServer(port, Token, "tools/list", Json::object(), 10);
    std::vector<std::string> names;
    for (const Json& tool : list.Result.value("tools", Json::array()))
        names.push_back(tool.value("name", std::string()));
    CHECK(list.Error.empty() && names == std::vector<std::string>({ "echo", "fails" }),
          "tools/list: {} tools registered via RegisterTool", names.size());

    const ClientReply call = CallServer(port, Token, "tools/call", Json{ { "name", "echo" }, { "arguments", Json{ { "text", "hello" } } } }, 10);
    const ToolResult forwarded = ToolResultFromJson(call.Result);
    const Json said{ { "said", "hello" } };
    CHECK(call.Error.empty() && !forwarded.IsError && forwarded.Structured == said, "a call's structured answer comes back");
    bool hasImage = false;
    for (const Json& item : forwarded.Content)
        hasImage = hasImage || (item.value("type", std::string()) == "image" && item.value("mimeType", std::string()) == "image/png");
    CHECK(hasImage, "an image in the answer is forwarded as it came");
    CHECK(forwarded.ToJson() == call.Result, "forwarded unchanged: the same result JSON");

    const ClientReply failed = CallServer(port, Token, "tools/call", Json{ { "name", "fails" } }, 10);
    CHECK(failed.Error.empty() && ToolResultFromJson(failed.Result).IsError, "a tool error is an answer (isError), not a transport error");

    auto up = ServerProbe::Start("test game", port, Token, 10.0);
    for (int wait = 0; wait < 200 && up->Get() == ServerProbe::State::Starting; ++wait)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    CHECK(up->Get() == ServerProbe::State::Up && std::string(ToString(up->Get())) == "ready", "the probe finds a running server: {}",
          ToString(up->Get()));

    running = false;
    appLoop.join();
    server.Stop();

    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "nox_mcp_token_test" / ".nox";
    std::error_code ignored;
    std::filesystem::remove_all(folder.parent_path(), ignored);
    std::string first, again, rotated;
    CHECK(NoxMcp::LoadOrCreateToken(folder, false, first) && first.size() == 64, "a token is made when there is none");
    CHECK(NoxMcp::LoadOrCreateToken(folder, false, again) && again == first, "read again: the same token");
    CHECK(NoxMcp::LoadOrCreateToken(folder, true, rotated) && rotated != first && rotated.size() == 64, "rotate: a new one");
    CHECK(std::filesystem::exists(folder / ".gitignore"), "the .nox folder carries its own .gitignore");
    CHECK(!NoxMcp::LoadOrCreateToken({}, false, again), "no folder: no token");
    std::filesystem::remove_all(folder.parent_path(), ignored);
}

static void TestMcpValidation()
{
    using namespace NoxMcp;
    std::printf("\n== MCP component edits (JSON validation & patch)\n");
    const Json light = Json::parse(R"({ "Color": [1, 1, 1], "Intensity": 10.0, "CastShadows": true, "Samples": 4, "Mode": "Realtime",
                                        "Mesh": "16531713622870338582", "Texture": "0", "Curve": [{ "Time": 0.0, "Value": 1.0 }],
                                        "Inner": { "Radius": 5.0 } })");

    CHECK(CheckAgainstExample(light, Json::parse(R"({ "Intensity": 3, "Color": [0.5, 0.2, 0.1] })")).empty(), "a whole number for a number, a vector of 3: fits");
    std::string problem = CheckAgainstExample(light, Json::parse(R"({ "Intensty": 3 })"));
    CHECK(problem.find("unknown field 'Intensty'") != std::string::npos && problem.find("Intensity") != std::string::npos,
          "a misspelled field: refused, the valid ones listed ({})", problem);
    CHECK(!CheckAgainstExample(light, Json::parse(R"({ "Color": [1, 1] })")).empty(), "a vector of another length: refused");
    CHECK(!CheckAgainstExample(light, Json::parse(R"({ "CastShadows": 1 })")).empty(), "1 for a bool: refused");
    problem = CheckAgainstExample(light, Json::parse(R"({ "Mesh": 16531713622870338582 })"));
    CHECK(problem.find("write it as a string") != std::string::npos, "an id as a JSON number: refused ({})", problem);
    CHECK(CheckAgainstExample(light, Json::parse(R"({ "Mesh": "10922256994577822127", "Texture": "6087766411216149246" })")).empty(),
          "ids as digit strings: fit");

    Json patched;
    problem = ApplyPatch(light, Json::parse(R"([{ "op": "replace", "path": "/Intensity", "value": 2.5 }, { "op": "replace", "path": "/Color/0", "value": 0.25 }])"), patched);
    CHECK(problem.empty() && patched["Intensity"] == 2.5 && patched["Color"][0] == 0.25 && patched["Samples"] == 4,
          "a patch: two fields changed, the rest as they were");
    CHECK(!ApplyPatch(light, Json::parse(R"([{ "op": "move", "from": "/A", "path": "/B" }])"), patched).empty(), "'move' is not allowed");

    const Json stored = Json::parse(R"({ "Intensity": 100.0, "Color": [0.25, 1.0000001, 1], "Mode": "Realtime" })");
    const Json differences = Differences(Json::parse(R"({ "Intensity": 500, "Color": [0.25, 1, 1], "Mode": "Realtime" })"), stored);
    CHECK(differences.size() == 1 && differences[0]["path"] == "/Intensity" && differences[0]["stored"] == 100.0,
          "what the engine stored differently: only the clamped field ({})", differences.dump());

    const Json merged = Merge(Json::parse(R"({ "A": 1, "B": { "C": 2, "D": 3 } })"), Json::parse(R"({ "B": { "C": 9 }, "E": 4 })"));
    CHECK(merged == Json::parse(R"({ "A": 1, "B": { "C": 9, "D": 3 }, "E": 4 })"), "defaults merged under current values");

    const Json schema = SchemaFromExample(light);
    CHECK(schema["Color"]["kind"] == "vector3" && schema["Mesh"]["kind"] == "id" && schema["Samples"]["kind"] == "number" &&
          schema["Intensity"]["kind"] == "number" && schema["Inner"]["kind"] == "object", "a schema from an example: kinds ({})", schema["Color"].dump());
}

// The timing of injected input (InputPlayer): holds per frame, shared keys, motion spread over the time; and the schemas such a
// tool's arguments are checked against.
static void TestMcpInputPlayer()
{
    using namespace McpTest;
    std::printf("\n== MCP inject_input timing\n");
    auto has = [](const std::vector<std::pair<int, bool>>& changes, int code, bool down)
    {
        return std::find(changes.begin(), changes.end(), std::make_pair(code, down)) != changes.end();
    };
    const double frame = 1.0 / 60.0;

    // A tap (no time): down for exactly one frame.
    {
        InputPlayer player;
        const uint64_t id = player.Add({ { 44 }, {}, 0.0f, 0.0f, 0.0f, 0.0 });
        InputPlayer::Frame first = player.Advance(0.0);
        CHECK(has(first.Keys, 44, true) && first.Finished.empty() && player.IsDown(44), "a tap: down in its first frame");
        InputPlayer::Frame second = player.Advance(frame);
        CHECK(has(second.Keys, 44, false) && second.Finished.size() == 1 && second.Finished[0] == id && player.Empty(),
              "a tap: released in the next frame, its call answered");
    }

    // W for 0.5 s with the mouse moving 300 px right: motion spread over the frames, all of it by the end.
    {
        InputPlayer player;
        player.Add({ { 26 }, {}, 300.0f, -60.0f, 0.0f, 0.5 });
        float movedX = 0.0f, movedY = 0.0f;
        int framesDown = 0;
        bool released = false;
        double now = 10.0;
        for (int i = 0; i < 100 && !released; ++i, now += frame)
        {
            InputPlayer::Frame step = player.Advance(now);
            movedX += step.MoveX;
            movedY += step.MoveY;
            released = has(step.Keys, 26, false);
            if (!released)
                ++framesDown;
            if (i == 10)
                CHECK(std::abs(movedX - 300.0f * 10.0f / 30.0f) < 0.5f, "motion follows the time: {:.1f} px after a third", movedX);
        }
        CHECK(released && framesDown >= 30 && framesDown <= 31, "W held for 0.5 s at 60 Hz: {} frames", framesDown);
        CHECK(std::abs(movedX - 300.0f) < 1e-3f && std::abs(movedY + 60.0f) < 1e-3f, "all the motion arrives: {:.3f}, {:.3f}", movedX, movedY);
    }

    // Two holds sharing a key: it stays down until both let go; a slow frame still delivers the rest of the motion.
    {
        InputPlayer player;
        player.Add({ { 26 }, {}, 0.0f, 0.0f, 0.0f, 1.0 });       // W for 1 s
        player.Add({ { 26, 44 }, { 1 }, 0.0f, 0.0f, 4.0f, 0.2 }); // W + Space + left button for 0.2 s, 4 notches
        InputPlayer::Frame start = player.Advance(0.0);
        CHECK(has(start.Keys, 26, true) && has(start.Keys, 44, true) && has(start.Buttons, 1, true) &&
              std::count_if(start.Keys.begin(), start.Keys.end(), [](const auto& c) { return c.first == 26; }) == 1,
              "shared key: one press for both holds");
        InputPlayer::Frame late = player.Advance(0.5); // (one slow frame past the short hold's end)
        CHECK(has(late.Keys, 44, false) && has(late.Buttons, 1, false) && !has(late.Keys, 26, false) && player.IsDown(26) &&
              std::abs(late.Scroll - 4.0f) < 1e-4f && late.Finished.size() == 1,
              "the short hold lets go of its own keys (all 4 notches), W stays down for the other");
        InputPlayer::Frame end = player.Advance(1.0);
        CHECK(has(end.Keys, 26, false) && player.Empty(), "W released when the last hold ends");
    }

    // Clear (Play ended): everything down comes back as a release; every hold is finished.
    {
        InputPlayer player;
        player.Add({ { 4, 7 }, { 3 }, 0.0f, 0.0f, 0.0f, 5.0 });
        player.Add({ { 22 }, {}, 0.0f, 0.0f, 0.0f, 5.0 }); // (not started yet: nothing to release)
        player.Advance(0.0);
        player.Add({ { 26 }, {}, 0.0f, 0.0f, 0.0f, 5.0 });
        InputPlayer::Frame cleared = player.Clear();
        CHECK(has(cleared.Keys, 4, false) && has(cleared.Keys, 7, false) && has(cleared.Keys, 22, false) && has(cleared.Buttons, 3, false) &&
              !has(cleared.Keys, 26, false) && cleared.Finished.size() == 3 && player.Empty(),
              "clear: the started holds' keys released, all three holds finished");
    }

    // The tool schemas: number and list arguments are checked.
    {
        const Json schema = Schema().Number("duration", "", false, 0.0, 10.0).Array("keys", "", "string").Build();
        CHECK(Protocol::Validate(schema, Json::parse(R"({ "duration": 0.25, "keys": ["W", "Space"] })")).empty(), "a number and a list of strings fit");
        CHECK(!Protocol::Validate(schema, Json::parse(R"({ "duration": 11 })")).empty() &&
              !Protocol::Validate(schema, Json::parse(R"({ "keys": ["W", 3] })")).empty() &&
              !Protocol::Validate(schema, Json::parse(R"({ "keys": "W" })")).empty(),
              "refused: a number over its maximum, a list with a number in it, a word for a list");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::mutex logMutex;
    std::vector<std::string> logs;
    NoxMcp::SetLogCallback([&](NoxMcp::LogLevel, std::string_view msg)
    {
        std::scoped_lock lock(logMutex);
        logs.emplace_back(msg);
    });

    TestMcpProtocol();
    TestMcpServer();
    TestMcpClientAndHighLevelApi();
    TestMcpValidation();
    TestMcpInputPlayer();

    CHECK(!logs.empty(), "custom SetLogCallback received {} log messages", logs.size());

    std::printf("\n============================================================================\n");
    std::printf("NoxMcpTest Summary: %d passed, %d failed\n", g_Passed, g_Failed);
    std::printf("============================================================================\n");
    return g_Failed == 0 ? 0 : 1;
}
