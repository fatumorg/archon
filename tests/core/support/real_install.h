#pragma once

#include <QTest>

#include <cstdlib>
#include <filesystem>
#include <optional>

namespace archon::test {

inline std::optional<std::filesystem::path> environmentPath(const char *name)
{
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0')
        return std::nullopt;
    return std::filesystem::path(value);
}

inline std::optional<std::filesystem::path> realGameDir()
{
    return environmentPath("ARCHON_GTA5_DIR");
}

inline std::optional<std::filesystem::path> realMagicDat()
{
    return environmentPath("ARCHON_MAGIC_DAT");
}

}

#define ARCHON_REQUIRE_REAL() \
    do { \
        if (!archon::test::realGameDir() || !archon::test::realMagicDat()) \
            QSKIP("ARCHON_GTA5_DIR and ARCHON_MAGIC_DAT are not both set"); \
    } while (false)
