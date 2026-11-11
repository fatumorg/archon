#pragma once

#include "core/rpf/rpf_archive.h"

#include <map>
#include <string>
#include <vector>

namespace archon::test {

class RpfBuilder {
public:
    void addDirectory(const std::string &path);
    void addBinary(const std::string &path, std::vector<std::byte> bytes, bool compress);
    void addResource(const std::string &path, const std::vector<std::byte> &pageData, std::uint32_t systemFlags, std::uint32_t graphicsFlags);
    void addResourceBody(const std::string &path, const std::vector<std::byte> &storedBody, std::uint32_t systemFlags, std::uint32_t graphicsFlags);
    std::vector<std::byte> build(RpfEncryption encryption = RpfEncryption::None) const;

private:
    struct Node {
        RpfEntryKind kind = RpfEntryKind::Directory;
        std::string name;
        std::map<std::string, Node> children;
        std::vector<std::byte> stored;
        std::uint32_t storedSize = 0;
        std::uint32_t uncompressedSize = 0;
        std::uint32_t systemFlags = 0;
        std::uint32_t graphicsFlags = 0;
    };

    Node &node(const std::string &path);

    Node root_;
};

}
