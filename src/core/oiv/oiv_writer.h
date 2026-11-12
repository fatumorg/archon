#pragma once

#include "core/oiv/oiv_package.h"

#include <filesystem>

namespace archon {

struct PackageItem {
    enum class Kind { Add, Delete };
    Kind kind;
    std::filesystem::path source;
    std::string target;
};

struct PackageDraft {
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
    std::vector<std::byte> iconPng;
    std::vector<PackageItem> items;
};

Result<std::string> assemblyXml(const PackageDraft &draft, const std::string &id);
Result<std::vector<std::byte>> writeOivPackage(const PackageDraft &draft);

}
