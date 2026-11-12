#include "core/oiv/oiv_writer.h"

#include "core/io/ascii.h"
#include "core/io/zip_writer.h"

#include <QUuid>

#include <pugixml.hpp>

#include <cstdio>
#include <sstream>

namespace archon {

namespace {

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "oiv", 0, std::move(message) });
}

std::string color(std::uint32_t value)
{
    char text[10];
    std::snprintf(text, sizeof text, "$%08X", value);
    return text;
}

void textNode(pugi::xml_node parent, const char *name, const std::string &value)
{
    parent.append_child(name).append_child(pugi::node_pcdata).set_value(value.c_str());
}

std::vector<std::string> components(const std::string &path)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        parts.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

std::string sourceName(std::size_t index, const PackageItem &item)
{
    return std::to_string(index + 1) + "/" + item.source.filename().string();
}

Result<void> validate(const PackageDraft &draft)
{
    if (draft.name.empty())
        return failure("The package needs a name.");
    if (draft.author.empty())
        return failure("The package needs an author.");
    if (draft.items.empty())
        return failure("The package has no content.");
    for (const PackageItem &item : draft.items) {
        if (!normalizedOivPath(item.target))
            return failure("\"" + item.target + "\" is not a safe target path");
        std::error_code error;
        if (item.kind == PackageItem::Kind::Add && !std::filesystem::is_regular_file(item.source, error))
            return failure("cannot read " + item.source.string());
    }
    return {};
}

}

Result<std::string> assemblyXml(const PackageDraft &draft, const std::string &id)
{
    if (auto valid = validate(draft); !valid)
        return std::unexpected(valid.error());
    pugi::xml_document document;
    pugi::xml_node declaration = document.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";
    pugi::xml_node package = document.append_child("package");
    package.append_attribute("version") = "2.2";
    package.append_attribute("id") = id.c_str();
    package.append_attribute("target") = "Five";

    pugi::xml_node metadata = package.append_child("metadata");
    textNode(metadata, "name", draft.name);
    pugi::xml_node version = metadata.append_child("version");
    textNode(version, "major", std::to_string(draft.major));
    textNode(version, "minor", std::to_string(draft.minor));
    if (!draft.tag.empty())
        textNode(version, "tag", draft.tag);
    pugi::xml_node author = metadata.append_child("author");
    textNode(author, "displayName", draft.author);
    if (!draft.authorLink.empty())
        textNode(author, "actionLink", draft.authorLink);
    textNode(metadata, "description", draft.description);
    if (!draft.largeDescription.empty()) {
        pugi::xml_node large = metadata.append_child("largeDescription");
        if (!draft.largeDescriptionTitle.empty())
            large.append_attribute("displayName") = draft.largeDescriptionTitle.c_str();
        large.append_child(pugi::node_pcdata).set_value(draft.largeDescription.c_str());
    }
    if (!draft.licence.empty())
        textNode(metadata, "licence", draft.licence);

    pugi::xml_node colors = package.append_child("colors");
    pugi::xml_node header = colors.append_child("headerBackground");
    header.append_attribute("useBlackTextColor") = draft.blackHeaderText ? "True" : "False";
    header.append_child(pugi::node_pcdata).set_value(color(draft.headerColor).c_str());
    textNode(colors, "iconBackground", color(draft.iconColor));

    pugi::xml_node content = package.append_child("content");
    std::vector<std::pair<std::string, pugi::xml_node>> open;
    for (std::size_t index = 0; index < draft.items.size(); ++index) {
        const PackageItem &item = draft.items[index];
        const std::vector<std::string> parts = components(*normalizedOivPath(item.target));
        std::vector<std::string> chain;
        std::string current;
        std::size_t inner = 0;
        for (std::size_t part = 0; part + 1 < parts.size(); ++part) {
            current += (current.empty() ? "" : "/") + parts[part];
            if (asciiLower(parts[part]).ends_with(".rpf")) {
                chain.push_back(current);
                current.clear();
                inner = part + 1;
            }
        }
        std::size_t shared = 0;
        while (shared < chain.size() && shared < open.size() && asciiEqualIgnoreCase(open[shared].first, chain[shared]))
            ++shared;
        open.resize(shared);
        for (std::size_t level = shared; level < chain.size(); ++level) {
            pugi::xml_node parent = level == 0 ? content : open[level - 1].second;
            pugi::xml_node archive = parent.append_child("archive");
            archive.append_attribute("path") = chain[level].c_str();
            archive.append_attribute("createIfNotExist") = "False";
            archive.append_attribute("type") = "RPF7";
            open.emplace_back(chain[level], archive);
        }
        std::string relative;
        for (std::size_t part = inner; part < parts.size(); ++part)
            relative += (relative.empty() ? "" : "/") + parts[part];
        pugi::xml_node parent = open.empty() ? content : open.back().second;
        pugi::xml_node command = parent.append_child(item.kind == PackageItem::Kind::Add ? "add" : "delete");
        if (item.kind == PackageItem::Kind::Add)
            command.append_attribute("source") = sourceName(index, item).c_str();
        command.append_child(pugi::node_pcdata).set_value(relative.c_str());
    }

    std::ostringstream out;
    document.save(out, "\t", pugi::format_indent, pugi::encoding_utf8);
    return out.str();
}

Result<std::vector<std::byte>> writeOivPackage(const PackageDraft &draft)
{
    const std::string id = QUuid::createUuid().toString(QUuid::WithBraces).toUpper().toStdString();
    const auto xml = assemblyXml(draft, id);
    if (!xml)
        return std::unexpected(xml.error());
    ZipWriter zip;
    const auto bytes = std::as_bytes(std::span(xml->data(), xml->size()));
    if (auto added = zip.add("assembly.xml", std::vector<std::byte>(bytes.begin(), bytes.end())); !added)
        return std::unexpected(added.error());
    if (!draft.iconPng.empty()) {
        if (auto added = zip.add("icon.png", draft.iconPng); !added)
            return std::unexpected(added.error());
    }
    for (std::size_t index = 0; index < draft.items.size(); ++index) {
        const PackageItem &item = draft.items[index];
        if (item.kind != PackageItem::Kind::Add)
            continue;
        if (auto added = zip.addFile("content/" + sourceName(index, item), item.source); !added)
            return std::unexpected(added.error());
    }
    return zip.finish();
}

}
