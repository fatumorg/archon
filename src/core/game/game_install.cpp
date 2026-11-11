#include "core/game/game_install.h"

#include "core/io/ascii.h"
#include "core/io/path_resolver.h"
#include "core/rpf/rpf_reader.h"

#include <QFile>
#include <QXmlStreamReader>

#include <algorithm>

namespace archon {

namespace {

constexpr qint64 maxSteamFileSize = 1 << 20;
constexpr std::string_view dlcPrefix = "dlcpacks:";

std::vector<std::string> quotedStrings(const std::filesystem::path &file)
{
    std::vector<std::string> tokens;
    QFile source(file);
    if (!source.open(QIODevice::ReadOnly) || source.size() > maxSteamFileSize)
        return tokens;
    const std::string text = source.readAll().toStdString();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '"')
            continue;
        std::string token;
        for (++i; i < text.size() && text[i] != '"'; ++i) {
            if (text[i] == '\\' && i + 1 < text.size())
                ++i;
            token.push_back(text[i]);
        }
        if (i < text.size())
            tokens.push_back(std::move(token));
    }
    return tokens;
}

std::vector<std::string> valuesOf(const std::filesystem::path &file, std::string_view key)
{
    std::vector<std::string> values;
    const auto tokens = quotedStrings(file);
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == key)
            values.push_back(tokens[++i]);
    }
    return values;
}

}

Result<GameInstall> openGameInstall(const std::filesystem::path &root)
{
    if (const auto legacy = resolveCaseInsensitive(root, "GTA5.exe"))
        return GameInstall { root, GameEdition::Legacy, *legacy };
    if (const auto enhanced = resolveCaseInsensitive(root, "GTA5_Enhanced.exe"))
        return GameInstall { root, GameEdition::Enhanced, *enhanced };
    return std::unexpected(Error { "game", 0, "no GTA V executable in " + root.string() });
}

std::vector<GameInstall> findSteamInstalls(const std::filesystem::path &home)
{
    std::vector<GameInstall> installs;
    for (const char *steamRoot : { ".local/share/Steam", ".steam/steam" }) {
        for (const std::string &library : valuesOf(home / steamRoot / "steamapps" / "libraryfolders.vdf", "path")) {
            const std::filesystem::path steamApps = std::filesystem::path(library) / "steamapps";
            for (const char *manifest : { "appmanifest_271590.acf", "appmanifest_3240220.acf" }) {
                for (const std::string &directory : valuesOf(steamApps / manifest, "installdir")) {
                    std::error_code error;
                    const auto root = std::filesystem::canonical(steamApps / "common" / directory, error);
                    if (error || std::ranges::contains(installs, root, &GameInstall::root))
                        continue;
                    if (const auto install = openGameInstall(root))
                        installs.push_back(*install);
                }
            }
        }
    }
    return installs;
}

std::vector<std::filesystem::path> listArchives(const GameInstall &install)
{
    std::vector<std::filesystem::path> archives;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(install.root, error), end; !error && it != end; it.increment(error)) {
        std::error_code status;
        if (it.depth() == 0 && it->is_directory(status) && asciiEqualIgnoreCase(it->path().filename().string(), "mods")) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(status) && asciiEqualIgnoreCase(it->path().extension().string(), ".rpf"))
            archives.push_back(it->path().lexically_relative(install.root));
    }
    std::ranges::sort(archives);
    return archives;
}

Result<std::vector<std::string>> readDlcList(const GameInstall &install, const Keys &keys)
{
    const auto archive = resolveCaseInsensitive(install.root, "update/update.rpf");
    if (!archive)
        return std::unexpected(Error { "game", 0, "update/update.rpf not found in " + install.root.string() });
    const auto reader = RpfReader::open(openFileSource(*archive), archive->filename().string(), &keys);
    if (!reader)
        return std::unexpected(reader.error());
    const auto entry = reader->archive().find("common/data/dlclist.xml");
    if (!entry)
        return std::unexpected(Error { "game", 0, "dlclist.xml not found in update.rpf" });
    const auto data = reader->fileData(*entry);
    if (!data)
        return std::unexpected(data.error());

    std::vector<std::string> names;
    QXmlStreamReader xml(QByteArray::fromRawData(reinterpret_cast<const char *>(data->data()), static_cast<qsizetype>(data->size())));
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement || xml.name() != QLatin1StringView("Item"))
            continue;
        const std::string item = asciiLower(xml.readElementText().trimmed().toStdString());
        if (!item.starts_with(dlcPrefix))
            continue;
        const std::size_t first = item.find_first_not_of("/\\", dlcPrefix.size());
        const std::size_t last = item.find_last_not_of("/\\");
        if (first != std::string::npos)
            names.push_back(item.substr(first, last - first + 1));
    }
    if (xml.hasError())
        return std::unexpected(Error { "xml", static_cast<std::uint64_t>(xml.characterOffset()), "dlclist.xml: " + xml.errorString().toStdString() });
    return names;
}

}
