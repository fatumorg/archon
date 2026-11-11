#include "app/display_format.h"

#include <array>

namespace archon {

namespace {

struct KnownType {
    QLatin1StringView extension;
    QLatin1StringView name;
};

constexpr std::array knownTypes {
    KnownType { QLatin1StringView("xml"), QLatin1StringView("XML text") },
    KnownType { QLatin1StringView("meta"), QLatin1StringView("Meta (XML)") },
    KnownType { QLatin1StringView("ytd"), QLatin1StringView("Texture dictionary") },
    KnownType { QLatin1StringView("ydr"), QLatin1StringView("Model") },
    KnownType { QLatin1StringView("ydd"), QLatin1StringView("Model") },
    KnownType { QLatin1StringView("yft"), QLatin1StringView("Model") },
};

}

QString formatSize(std::uint64_t bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 bytes").arg(bytes);
    double value = static_cast<double>(bytes) / 1024;
    qsizetype unit = 0;
    constexpr std::array units { "Kb", "Mb", "Gb" };
    while (value >= 1024 && unit + 1 < static_cast<qsizetype>(units.size())) {
        value /= 1024;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(QString::number(value, 'g', 4), QLatin1StringView(units[static_cast<std::size_t>(unit)]));
}

QString sizeText(const BrowserNode &node)
{
    return node.kind == NodeKind::Folder ? QStringLiteral("%1 items").arg(node.size) : formatSize(node.size);
}

QString typeName(const BrowserNode &node)
{
    if (node.kind == NodeKind::Folder)
        return QStringLiteral("Folder");
    if (node.kind == NodeKind::Archive)
        return QStringLiteral("RPF Archive");
    const QString name = QString::fromStdString(node.name);
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == name.size() - 1)
        return QStringLiteral("File");
    const QString extension = name.mid(dot + 1).toLower();
    for (const KnownType &known : knownTypes) {
        if (extension == known.extension)
            return known.name;
    }
    return QStringLiteral("%1 file").arg(extension.toUpper());
}

QString attributesText(const BrowserNode &node)
{
    if (node.kind == NodeKind::Folder)
        return QStringLiteral("Folder");
    QString text;
    if (node.resource)
        text += QStringLiteral("Resource;");
    if (node.compressed)
        text += QStringLiteral("Compressed;");
    if (node.encrypted)
        text += QStringLiteral("Encrypted;");
    return text;
}

}
