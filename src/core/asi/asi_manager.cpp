#include "core/asi/asi_manager.h"

#include "core/crypto/hash.h"
#include "core/io/ascii.h"
#include "core/io/zip_archive.h"
#include "core/rpf/rpf_toc.h"
#include "core/safety/transaction.h"

#include <QFile>

#include <algorithm>
#include <array>
#include <functional>

namespace archon {

namespace {

constexpr char format[] = "asi";
constexpr std::uint64_t memberLimit = 64u << 20;
constexpr std::string_view disabledSuffix = ".disabled";
constexpr std::array<std::string_view, 3> modsLoaders { "RageOpenV.asi", "OpenIV.asi", "OpenRPF.asi" };
constexpr std::array<std::string_view, 2> pluginFolders { "scripts", "plugins" };

const PinnedDownload asiLoader {
    "ASI Loader",
    "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.2/Ultimate-ASI-Loader-NoPDB_x64.zip",
    "fe964adf4de7f467ff39c440a4059de02e37d0574a62944eef2f55a35edafa2f",
    "dinput8.dll",
    "57ec56390c92a18b82f58855e63c93985e254d1e29d5f75cbc28dc08f96c8a12",
};

const PinnedDownload modsLoader {
    "Mods folder loader",
    "https://github.com/Chiheb-Bacha/RageOpenV/releases/download/v1.0/RageOpenV.zip",
    "c39d574caa9db4462b20110e87a8229e48deeb2819883800c8e54f1c6c80548c",
    "RageOpenV.asi",
    "951d53952dedcfd9375a0cb37814cb8a783f39880b68de8b22f23535deda56f1",
};

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { format, 0, std::move(message) });
}

std::string hexDigest(std::span<const std::byte> bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (const std::byte value : sha256(bytes)) {
        hex += digits[std::to_integer<unsigned>(value) >> 4];
        hex += digits[std::to_integer<unsigned>(value) & 0xF];
    }
    return hex;
}

std::string fileDigest(const std::filesystem::path &file)
{
    QFile opened(file);
    if (!opened.open(QIODevice::ReadOnly) || static_cast<std::uint64_t>(opened.size()) > memberLimit)
        return {};
    const QByteArray content = opened.readAll();
    return hexDigest(std::as_bytes(std::span(content.constData(), static_cast<std::size_t>(content.size()))));
}

bool isRegular(const std::filesystem::path &file)
{
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(file, error));
}

std::optional<std::filesystem::path> childIgnoringCase(const std::filesystem::path &directory, std::string_view name)
{
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (asciiEqualIgnoreCase(it->path().filename().string(), name))
            return it->path();
    }
    return std::nullopt;
}

bool isModsLoader(std::string_view name)
{
    return std::ranges::any_of(modsLoaders, [name](std::string_view known) { return asciiEqualIgnoreCase(known, name); });
}

bool isPluginName(std::string_view name)
{
    const std::string lower = asciiLower(name);
    return lower.ends_with(".asi") || lower.ends_with(".asi.disabled");
}

Result<void> copyInto(Transaction &transaction, const std::filesystem::path &target, const std::function<Result<void>(const std::filesystem::path &)> &write)
{
    return transaction.replaceFile(target, write);
}

Result<void> writeBytes(const std::filesystem::path &file, std::span<const std::byte> bytes)
{
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()))
        return failure("cannot write " + file.string());
    return {};
}

Result<void> copyFile(const std::filesystem::path &from, const std::filesystem::path &to)
{
    std::error_code error;
    if (!std::filesystem::copy_file(from, to, error) || error)
        return failure("cannot copy " + from.string());
    return {};
}

Result<void> run(const std::filesystem::path &store, const std::filesystem::path &root, const std::string &description, const std::function<Result<void>(Transaction &)> &work)
{
    auto transaction = Transaction::begin(store, root, description);
    if (!transaction)
        return std::unexpected(transaction.error());
    if (auto done = work(*transaction); !done)
        return done;
    return transaction->commit();
}

std::string plural(std::size_t count, std::string_view singular, std::string_view many)
{
    return std::to_string(count) + ' ' + std::string(count == 1 ? singular : many);
}

}

const PinnedDownload &asiLoaderDownload()
{
    return asiLoader;
}

const PinnedDownload &modsLoaderDownload()
{
    return modsLoader;
}

Result<std::vector<std::byte>> pinnedContent(const PinnedDownload &download, std::span<const std::byte> bytes)
{
    const std::string digest = hexDigest(bytes);
    if (digest == download.memberSha256)
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    if (digest == download.archiveSha256) {
        auto zip = ZipArchive::open(std::vector<std::byte>(bytes.begin(), bytes.end()));
        if (!zip)
            return std::unexpected(zip.error());
        auto member = zip->read(download.member, memberLimit);
        if (!member)
            return member;
        if (hexDigest(*member) == download.memberSha256)
            return member;
    }
    return failure(std::string(download.title) + " does not match the pinned SHA-256");
}

std::string_view loaderFileName(GameEdition edition)
{
    return edition == GameEdition::Enhanced ? "xinput1_4.dll" : "dinput8.dll";
}

AsiManager::AsiManager(std::filesystem::path root, GameEdition edition, std::filesystem::path store)
    : root_(std::move(root))
    , edition_(edition)
    , store_(std::move(store))
{
}

AsiState AsiManager::state() const
{
    AsiState state { false, false, {}, {} };
    if (const auto loader = childIgnoringCase(root_, loaderFileName(edition_)); loader && isRegular(*loader)) {
        state.loaderInstalled = true;
        state.loaderPinned = fileDigest(*loader) == asiLoader.memberSha256;
    }
    std::vector<std::pair<std::filesystem::path, std::string>> folders { { root_, {} } };
    std::error_code error;
    for (std::filesystem::directory_iterator it(root_, error), end; !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (it->is_directory(error) && std::ranges::any_of(pluginFolders, [&name](std::string_view folder) { return asciiEqualIgnoreCase(folder, name); }))
            folders.emplace_back(it->path(), name + '/');
    }
    for (const auto &[folder, prefix] : folders) {
        for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            const std::string name = it->path().filename().string();
            if (!isRegular(it->path()) || !isPluginName(name))
                continue;
            if (prefix.empty() && isModsLoader(name)) {
                if (state.modsLoader.empty())
                    state.modsLoader = name;
                continue;
            }
            state.plugins.push_back({ prefix + name, !asciiLower(name).ends_with(disabledSuffix), std::filesystem::file_size(it->path(), error) });
        }
    }
    std::ranges::sort(state.plugins, [](const AsiPlugin &left, const AsiPlugin &right) { return asciiLower(left.path) < asciiLower(right.path); });
    return state;
}

Result<void> AsiManager::installLoader(std::span<const std::byte> dll)
{
    const std::filesystem::path target = childIgnoringCase(root_, loaderFileName(edition_)).value_or(root_ / std::string(loaderFileName(edition_)));
    return run(store_, root_, "Install ASI Loader", [&](Transaction &transaction) {
        return copyInto(transaction, target, [dll](const std::filesystem::path &temporary) { return writeBytes(temporary, dll); });
    });
}

Result<void> AsiManager::uninstallLoader()
{
    const auto loader = childIgnoringCase(root_, loaderFileName(edition_));
    if (!loader || !isRegular(*loader))
        return failure("the ASI Loader is not installed");
    return run(store_, root_, "Uninstall ASI Loader", [&](Transaction &transaction) { return transaction.removeFile(*loader); });
}

Result<void> AsiManager::installModsLoader(std::span<const std::byte> asi)
{
    const std::filesystem::path target = childIgnoringCase(root_, modsLoaders[0]).value_or(root_ / std::string(modsLoaders[0]));
    const bool hasMods = childIgnoringCase(root_, "mods").has_value();
    return run(store_, root_, "Install mods folder loader", [&](Transaction &transaction) -> Result<void> {
        if (!hasMods) {
            if (auto made = transaction.createDirectories(root_ / "mods"); !made)
                return made;
        }
        return copyInto(transaction, target, [asi](const std::filesystem::path &temporary) { return writeBytes(temporary, asi); });
    });
}

Result<void> AsiManager::uninstallModsLoader()
{
    std::vector<std::filesystem::path> present;
    for (const std::string_view name : modsLoaders) {
        if (const auto found = childIgnoringCase(root_, name); found && isRegular(*found))
            present.push_back(*found);
    }
    if (present.empty())
        return failure("no mods folder loader is installed");
    return run(store_, root_, "Uninstall mods folder loader", [&](Transaction &transaction) -> Result<void> {
        for (const std::filesystem::path &file : present) {
            if (auto removed = transaction.removeFile(file); !removed)
                return removed;
        }
        return {};
    });
}

Result<void> AsiManager::addPlugins(std::span<const std::filesystem::path> sources)
{
    if (sources.empty())
        return failure("no plugins to add");
    for (const std::filesystem::path &source : sources) {
        const std::string name = source.filename().string();
        if (!isRegular(source) || !asciiLower(name).ends_with(".asi") || !isValidEntryName(name))
            return failure(name + " is not an ASI plugin");
    }
    return run(store_, root_, "Add " + plural(sources.size(), "ASI plugin", "ASI plugins"), [&](Transaction &transaction) -> Result<void> {
        for (const std::filesystem::path &source : sources) {
            const std::string name = source.filename().string();
            const std::filesystem::path target = childIgnoringCase(root_, name).value_or(root_ / name);
            if (auto copied = copyInto(transaction, target, [&source](const std::filesystem::path &temporary) { return copyFile(source, temporary); }); !copied)
                return copied;
        }
        return {};
    });
}

Result<void> AsiManager::removePlugin(std::string_view path)
{
    const auto plugins = state().plugins;
    const auto found = std::ranges::find(plugins, path, &AsiPlugin::path);
    if (found == plugins.end())
        return failure(std::string(path) + " is not an ASI plugin of this game");
    const std::string name = std::filesystem::path(found->path).filename().string();
    return run(store_, root_, "Remove ASI plugin " + name, [&](Transaction &transaction) { return transaction.removeFile(root_ / found->path); });
}

Result<void> AsiManager::setPluginEnabled(std::string_view path, bool enabled)
{
    const auto plugins = state().plugins;
    const auto found = std::ranges::find(plugins, path, &AsiPlugin::path);
    if (found == plugins.end())
        return failure(std::string(path) + " is not an ASI plugin of this game");
    if (found->enabled == enabled)
        return failure(std::string(path) + (enabled ? " is already enabled" : " is already disabled"));
    const std::filesystem::path from = root_ / found->path;
    const std::string current = from.filename().string();
    const std::string renamed = enabled ? current.substr(0, current.size() - disabledSuffix.size()) : current + std::string(disabledSuffix);
    const std::filesystem::path to = from.parent_path() / renamed;
    const std::string shown = enabled ? renamed : current;
    return run(store_, root_, (enabled ? "Enable ASI plugin " : "Disable ASI plugin ") + shown, [&](Transaction &transaction) -> Result<void> {
        if (auto copied = copyInto(transaction, to, [&from](const std::filesystem::path &temporary) { return copyFile(from, temporary); }); !copied)
            return copied;
        return transaction.removeFile(from);
    });
}

}
