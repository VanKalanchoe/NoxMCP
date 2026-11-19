#pragma once

#include <filesystem>
#include <string>

namespace NoxMcp
{
    // Creates the folder and its .gitignore when missing; false when it cannot be written.
    bool EnsureNoxFolder(const std::filesystem::path& folder);

    // Reads the 64-hex-digit token in <folder>/mcp-token, or generates and saves a new one when missing (or `rotate` is true).
    bool LoadOrCreateToken(const std::filesystem::path& folder, bool rotate, std::string& token);
}
