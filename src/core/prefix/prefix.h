#pragma once

#include "core/game/game_install.h"

#include <QStringList>

#include <filesystem>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace archon {

enum class PrefixComponent { DotNet48, VcRun2022, D3dCompiler47, Windows10, LoaderOverride };

std::string_view componentTitle(PrefixComponent component);
std::uint32_t steamAppId(GameEdition edition);
std::optional<std::filesystem::path> findPrefix(const std::filesystem::path &gameRoot, GameEdition edition);
std::map<PrefixComponent, bool> prefixStatus(const std::filesystem::path &prefix, GameEdition edition);
std::vector<QStringList> setupCommands(GameEdition edition, const std::map<PrefixComponent, bool> &status);
bool isGameRunning(const std::filesystem::path &proc);

}
