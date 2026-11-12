#pragma once

#include "core/io/zip_archive.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace archon {

struct OivText {
    enum class Kind { Add, Insert, Replace, Delete };
    enum class Condition { Equal, StartWith, Mask };
    Kind kind;
    std::string value;
    std::string line;
    Condition condition;
    bool after;
};

struct OivXml {
    enum class Kind { Add, Replace, Remove };
    enum class Append { First, Last, Before, After };
    Kind kind;
    std::string xpath;
    Append append;
    std::string fragment;
};

struct OivCommand {
    enum class Kind { Add, Delete, Archive, Defragment, Text, Xml, Pso };
    Kind kind;
    std::string path;
    std::string source;
    bool createIfNotExist;
    std::vector<OivCommand> children;
    std::vector<OivText> text;
    std::vector<OivXml> xml;
};

struct OivContent {
    std::string name;
    std::string description;
    std::vector<OivCommand> commands;
};

struct OivPackage {
    std::string version;
    std::string name;
    int major;
    int minor;
    std::string tag;
    std::string author;
    std::string authorLink;
    std::string description;
    std::string largeDescriptionTitle;
    std::string largeDescription;
    std::string licence;
    std::uint32_t headerColor;
    bool blackHeaderText;
    std::uint32_t iconColor;
    std::vector<std::byte> icon;
    std::vector<OivContent> contents;
};

Result<OivPackage> readOivPackage(const ZipArchive &zip);
std::optional<std::string> normalizedOivPath(std::string_view path);
Result<std::vector<std::byte>> oivSource(const ZipArchive &zip, const OivPackage &package, std::string_view source);

}
