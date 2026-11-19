# NoxMcp

A standalone C++20 **Model Context Protocol (MCP)** JSON-RPC 2.0 server, client, and schema validation library over Streamable HTTP (`cpp-httplib`).

`NoxMcp` has **zero** engine dependencies. Any C++ application (game engine, standalone editor, `NoxUI` tool, or headless service) can embed `NoxMcp::McpServer`, register custom tools via callbacks, and safely pump incoming tool invocations on its main thread.

---

## Features

- **MCP Streamable HTTP Protocol (`McpProtocol.h`):** Supports revisions `2025-06-18` and `2025-03-26`, `initialize`, `ping`, `tools/list`, `tools/call`, constant-time bearer token auth, loopback `Host` / `Origin` DNS-rebinding protection, fluent `Schema` builder, and argument validation.
- **Thread-Safe Main-Thread Dispatch (`McpServer.h`):** HTTP requests run on background threads while tool handlers are queued and executed on the host's main thread via `PollMainThreadQueue()` / `Pump()`.
- **MCP Client & Liveness Probe (`McpClient.h`):** Forward JSON-RPC tool calls (`CallServer`) to another local MCP process and monitor server startup asynchronously (`ServerProbe`).
- **JSON Schema & Patch Helpers (`McpValidation.h`):** Example-based schema validation, RFC 6902 JSON-Patch application, structural diffing, and recursive merging.
- **Token Management (`McpTokenFiles.h`):** Cryptographic 256-bit bearer token generation (`BCryptGenRandom` / `/dev/urandom`) and `.gitignore`-protected token folder persistence.
- **Hookable Callbacks (`McpLog.h`):** `SetLogCallback` (defaults to `stderr`) and optional `SetProfilerCallbacks`.

---

## Quick Integration Example

```cpp
#include <NoxMcp/NoxMcp.h>

NoxMcp::McpServer server;
server.SetToken(NoxMcp::GenerateToken());
server.RegisterTool(
    { "echo", "Echoes input text", R"({"type":"object","properties":{"text":{"type":"string"}},"required":["text"]})" },
    [](const NoxMcp::JsonValue& args) -> NoxMcp::McpToolResult
    {
        return NoxMcp::McpToolResult::Text(args["text"].get<std::string>());
    }
);

server.Start(8765);

// Inside your application's main loop:
server.PollMainThreadQueue();
```

---

## Quick Start (Fresh Clone)

### 1. Install Dependencies into `vendors/`
```bat
scripts\install_dependencies.bat
```
Clones `cpp-httplib` and downloads `nlohmann/json.hpp` into `vendors/`.

### 2. Generate the Solution (`.slnx`) for Rider / Visual Studio
```bat
:: Configure with Visual Studio 2026 + ClangCL (default), generating build\NoxMcp.slnx:
scripts\setup_build.bat

:: To configure with default MSVC instead of ClangCL:
scripts\setup_build.bat --msvc
```

Open **`build\NoxMcp.slnx`** in **JetBrains Rider** (or Visual Studio 2026) and build/run **`NoxMcpTest`**.
