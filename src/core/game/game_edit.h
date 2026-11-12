#pragma once

#include "core/game/game_browser.h"
#include "core/safety/transaction.h"

#include <memory>
#include <span>

namespace archon {

class GameEditor {
public:
    GameEditor(std::filesystem::path root, std::shared_ptr<const Keys> keys, std::filesystem::path store);

    Result<void> import(std::string_view folder, std::span<const std::filesystem::path> sources);
    Result<void> replace(std::string_view file, const std::filesystem::path &source);
    Result<void> remove(std::span<const std::string> paths);
    Result<void> makeFolder(std::string_view folder, std::string_view name);
    Result<void> rename(std::string_view path, std::string_view newName);
    Result<void> rebuild(std::string_view archive);
    Result<std::string> copyToMods(std::string_view path);

private:
    Result<EditTarget> locate(std::string_view path, bool enterArchive) const;
    Result<void> run(const std::string &description, const std::function<Result<void>(Transaction &)> &work) const;

    std::filesystem::path root_;
    std::shared_ptr<const Keys> keys_;
    std::filesystem::path store_;
};

bool isInMods(std::string_view path);
bool isInArchive(GameBrowser &browser, std::string_view path);

}
