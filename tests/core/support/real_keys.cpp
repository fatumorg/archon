#include "support/real_keys.h"

#include "support/real_install.h"

#include "core/game/game_install.h"

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
    const auto install = openGameInstall(*realGameDir());
    if (!install) {
        qWarning("%s", install.error().message.c_str());
        return nullptr;
    }
    const std::string cache = install->edition == GameEdition::Enhanced ? "test-keys-enhanced.bin" : "test-keys.bin";
    const auto loaded = loadKeys(install->executable, *realMagicDat(), std::filesystem::path(ARCHON_TEST_CACHE_DIR) / cache);
    if (!loaded) {
        qWarning("%s", loaded.error().message.c_str());
        return nullptr;
    }
    keys = std::make_unique<Keys>(*loaded);
    return keys.get();
}

}
