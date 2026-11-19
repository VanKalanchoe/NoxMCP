#include "NoxMcp/McpLog.h"

#include <cstdio>
#include <mutex>

namespace NoxMcp
{
    namespace
    {
        std::mutex g_LogMutex;
        LogCallback g_LogCallback;
        ProfilerCallbacks g_ProfilerCallbacks;
    }

    void SetLogCallback(LogCallback callback)
    {
        std::scoped_lock lock(g_LogMutex);
        g_LogCallback = std::move(callback);
    }

    void LogMessage(LogLevel level, std::string_view message)
    {
        LogCallback cb;
        {
            std::scoped_lock lock(g_LogMutex);
            cb = g_LogCallback;
        }
        if (cb)
        {
            cb(level, message);
            return;
        }

        const char* tag = level == LogLevel::Error ? "ERROR" : level == LogLevel::Warn ? "WARN" : "INFO";
        std::fprintf(stderr, "[NoxMcp][%s] %.*s\n", tag, static_cast<int>(message.size()), message.data());
    }

    void SetProfilerCallbacks(ProfilerCallbacks callbacks)
    {
        g_ProfilerCallbacks = std::move(callbacks);
    }

    const ProfilerCallbacks& GetProfilerCallbacks()
    {
        return g_ProfilerCallbacks;
    }
}
