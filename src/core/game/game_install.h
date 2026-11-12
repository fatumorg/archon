#pragma once

#include "core/crypto/keys.h"
#include "core/io/error.h"

#include <filesystem>
#include <string>
#include <vector>

namespace archon {

enum class GameEdition { Legacy, Enhanced };

struct GameInstall {
    std::filesystem::path root;
    GameEdition edition;
    std::filesystem::path executable;
};

Result<GameInstall> openGameInstall(const std::filesystem::path &root);
std::vector<GameInstall> findSteamInstalls(const std::filesystem::path &home);
bool isSameFolder(const std::filesystem::path &first, const std::filesystem::path &second);
std::vector<std::filesystem::path> listArchives(const GameInstall &install);
Result<std::vector<std::string>> readDlcList(const GameInstall &install, const Keys &keys);

}
