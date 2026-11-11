#pragma once

#include "core/game/game_browser.h"

#include <atomic>

namespace archon {

bool matchesPattern(std::string_view pattern, std::string_view name);

struct SearchHit {
    std::string path;
    BrowserNode node;
};

struct SearchResult {
    std::vector<SearchHit> hits;
    std::uint64_t skipped = 0;
    bool truncated = false;
};

SearchResult searchEntries(GameBrowser &browser, std::string_view startPath, std::string_view pattern, const std::atomic<bool> &cancel, std::size_t limit = 100000);

}
