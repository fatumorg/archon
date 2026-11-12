#include "core/meta/rbf_file.h"

#include "core/meta/meta_xml.h"

#include <pugixml.hpp>

#include <bit>
#include <charconv>
#include <cstring>
#include <format>
#include <optional>
#include <unordered_map>

namespace archon {

namespace {

constexpr std::uint8_t closeToken = 0xFF;
constexpr std::uint8_t bytesToken = 0xFD;
constexpr std::uint8_t openType = 0x00;
constexpr std::uint8_t intType = 0x10;
constexpr std::uint8_t falseType = 0x20;
constexpr std::uint8_t trueType = 0x30;
constexpr std::uint8_t floatType = 0x40;
constexpr std::uint8_t vectorType = 0x50;
constexpr std::uint8_t stringType = 0x60;
constexpr std::size_t maxDescriptors = 0xFD;
constexpr std::size_t maxDepth = 256;

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "rbf", 0, std::move(message) });
}

class Input {
public:
    explicit Input(std::span<const std::byte> bytes)
        : bytes_(bytes)
    {
    }

    bool done() const
    {
        return at_ >= bytes_.size();
    }

    std::optional<std::uint64_t> number(std::size_t size)
    {
        if (bytes_.size() - at_ < size)
            return std::nullopt;
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < size; ++index)
            value |= std::to_integer<std::uint64_t>(bytes_[at_ + index]) << (8 * index);
        at_ += size;
        return value;
    }

    std::optional<std::span<const std::byte>> take(std::size_t size)
    {
        if (bytes_.size() - at_ < size)
            return std::nullopt;
        const auto taken = bytes_.subspan(at_, size);
        at_ += size;
        return taken;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t at_ = 4;
};

std::string floatText(float value)
{
    std::string text = shortestFloat(value);
    if (text.find_first_of(".exn") == std::string::npos)
        text += ".0";
    return text;
}

bool looksLikeFloat(std::string_view text)
{
    if (text.starts_with("0x") || text.find('.') != std::string_view::npos || text.find('e') != std::string_view::npos || text == "inf" || text == "-inf") {
        float value = 0;
        if (text.starts_with("0x"))
            return text.size() == 10;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc() && end == text.data() + text.size();
    }
    return false;
}

std::optional<float> floatValue(std::string_view text)
{
    if (text.starts_with("0x")) {
        std::uint32_t bits = 0;
        const auto [end, error] = std::from_chars(text.data() + 2, text.data() + text.size(), bits, 16);
        if (error != std::errc() || end != text.data() + text.size())
            return std::nullopt;
        return std::bit_cast<float>(bits);
    }
    float value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::string hexText(std::span<const std::byte> bytes)
{
    std::string text;
    for (const std::byte byte : bytes)
        text += std::format("{:02X}", std::to_integer<unsigned>(byte));
    return text;
}

class Output {
public:
    Result<void> element(pugi::xml_node node, std::size_t depth)
    {
        if (depth > maxDepth)
            return failure("the XML is nested too deeply");
        const std::string_view name = node.name();
        std::vector<pugi::xml_attribute> attributes(node.attributes_begin(), node.attributes_end());
        std::vector<pugi::xml_node> children;
        bool hasText = false;
        for (pugi::xml_node child : node.children()) {
            if (child.type() == pugi::node_element)
                children.push_back(child);
            else if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
                hasText = true;
        }
        if (children.empty() && !hasText && attributes.size() == 1 && std::string_view(attributes[0].name()) == "value")
            return value(name, attributes[0].value());
        if (children.empty() && !hasText && attributes.size() == 3 && std::string_view(attributes[0].name()) == "x" && std::string_view(attributes[1].name()) == "y" && std::string_view(attributes[2].name()) == "z") {
            const auto x = floatValue(attributes[0].value());
            const auto y = floatValue(attributes[1].value());
            const auto z = floatValue(attributes[2].value());
            if (x && y && z) {
                if (const auto written = descriptor(name, vectorType); !written)
                    return written;
                for (const float component : { *x, *y, *z })
                    number(std::bit_cast<std::uint32_t>(component), 4);
                return {};
            }
        }
        if (attributes.size() > 0xFFFF)
            return failure("an element has too many attributes");
        if (const auto written = descriptor(name, openType); !written)
            return written;
        number(0, 4);
        number(attributes.size(), 2);
        for (const pugi::xml_attribute attribute : attributes) {
            const std::string_view text = attribute.value();
            if (looksLikeFloat(text)) {
                if (const auto written = descriptor(attribute.name(), floatType); !written)
                    return written;
                number(std::bit_cast<std::uint32_t>(*floatValue(text)), 4);
                continue;
            }
            const auto bytes = utf8ToLatin1(text);
            if (!bytes || bytes->size() > 0x7FFF)
                return failure("attribute " + std::string(attribute.name()) + " cannot be stored");
            if (const auto written = descriptor(attribute.name(), stringType); !written)
                return written;
            number(bytes->size(), 2);
            out_.insert(out_.end(), bytes->begin(), bytes->end());
        }
        for (const pugi::xml_node child : children) {
            if (const auto written = element(child, depth + 1); !written)
                return written;
        }
        if (hasText) {
            std::vector<std::byte> content;
            if (node.attribute("content")) {
                const std::string text = node.child_value();
                if (text.size() % 2 != 0)
                    return failure("the content of " + std::string(name) + " is not hexadecimal");
                for (std::size_t index = 0; index < text.size(); index += 2) {
                    unsigned value = 0;
                    const auto [end, error] = std::from_chars(text.data() + index, text.data() + index + 2, value, 16);
                    if (error != std::errc() || end != text.data() + index + 2)
                        return failure("the content of " + std::string(name) + " is not hexadecimal");
                    content.push_back(static_cast<std::byte>(value));
                }
            } else {
                const auto bytes = utf8ToLatin1(node.child_value());
                if (!bytes)
                    return failure("the text of " + std::string(name) + " cannot be stored");
                content = *bytes;
                content.push_back(std::byte { 0 });
            }
            out_.push_back(std::byte { bytesToken });
            out_.push_back(std::byte { closeToken });
            number(content.size(), 4);
            out_.insert(out_.end(), content.begin(), content.end());
        }
        out_.push_back(std::byte { closeToken });
        out_.push_back(std::byte { closeToken });
        return {};
    }

    std::vector<std::byte> take()
    {
        return std::move(out_);
    }

private:
    Result<void> value(std::string_view name, std::string_view text)
    {
        if (text == "true" || text == "false")
            return descriptor(name, text == "true" ? trueType : falseType);
        if (looksLikeFloat(text)) {
            if (const auto written = descriptor(name, floatType); !written)
                return written;
            number(std::bit_cast<std::uint32_t>(*floatValue(text)), 4);
            return {};
        }
        std::int64_t integer = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), integer);
        if (text.empty() || error != std::errc() || end != text.data() + text.size() || integer < std::numeric_limits<std::int32_t>::min() || integer > std::numeric_limits<std::uint32_t>::max())
            return failure("the value of " + std::string(name) + " is not a number");
        if (const auto written = descriptor(name, intType); !written)
            return written;
        number(static_cast<std::uint32_t>(integer), 4);
        return {};
    }

    Result<void> descriptor(std::string_view name, std::uint8_t type)
    {
        const auto found = index_.find(std::string(name));
        if (found != index_.end()) {
            out_.push_back(static_cast<std::byte>(found->second));
            out_.push_back(static_cast<std::byte>(type));
            return {};
        }
        if (index_.size() >= maxDescriptors)
            return failure("the XML uses more than 253 different names");
        const auto index = static_cast<std::uint8_t>(index_.size());
        index_.emplace(std::string(name), index);
        out_.push_back(static_cast<std::byte>(index));
        out_.push_back(static_cast<std::byte>(type));
        number(name.size(), 2);
        const auto bytes = std::as_bytes(std::span(name.data(), name.size()));
        out_.insert(out_.end(), bytes.begin(), bytes.end());
        return {};
    }

    void number(std::uint64_t value, std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index)
            out_.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xFF));
    }

    std::vector<std::byte> out_ { std::byte { 'R' }, std::byte { 'B' }, std::byte { 'F' }, std::byte { '0' } };
    std::unordered_map<std::string, std::uint8_t> index_;
};

struct StringWriter : pugi::xml_writer {
    std::string text;
    void write(const void *data, std::size_t size) override
    {
        text.append(static_cast<const char *>(data), size);
    }
};

}

bool isRbfFile(std::span<const std::byte> bytes)
{
    return bytes.size() >= 4 && std::memcmp(bytes.data(), "RBF0", 4) == 0;
}

Result<std::string> rbfToXml(std::span<const std::byte> bytes)
{
    if (!isRbfFile(bytes))
        return failure("the file is not an RBF file");
    Input input(bytes);
    pugi::xml_document document;
    std::vector<pugi::xml_node> stack { document };
    std::vector<std::string> names;
    std::size_t pendingAttributes = 0;
    const auto cut = [] { return failure("the file is cut short"); };
    while (!input.done()) {
        const auto index = input.number(1);
        if (!index)
            return cut();
        if (*index == closeToken || *index == bytesToken) {
            const auto marker = input.number(1);
            if (!marker || *marker != closeToken)
                return failure("a token is malformed");
            if (*index == closeToken) {
                if (stack.size() <= 1 || pendingAttributes != 0)
                    return failure("an element closes that was never opened");
                stack.pop_back();
                continue;
            }
            const auto length = input.number(4);
            const auto content = length ? input.take(static_cast<std::size_t>(*length)) : std::nullopt;
            if (!content)
                return cut();
            if (stack.size() <= 1)
                return failure("text appears outside an element");
            pugi::xml_node node = stack.back();
            if (node.attribute("content")) {
                node.text().set(hexText(*content).c_str());
            } else {
                const std::size_t length = !content->empty() && content->back() == std::byte { 0 } ? content->size() - 1 : content->size();
                node.text().set(latin1ToUtf8(content->first(length)).c_str());
            }
            continue;
        }
        const auto type = input.number(1);
        if (!type)
            return cut();
        if (*index == names.size()) {
            const auto length = input.number(2);
            const auto name = length ? input.take(static_cast<std::size_t>(*length)) : std::nullopt;
            if (!name || name->empty())
                return cut();
            names.emplace_back(reinterpret_cast<const char *>(name->data()), name->size());
        }
        if (*index >= names.size())
            return failure("a token names a descriptor that does not exist");
        const std::string &name = names[static_cast<std::size_t>(*index)];
        pugi::xml_node parent = stack.back();
        if (pendingAttributes > 0) {
            --pendingAttributes;
            std::string text;
            switch (*type) {
            case stringType: {
                const auto length = input.number(2);
                const auto value = length ? input.take(static_cast<std::size_t>(*length)) : std::nullopt;
                if (!value)
                    return cut();
                text = latin1ToUtf8(*value);
                break;
            }
            case floatType: {
                const auto value = input.number(4);
                if (!value)
                    return cut();
                text = floatText(std::bit_cast<float>(static_cast<std::uint32_t>(*value)));
                break;
            }
            default:
                return failure("attribute " + name + " has a kind ARCHON does not read");
            }
            parent.append_attribute(name.c_str()).set_value(text.c_str());
            continue;
        }
        if (stack.size() > maxDepth)
            return failure("the file is nested too deeply");
        pugi::xml_node node = parent.append_child(name.c_str());
        switch (*type) {
        case openType: {
            const auto zero = input.number(4);
            const auto count = input.number(2);
            if (!zero || !count)
                return cut();
            if (*zero != 0)
                return failure("an element header holds data ARCHON does not read");
            pendingAttributes = static_cast<std::size_t>(*count);
            stack.push_back(node);
            break;
        }
        case intType: {
            const auto value = input.number(4);
            if (!value)
                return cut();
            node.append_attribute("value").set_value(std::to_string(static_cast<std::int32_t>(*value)).c_str());
            break;
        }
        case falseType:
        case trueType:
            node.append_attribute("value").set_value(*type == trueType ? "true" : "false");
            break;
        case floatType: {
            const auto value = input.number(4);
            if (!value)
                return cut();
            node.append_attribute("value").set_value(floatText(std::bit_cast<float>(static_cast<std::uint32_t>(*value))).c_str());
            break;
        }
        case vectorType:
            for (const char *axis : { "x", "y", "z" }) {
                const auto value = input.number(4);
                if (!value)
                    return cut();
                node.append_attribute(axis).set_value(floatText(std::bit_cast<float>(static_cast<std::uint32_t>(*value))).c_str());
            }
            break;
        default:
            return failure("a token has kind " + std::to_string(*type) + ", which ARCHON does not read");
        }
    }
    if (stack.size() != 1)
        return failure("the file ends inside an element");
    StringWriter writer;
    document.save(writer, "  ", pugi::format_indent, pugi::encoding_utf8);
    return std::move(writer.text);
}

Result<std::vector<std::byte>> rbfFromXml(std::string_view xml)
{
    pugi::xml_document document;
    const pugi::xml_parse_result parsed = document.load_buffer(xml.data(), xml.size(), pugi::parse_default & ~pugi::parse_eol, pugi::encoding_utf8);
    if (!parsed)
        return failure(std::string("the XML is not well formed: ") + parsed.description() + " at character " + std::to_string(parsed.offset));
    Output output;
    for (pugi::xml_node node : document.children()) {
        if (node.type() != pugi::node_element)
            continue;
        if (const auto written = output.element(node, 0); !written)
            return std::unexpected(written.error());
    }
    return output.take();
}

}
