#include "core/oiv/content_edit.h"

#include "core/io/ascii.h"
#include "core/search/entry_search.h"

#include <pugixml.hpp>

#include <algorithm>
#include <set>
#include <sstream>

namespace archon {

namespace {

constexpr char format[] = "oiv";

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { format, 0, std::move(message) });
}

bool matches(const OivText &edit, std::string_view line)
{
    switch (edit.condition) {
    case OivText::Condition::Equal:
        return line == edit.line;
    case OivText::Condition::StartWith:
        return line.starts_with(edit.line);
    case OivText::Condition::Mask:
        if (edit.line.find_first_of("*?") == std::string::npos)
            return asciiEqualIgnoreCase(line, edit.line);
        return matchesPattern(edit.line, line);
    }
    return false;
}

std::vector<std::byte> bytesOf(std::string_view text)
{
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

Result<std::vector<pugi::xml_node>> select(const pugi::xml_document &document, const std::string &xpath)
{
    try {
        const pugi::xpath_query query(xpath.c_str());
        std::vector<pugi::xml_node> nodes;
        for (const pugi::xpath_node &selected : query.evaluate_node_set(document)) {
            if (selected.node())
                nodes.push_back(selected.node());
        }
        return nodes;
    } catch (const pugi::xpath_exception &error) {
        return failure("XPath " + xpath + " is invalid: " + error.what());
    }
}

}

std::vector<std::byte> applyTextEdits(std::span<const std::byte> original, std::span<const OivText> edits)
{
    const std::string text(reinterpret_cast<const char *>(original.data()), original.size());
    const std::size_t firstBreak = text.find('\n');
    const std::string ending = firstBreak != std::string::npos && firstBreak > 0 && text[firstBreak - 1] == '\r' ? "\r\n" : firstBreak != std::string::npos ? "\n" : "\r\n";
    const bool trailing = !text.empty() && text.back() == '\n';
    std::vector<std::string> lines;
    for (std::size_t start = 0; start < text.size();) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        if (line.ends_with('\r'))
            line.pop_back();
        lines.push_back(std::move(line));
        start = end + 1;
    }

    for (const OivText &edit : edits) {
        switch (edit.kind) {
        case OivText::Kind::Add:
            lines.push_back(edit.value);
            break;
        case OivText::Kind::Insert: {
            const auto found = std::ranges::find_if(lines, [&edit](const std::string &line) { return matches(edit, line); });
            if (found != lines.end())
                lines.insert(edit.after ? found + 1 : found, edit.value);
            break;
        }
        case OivText::Kind::Replace:
            for (std::string &line : lines) {
                if (matches(edit, line))
                    line = edit.value;
            }
            break;
        case OivText::Kind::Delete:
            std::erase_if(lines, [&edit](const std::string &line) { return matches(edit, line); });
            break;
        }
    }

    std::string joined;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index > 0)
            joined += ending;
        joined += lines[index];
    }
    if (trailing && !lines.empty())
        joined += ending;
    return bytesOf(joined);
}

Result<std::vector<std::byte>> applyXmlEdits(std::span<const std::byte> original, std::span<const OivXml> edits)
{
    pugi::xml_document document;
    constexpr unsigned options = pugi::parse_default | pugi::parse_declaration | pugi::parse_comments;
    const pugi::xml_parse_result parsed = document.load_buffer(original.data(), original.size(), options);
    if (!parsed)
        return failure(std::string("the file is not valid XML: ") + parsed.description());

    for (const OivXml &edit : edits) {
        auto nodes = select(document, edit.xpath);
        if (!nodes)
            return std::unexpected(nodes.error());
        if (nodes->empty() && edit.kind != OivXml::Kind::Remove)
            return failure("XPath " + edit.xpath + " selects nothing");
        if (edit.kind != OivXml::Kind::Add) {
            std::set<pugi::xml_node_struct *> selected;
            for (const pugi::xml_node node : *nodes)
                selected.insert(node.internal_object());
            std::vector<pugi::xml_node> outermost;
            for (const pugi::xml_node node : *nodes) {
                bool nested = false;
                for (pugi::xml_node parent = node.parent(); parent && !nested; parent = parent.parent())
                    nested = selected.contains(parent.internal_object());
                if (!nested)
                    outermost.push_back(node);
            }
            *nodes = std::move(outermost);
        }
        pugi::xml_document fragment;
        const std::string wrapped = "<fragment>" + edit.fragment + "</fragment>";
        if (!fragment.load_buffer(wrapped.data(), wrapped.size(), pugi::parse_default))
            return failure("the package's XML for " + edit.xpath + " is not valid");
        const pugi::xml_node pieces = fragment.child("fragment");
        for (pugi::xml_node node : *nodes) {
            switch (edit.kind) {
            case OivXml::Kind::Add: {
                if (edit.append == OivXml::Append::Last) {
                    for (const pugi::xml_node piece : pieces.children())
                        node.append_copy(piece);
                } else if (edit.append == OivXml::Append::First) {
                    pugi::xml_node anchor = node.first_child();
                    for (const pugi::xml_node piece : pieces.children())
                        anchor ? node.insert_copy_before(piece, anchor) : node.append_copy(piece);
                } else if (edit.append == OivXml::Append::Before) {
                    for (const pugi::xml_node piece : pieces.children())
                        node.parent().insert_copy_before(piece, node);
                } else {
                    pugi::xml_node anchor = node;
                    for (const pugi::xml_node piece : pieces.children())
                        anchor = node.parent().insert_copy_after(piece, anchor);
                }
                break;
            }
            case OivXml::Kind::Replace:
                for (const pugi::xml_node piece : pieces.children())
                    node.parent().insert_copy_before(piece, node);
                node.parent().remove_child(node);
                break;
            case OivXml::Kind::Remove:
                node.parent().remove_child(node);
                break;
            }
        }
    }

    std::ostringstream out;
    document.save(out, "\t", pugi::format_indent | pugi::format_no_declaration, pugi::encoding_utf8);
    return bytesOf(out.str());
}

}
