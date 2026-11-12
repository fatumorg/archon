#include "core/prefix/prefix.h"

#include "core/io/ascii.h"

#include <QFile>

#include <algorithm>
#include <array>

namespace archon {

namespace {

constexpr std::string_view builtinMarker = "Wine builtin DLL";
constexpr qint64 headerLength = 128;
constexpr qint64 registryLimit = 256 << 20;
constexpr std::array<std::string_view, 2> gameExecutables { "gta5.exe", "gta5_enhanced.exe" };

std::string loaderDll(GameEdition edition)
{
    return edition == GameEdition::Enhanced ? "xinput1_4" : "dinput8";
}

bool isNative(const std::filesystem::path &file)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(file, error)))
        return false;
    QFile opened(file);
    if (!opened.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = opened.read(headerLength);
    return !header.contains(QByteArray::fromRawData(builtinMarker.data(), static_cast<qsizetype>(builtinMarker.size())));
}

std::string normalizedKey(std::string_view key)
{
    std::string normalized;
    for (std::size_t index = 0; index < key.size(); ++index) {
        if (key[index] == '\\' && index + 1 < key.size() && key[index + 1] == '\\')
            ++index;
        normalized += key[index];
    }
    return asciiLower(normalized);
}

std::optional<std::string> registryValue(const std::filesystem::path &file, std::string_view key, std::string_view name)
{
    QFile opened(file);
    if (!opened.open(QIODevice::ReadOnly) || opened.size() > registryLimit)
        return std::nullopt;
    const std::string wantedKey = asciiLower(key);
    const std::string wantedName = '"' + asciiLower(name) + "\"=";
    bool inKey = false;
    while (!opened.atEnd()) {
        const std::string line = opened.readLine().trimmed().toStdString();
        if (line.starts_with('[')) {
            const std::size_t close = line.find(']');
            inKey = close != std::string::npos && normalizedKey(std::string_view(line).substr(1, close - 1)) == wantedKey;
            continue;
        }
        if (inKey && asciiLower(line).starts_with(wantedName))
            return line.substr(wantedName.size());
    }
    return std::nullopt;
}

std::string unquoted(std::string value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return value.substr(1, value.size() - 2);
    return value;
}

bool isWindows10(const std::filesystem::path &prefix)
{
    if (const auto version = registryValue(prefix / "user.reg", "Software\\Wine", "Version")) {
        const std::string name = asciiLower(unquoted(*version));
        return name == "win10" || name == "win11";
    }
    const auto major = registryValue(prefix / "system.reg", "Software\\Microsoft\\Windows NT\\CurrentVersion", "CurrentMajorVersionNumber");
    return major && asciiLower(*major) == "dword:0000000a";
}

bool hasNativeOverride(const std::filesystem::path &prefix, GameEdition edition)
{
    const auto value = registryValue(prefix / "user.reg", "Software\\Wine\\DllOverrides", loaderDll(edition));
    return value && (asciiLower(unquoted(*value)).starts_with("native") || asciiLower(unquoted(*value)).starts_with("n,"));
}

}

std::string_view componentTitle(PrefixComponent component)
{
    switch (component) {
    case PrefixComponent::DotNet48:
        return ".NET Framework 4.8 (ScriptHookVDotNet, RAGE Plugin Hook, LSPDFR)";
    case PrefixComponent::VcRun2022:
        return "Visual C++ 2015-2022 runtime (ScriptHookV, native ASI plugins)";
    case PrefixComponent::D3dCompiler47:
        return "Direct3D shader compiler 47 (ReShade, ENB)";
    case PrefixComponent::Windows10:
        return "Windows version set to Windows 10";
    case PrefixComponent::LoaderOverride:
        return "ASI Loader DLL set to native";
    }
    return {};
}

std::uint32_t steamAppId(GameEdition edition)
{
    return edition == GameEdition::Enhanced ? 3240220 : 271590;
}

std::optional<std::filesystem::path> findPrefix(const std::filesystem::path &gameRoot, GameEdition edition)
{
    std::filesystem::path root = gameRoot.lexically_normal();
    if (root.filename().empty())
        root = root.parent_path();
    const std::filesystem::path steamapps = root.parent_path().parent_path();
    const std::filesystem::path prefix = steamapps / "compatdata" / std::to_string(steamAppId(edition)) / "pfx";
    std::error_code error;
    if (!std::filesystem::is_directory(prefix, error))
        return std::nullopt;
    return prefix;
}

std::map<PrefixComponent, bool> prefixStatus(const std::filesystem::path &prefix, GameEdition edition)
{
    const std::filesystem::path windows = prefix / "drive_c" / "windows";
    const std::filesystem::path system32 = windows / "system32";
    return {
        { PrefixComponent::DotNet48, isNative(windows / "Microsoft.NET" / "Framework64" / "v4.0.30319" / "clr.dll") },
        { PrefixComponent::VcRun2022, isNative(system32 / "msvcp140.dll") && isNative(system32 / "vcruntime140.dll") },
        { PrefixComponent::D3dCompiler47, isNative(system32 / "d3dcompiler_47.dll") },
        { PrefixComponent::Windows10, isWindows10(prefix) },
        { PrefixComponent::LoaderOverride, hasNativeOverride(prefix, edition) },
    };
}

std::vector<QStringList> setupCommands(GameEdition edition, const std::map<PrefixComponent, bool> &status)
{
    const QString appId = QString::number(steamAppId(edition));
    const auto missing = [&status](PrefixComponent component) {
        const auto found = status.find(component);
        return found == status.end() || !found->second;
    };
    const std::array<std::pair<PrefixComponent, const char *>, 3> verbs { {
        { PrefixComponent::VcRun2022, "vcrun2022" },
        { PrefixComponent::D3dCompiler47, "d3dcompiler_47" },
        { PrefixComponent::DotNet48, "dotnet48" },
    } };
    std::vector<QStringList> commands;
    for (const auto &[component, verb] : verbs) {
        if (missing(component))
            commands.push_back({ appId, QStringLiteral("-q"), QString::fromLatin1(verb) });
    }
    if (missing(PrefixComponent::Windows10) || missing(PrefixComponent::DotNet48))
        commands.push_back({ appId, QStringLiteral("-q"), QStringLiteral("win10") });
    if (missing(PrefixComponent::LoaderOverride)) {
        const QString command = QStringLiteral("wine reg add HKCU\\\\Software\\\\Wine\\\\DllOverrides /v %1 /d native,builtin /f && \"$WINESERVER\" -w").arg(QString::fromStdString(loaderDll(edition)));
        commands.push_back({ QStringLiteral("-c"), command, appId });
    }
    return commands;
}

bool isGameRunning(const std::filesystem::path &proc)
{
    std::error_code error;
    for (std::filesystem::directory_iterator it(proc, error), end; !error && it != end; it.increment(error)) {
        const std::string pid = it->path().filename().string();
        if (pid.empty() || !std::ranges::all_of(pid, [](char c) { return c >= '0' && c <= '9'; }))
            continue;
        QFile cmdline(it->path() / "cmdline");
        if (!cmdline.open(QIODevice::ReadOnly))
            continue;
        for (const QByteArray &argument : cmdline.read(64 << 10).split('\0')) {
            const std::string text = argument.toStdString();
            const std::size_t separator = text.find_last_of("/\\");
            const std::string name = asciiLower(separator == std::string::npos ? text : text.substr(separator + 1));
            if (std::ranges::find(gameExecutables, name) != gameExecutables.end())
                return true;
        }
    }
    return false;
}

}
