#pragma once

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>

namespace archon::test {

inline std::filesystem::path pathOf(const QTemporaryDir &directory)
{
    return std::filesystem::path(directory.path().toStdString());
}

inline void writeFile(const std::filesystem::path &file, std::span<const std::byte> content)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(content.data()), static_cast<std::streamsize>(content.size()));
}

inline void writeFile(const std::filesystem::path &file, std::string_view content = {})
{
    writeFile(file, std::as_bytes(std::span(content.data(), content.size())));
}

}
