#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace NoxMcp
{
    enum class LogLevel : uint8_t
    {
        Info = 0,
        Warn,
        Error
    };

    using LogCallback = std::function<void(LogLevel level, std::string_view message)>;

    void SetLogCallback(LogCallback callback);
    void LogMessage(LogLevel level, std::string_view message);

    struct ProfilerCallbacks
    {
        std::function<void(const char* zoneName)> OnZoneBegin;
        std::function<void()> OnZoneEnd;
    };

    void SetProfilerCallbacks(ProfilerCallbacks callbacks);
    const ProfilerCallbacks& GetProfilerCallbacks();

    class ScopedProfileZone
    {
    public:
        explicit ScopedProfileZone(const char* zoneName)
        {
            const auto& cb = GetProfilerCallbacks();
            if (cb.OnZoneBegin)
            {
                cb.OnZoneBegin(zoneName);
                m_Active = true;
            }
        }

        ~ScopedProfileZone()
        {
            if (m_Active)
            {
                const auto& cb = GetProfilerCallbacks();
                if (cb.OnZoneEnd)
                    cb.OnZoneEnd();
            }
        }

        ScopedProfileZone(const ScopedProfileZone&) = delete;
        ScopedProfileZone& operator=(const ScopedProfileZone&) = delete;

    private:
        bool m_Active = false;
    };
}
