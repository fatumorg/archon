#pragma once

#include "core/game/game_browser.h"

#include <QString>

namespace archon {

QString formatSize(std::uint64_t bytes);
QString sizeText(const BrowserNode &node);
QString typeName(const BrowserNode &node);
QString attributesText(const BrowserNode &node);

}
