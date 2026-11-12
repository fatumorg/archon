#pragma once

#include "core/game/game_install.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace archon {

struct PinnedDownload {
    std::string_view title;
    std::string_view url;
    std::string_view archiveSha256;
    std::string_view member;
    std::string_view memberSha256;
};

const PinnedDownload &asiLoaderDownload();
const PinnedDownload &modsLoaderDownload();
Result<std::vector<std::byte>> pinnedContent(const PinnedDownload &download, std::span<const std::byte> bytes);
std::string_view loaderFileName(GameEdition edition);

struct AsiPlugin {
    std::string path;
    bool enabled;
    std::uint64_t size;
};

struct AsiState {
    bool loaderInstalled;
    bool loaderPinned;
    std::string modsLoader;
    std::vector<AsiPlugin> plugins;
};

class AsiManager {
public:
    AsiManager(std::filesystem::path root, GameEdition edition, std::filesystem::path store);

    AsiState state() const;
    Result<void> installLoader(std::span<const std::byte> dll);
    Result<void> uninstallLoader();
    Result<void> installModsLoader(std::span<const std::byte> asi);
    Result<void> uninstallModsLoader();
    Result<void> addPlugins(std::span<const std::filesystem::path> sources);
    Result<void> removePlugin(std::string_view path);
    Result<void> setPluginEnabled(std::string_view path, bool enabled);

private:
    std::filesystem::path root_;
    GameEdition edition_;
    std::filesystem::path store_;
};

}
