#include "core/oiv/oiv_package.h"

#include "core/io/ascii.h"
#include "core/rpf/rpf_toc.h"

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <sstream>

namespace archon {

namespace {

constexpr char format[] = "oiv";
constexpr std::uint64_t assemblyLimit = 16u << 20;
constexpr std::uint64_t iconLimit = 8u << 20;
constexpr std::uint64_t sourceLimit = 2048ull << 20;
constexpr std::uint32_t defaultHeaderColor = 0xFF23366A;
constexpr std::uint32_t defaultIconColor = 0xFF3B5998;

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { format, 0, std::move(message) });
}

bool isLegacy(std::string_view version)
{
    return version == "1.0" || version == "1.1";
}

std::string trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

std::string textOf(const pugi::xml_node &node)
{
    std::string text;
    for (const pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
            text += child.value();
    }
    return text;
}

bool isTrue(const pugi::xml_attribute &attribute)
{
    return asciiEqualIgnoreCase(trimmed(attribute.value()), "true");
}

std::uint32_t colorOf(const pugi::xml_node &node, std::uint32_t fallback)
{
    std::string text = trimmed(node.child_value());
    if (text.starts_with('$') || text.starts_with('#'))
        text.erase(0, 1);
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (text.empty() || error != std::errc() || end != text.data() + text.size())
        return fallback;
    return text.size() <= 6 ? value | 0xFF000000 : value;
}

Result<std::string> pathOf(std::string_view raw)
{
    auto path = normalizedOivPath(trimmed(raw));
    if (!path)
        return failure("\"" + std::string(raw) + "\" is not a safe path");
    return *path;
}

Result<OivText::Condition> conditionOf(const pugi::xml_node &node)
{
    const std::string condition = trimmed(node.attribute("condition").value());
    if (condition.empty() || asciiEqualIgnoreCase(condition, "Equal"))
        return OivText::Condition::Equal;
    if (asciiEqualIgnoreCase(condition, "StartWith"))
        return OivText::Condition::StartWith;
    if (asciiEqualIgnoreCase(condition, "Mask"))
        return OivText::Condition::Mask;
    return failure("unknown text condition \"" + condition + "\"");
}

Result<std::vector<OivText>> textCommands(const pugi::xml_node &parent)
{
    std::vector<OivText> commands;
    for (const pugi::xml_node node : parent.children()) {
        if (node.type() != pugi::node_element)
            continue;
        const std::string name = node.name();
        OivText command { OivText::Kind::Add, textOf(node), {}, OivText::Condition::Equal, false };
        if (name == "add") {
            commands.push_back(std::move(command));
            continue;
        }
        const auto condition = conditionOf(node);
        if (!condition)
            return std::unexpected(condition.error());
        command.condition = *condition;
        if (name == "insert") {
            command.kind = OivText::Kind::Insert;
            command.line = node.attribute("line").value();
            command.after = asciiEqualIgnoreCase(trimmed(node.attribute("where").value()), "After");
        } else if (name == "replace") {
            command.kind = OivText::Kind::Replace;
            command.line = node.attribute("line").value();
        } else if (name == "delete") {
            command.kind = OivText::Kind::Delete;
            command.line = std::move(command.value);
            command.value.clear();
        } else {
            return failure("unknown text command <" + name + ">");
        }
        commands.push_back(std::move(command));
    }
    return commands;
}

Result<std::vector<OivXml>> xmlCommands(const pugi::xml_node &parent)
{
    std::vector<OivXml> commands;
    for (const pugi::xml_node node : parent.children()) {
        if (node.type() != pugi::node_element)
            continue;
        const std::string name = node.name();
        OivXml command { OivXml::Kind::Add, trimmed(node.attribute("xpath").value()), OivXml::Append::Last, {} };
        if (command.xpath.empty())
            return failure("<" + name + "> has no xpath");
        if (name == "add") {
            const std::string append = trimmed(node.attribute("append").value());
            if (asciiEqualIgnoreCase(append, "First"))
                command.append = OivXml::Append::First;
            else if (asciiEqualIgnoreCase(append, "Before"))
                command.append = OivXml::Append::Before;
            else if (asciiEqualIgnoreCase(append, "After"))
                command.append = OivXml::Append::After;
            else if (!append.empty() && !asciiEqualIgnoreCase(append, "Last"))
                return failure("unknown append value \"" + append + "\"");
        } else if (name == "replace") {
            command.kind = OivXml::Kind::Replace;
        } else if (name == "remove") {
            command.kind = OivXml::Kind::Remove;
        } else {
            return failure("unknown xml command <" + name + ">");
        }
        std::ostringstream fragment;
        for (const pugi::xml_node child : node.children()) {
            if (child.type() == pugi::node_element)
                child.print(fragment, "", pugi::format_raw);
        }
        command.fragment = fragment.str();
        commands.push_back(std::move(command));
    }
    return commands;
}

Result<std::vector<OivCommand>> commandsOf(const pugi::xml_node &parent, bool legacy)
{
    std::vector<OivCommand> commands;
    for (const pugi::xml_node node : parent.children()) {
        if (node.type() != pugi::node_element)
            continue;
        const std::string name = node.name();
        OivCommand command { OivCommand::Kind::Add, {}, {}, isTrue(node.attribute("createIfNotExist")), {}, {}, {} };
        const bool isAdd = name == "add" || (legacy && name == "replace");
        const bool isArchive = name == (legacy ? "archive:open" : "archive");
        const bool isDefragment = legacy ? name == "archive:rebuild" : name == "defragmentation";
        const bool isText = name == (legacy ? "text:open" : "text");
        if (isAdd || name == "delete") {
            command.kind = isAdd ? OivCommand::Kind::Add : OivCommand::Kind::Delete;
            auto path = pathOf(node.child_value());
            if (!path)
                return std::unexpected(path.error());
            command.path = std::move(*path);
            if (isAdd) {
                auto source = pathOf(node.attribute("source").value());
                if (!source)
                    return std::unexpected(source.error());
                command.source = std::move(*source);
            }
        } else if (isArchive) {
            command.kind = OivCommand::Kind::Archive;
            const std::string type = trimmed(node.attribute("type").value());
            if (!type.empty() && !asciiEqualIgnoreCase(type, "RPF7"))
                return failure("archive type " + type + " is not a GTA V archive");
            auto path = pathOf(node.attribute("path").value());
            if (!path)
                return std::unexpected(path.error());
            command.path = std::move(*path);
            auto children = commandsOf(node, legacy);
            if (!children)
                return std::unexpected(children.error());
            command.children = std::move(*children);
        } else if (isDefragment) {
            command.kind = OivCommand::Kind::Defragment;
            auto path = pathOf(node.attribute(legacy ? "path" : "archive").value());
            if (!path)
                return std::unexpected(path.error());
            command.path = std::move(*path);
        } else if (isText) {
            command.kind = OivCommand::Kind::Text;
            auto path = pathOf(node.attribute("path").value());
            if (!path)
                return std::unexpected(path.error());
            command.path = std::move(*path);
            auto text = textCommands(node);
            if (!text)
                return std::unexpected(text.error());
            command.text = std::move(*text);
        } else if (!legacy && (name == "xml" || name == "pso")) {
            command.kind = name == "xml" ? OivCommand::Kind::Xml : OivCommand::Kind::Pso;
            auto path = pathOf(node.attribute("path").value());
            if (!path)
                return std::unexpected(path.error());
            command.path = std::move(*path);
            auto xml = xmlCommands(node);
            if (!xml)
                return std::unexpected(xml.error());
            command.xml = std::move(*xml);
        } else {
            return failure("unknown package command <" + name + ">");
        }
        commands.push_back(std::move(command));
    }
    return commands;
}

std::optional<std::string> zipNameFor(const ZipArchive &zip, std::string_view wanted)
{
    for (const std::string &name : zip.names()) {
        std::string normalized = name;
        std::ranges::replace(normalized, '\\', '/');
        if (asciiEqualIgnoreCase(normalized, wanted))
            return name;
    }
    return std::nullopt;
}

}

std::optional<std::string> normalizedOivPath(std::string_view path)
{
    std::string normalized;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos)
            end = path.size();
        const std::string_view part = path.substr(start, end - start);
        start = end + 1;
        if (part.empty() || part == ".")
            continue;
        if (part == ".." || part.find(':') != std::string_view::npos || !isValidEntryName(part))
            return std::nullopt;
        if (!normalized.empty())
            normalized += '/';
        normalized += part;
    }
    if (normalized.empty())
        return std::nullopt;
    return normalized;
}

Result<OivPackage> readOivPackage(const ZipArchive &zip)
{
    const auto assemblyName = zipNameFor(zip, "assembly.xml");
    if (!assemblyName)
        return failure("the package has no assembly.xml");
    const auto assembly = zip.read(*assemblyName, assemblyLimit);
    if (!assembly)
        return std::unexpected(assembly.error());
    pugi::xml_document document;
    const pugi::xml_parse_result parsed = document.load_buffer(assembly->data(), assembly->size(), pugi::parse_default);
    if (!parsed)
        return failure(std::string("assembly.xml is not valid XML: ") + parsed.description());
    const pugi::xml_node root = document.child("package");
    if (!root)
        return failure("assembly.xml has no <package> node");

    OivPackage package { trimmed(root.attribute("version").value()), {}, 1, 0, {}, {}, {}, {}, {}, {}, {}, defaultHeaderColor, false, defaultIconColor, {}, {} };
    const bool legacy = isLegacy(package.version);
    if (!legacy && package.version != "2.0" && package.version != "2.1" && package.version != "2.2")
        return failure("package version \"" + package.version + "\" is not supported");

    const pugi::xml_node metadata = root.child("metadata");
    package.name = trimmed(metadata.child_value("name"));
    if (package.name.empty())
        return failure("the package has no name");
    package.description = trimmed(textOf(metadata.child("description")));
    package.largeDescriptionTitle = trimmed(metadata.child("largeDescription").attribute("displayName").value());
    package.largeDescription = trimmed(textOf(metadata.child("largeDescription")));
    package.licence = trimmed(textOf(metadata.child("licence")));

    if (legacy) {
        package.author = trimmed(metadata.child_value("author"));
        std::vector<std::string> games;
        for (const pugi::xml_node content : root.children("content")) {
            const std::string game = trimmed(content.attribute("gameID").value());
            if (!asciiEqualIgnoreCase(game, "Five")) {
                if (std::ranges::find(games, game) == games.end())
                    games.push_back(game);
                continue;
            }
            auto commands = commandsOf(content, true);
            if (!commands)
                return std::unexpected(commands.error());
            package.contents.push_back({ trimmed(content.attribute("name").value()), trimmed(content.attribute("description").value()), std::move(*commands) });
        }
        if (package.contents.empty()) {
            std::string list;
            for (const std::string &game : games)
                list += (list.empty() ? "" : ", ") + game;
            return failure("This package is not for GTA V (it targets " + list + ").");
        }
    } else {
        const std::string target = trimmed(root.attribute("target").value());
        if (!asciiEqualIgnoreCase(target, "Five"))
            return failure("This package is not for GTA V (it targets " + target + ").");
        const pugi::xml_node version = metadata.child("version");
        package.major = std::atoi(version.child_value("major"));
        package.minor = std::atoi(version.child_value("minor"));
        package.tag = trimmed(version.child_value("tag"));
        const pugi::xml_node author = metadata.child("author");
        package.author = trimmed(author.child_value("displayName"));
        package.authorLink = trimmed(author.child_value("actionLink"));
        if (package.authorLink.empty())
            package.authorLink = trimmed(author.child_value("web"));
        const pugi::xml_node colors = root.child("colors");
        package.headerColor = colorOf(colors.child("headerBackground"), defaultHeaderColor);
        package.blackHeaderText = isTrue(colors.child("headerBackground").attribute("useBlackTextColor"));
        package.iconColor = colorOf(colors.child("iconBackground"), defaultIconColor);
        const pugi::xml_node content = root.child("content");
        if (!content)
            return failure("the package has no <content> node");
        auto commands = commandsOf(content, false);
        if (!commands)
            return std::unexpected(commands.error());
        package.contents.push_back({ "Install", {}, std::move(*commands) });
    }

    if (const auto icon = zipNameFor(zip, "icon.png")) {
        if (auto bytes = zip.read(*icon, iconLimit))
            package.icon = std::move(*bytes);
    }
    return package;
}

Result<std::vector<std::byte>> oivSource(const ZipArchive &zip, const OivPackage &package, std::string_view source)
{
    const std::string wanted = isLegacy(package.version) ? std::string(source) : "content/" + std::string(source);
    const auto name = zipNameFor(zip, wanted);
    if (!name)
        return failure("the package does not contain " + std::string(source));
    return zip.read(*name, sourceLimit);
}

}
