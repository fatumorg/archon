#include "core/search/entry_search.h"

#include "core/io/ascii.h"

namespace archon {

namespace {

bool matchesWildcard(std::string_view pattern, std::string_view name)
{
    std::size_t p = 0;
    std::size_t n = 0;
    std::size_t star = std::string_view::npos;
    std::size_t resume = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            resume = n;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            n = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

}

bool matchesPattern(std::string_view pattern, std::string_view name)
{
    if (pattern.empty())
        return false;
    const std::string lowerPattern = asciiLower(pattern);
    const std::string lowerName = asciiLower(name);
    if (lowerPattern.find_first_of("*?") == std::string::npos)
        return lowerName.find(lowerPattern) != std::string::npos;
    return matchesWildcard(lowerPattern, lowerName);
}

SearchResult searchEntries(GameBrowser &browser, std::string_view startPath, std::string_view pattern, const std::atomic<bool> &cancel, std::size_t limit)
{
    SearchResult result;
    std::vector<std::string> pending { std::string(startPath) };
    while (!pending.empty() && !cancel.load()) {
        const std::string location = std::move(pending.back());
        pending.pop_back();
        const auto nodes = browser.list(location);
        if (!nodes) {
            ++result.skipped;
            continue;
        }
        for (const BrowserNode &node : *nodes) {
            std::string path = location.empty() ? node.name : location + '/' + node.name;
            if (matchesPattern(pattern, node.name)) {
                if (result.hits.size() == limit) {
                    result.truncated = true;
                    return result;
                }
                result.hits.push_back({ path, node });
            }
            if (node.kind != NodeKind::File)
                pending.push_back(std::move(path));
        }
    }
    return result;
}

}
