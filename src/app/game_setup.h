#pragma once

#include "core/game/game_install.h"

#include <memory>
#include <optional>
#include <span>

class QWidget;

namespace archon {

struct GameSession {
    GameInstall install;
    std::shared_ptr<const Keys> keys;
    std::filesystem::path store;
};

Result<void> checkMagic(std::span<const std::byte> bytes);
std::optional<GameInstall> chooseInstall(QWidget *parent);
std::optional<GameSession> openSession(QWidget *parent);

}
