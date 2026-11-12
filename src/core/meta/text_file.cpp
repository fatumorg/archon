#include "core/meta/text_file.h"

#include "core/io/ascii.h"

#include <QStringDecoder>

#include <algorithm>
#include <array>

namespace archon {

namespace {

constexpr std::size_t textLimit = 64u << 20;
constexpr std::array<std::string_view, 12> textExtensions { ".meta", ".xml", ".dat", ".txt", ".ini", ".cfg", ".ide", ".ipl", ".log", ".csv", ".lua", ".json" };
constexpr std::array<unsigned char, 3> utf8Bom { 0xEF, 0xBB, 0xBF };

std::unexpected<Error> notText(std::string_view name)
{
    return std::unexpected(Error { "text", 0, std::string(name) + " is not a text file" });
}

}

bool isTextMetadataName(std::string_view name)
{
    const std::string lower = asciiLower(name);
    return std::ranges::any_of(textExtensions, [&lower](std::string_view extension) { return lower.ends_with(extension); });
}

Result<TextFile> decodeTextFile(std::span<const std::byte> bytes, std::string_view name)
{
    if (bytes.size() > textLimit)
        return std::unexpected(Error { "text", 0, std::string(name) + " is larger than 64 MiB" });
    TextFile file { {}, false, false, false };
    std::span<const std::byte> body = bytes;
    if (body.size() >= 3 && std::ranges::equal(body.first(3), utf8Bom, [](std::byte a, unsigned char b) { return std::to_integer<unsigned char>(a) == b; })) {
        file.bom = true;
        body = body.subspan(3);
    }
    std::size_t lineFeeds = 0;
    std::size_t carriageLines = 0;
    for (std::size_t index = 0; index < body.size(); ++index) {
        const auto value = std::to_integer<unsigned char>(body[index]);
        if (value == 0 || (value < 0x20 && value != '\t' && value != '\r' && value != '\n' && value != 0x0C))
            return notText(name);
        if (value == '\n') {
            ++lineFeeds;
            if (index > 0 && std::to_integer<unsigned char>(body[index - 1]) == '\r')
                ++carriageLines;
        }
    }
    file.crlf = carriageLines > 0 && carriageLines * 2 >= lineFeeds;
    const QByteArray raw(reinterpret_cast<const char *>(body.data()), static_cast<qsizetype>(body.size()));
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8.decode(raw);
    if (utf8.hasError()) {
        if (file.bom)
            return notText(name);
        file.latin1 = true;
        text = QString::fromLatin1(raw);
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    file.text = std::move(text);
    return file;
}

std::vector<std::byte> encodeTextFile(const TextFile &file, const QString &text)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (file.crlf)
        normalized.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    const QByteArray encoded = file.latin1 ? normalized.toLatin1() : normalized.toUtf8();
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(encoded.size()) + 3);
    if (file.bom) {
        for (const unsigned char value : utf8Bom)
            bytes.push_back(static_cast<std::byte>(value));
    }
    const auto body = std::as_bytes(std::span(encoded.constData(), static_cast<std::size_t>(encoded.size())));
    bytes.insert(bytes.end(), body.begin(), body.end());
    return bytes;
}

}
