#pragma once

#include "core/rpf/rpf_extract.h"

#include <map>

namespace archon {

enum class NodeKind { Folder, Archive, File };

struct BrowserNode {
    std::string name;
    NodeKind kind;
    std::uint64_t size;
    bool inArchive;
    bool compressed;
    bool encrypted;
    bool resource;
};

class GameBrowser {
public:
    GameBrowser(std::filesystem::path root, std::shared_ptr<const Keys> keys);

    Result<std::vector<BrowserNode>> list(std::string_view path);
    Result<std::string> canonicalPath(std::string_view path);
    Result<ExtractSummary> extract(std::string_view path, const std::filesystem::path &outputDirectory);

private:
    struct Location {
        std::filesystem::path disk;
        std::shared_ptr<const RpfReader> reader;
        std::uint32_t entry = 0;
        std::string canonical;
        std::string name;
    };

    Result<Location> resolve(std::string_view path, bool enterArchive);
    Result<std::shared_ptr<const RpfReader>> archiveAt(const std::string &canonical, const Location &location);

    std::filesystem::path root_;
    std::shared_ptr<const Keys> keys_;
    std::map<std::string, std::shared_ptr<const RpfReader>> archives_;
};

}
