#include "support/real_keys.h"

#include "support/real_install.h"

#include <memory>

namespace archon::test {

std::filesystem::path scratchDirectory()
{
    return std::filesystem::path(ARCHON_TEST_CACHE_DIR) / "scratch";
}

const Keys *realKeys()
{
    static std::unique_ptr<Keys> keys;
    if (keys || !realGameDir() || !realMagicDat())
        return keys.get();
    const auto loaded = loadKeys(*realGameDir() / "GTA5.exe", *realMagicDat(), std::filesystem::path(ARCHON_TEST_CACHE_DIR) / "test-keys.bin");
    if (!loaded) {
        qWarning("%s", loaded.error().message.c_str());
        return nullptr;
    }
    keys = std::make_unique<Keys>(*loaded);
    return keys.get();
}

}
