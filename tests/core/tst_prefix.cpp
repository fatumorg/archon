#include "core/prefix/prefix.h"
#include "support/temp_files.h"

#include <QTest>

using namespace archon;
using namespace archon::test;

namespace {

std::string nativeDll()
{
    std::string bytes(256, 'M');
    bytes.replace(0, 2, "MZ");
    return bytes;
}

std::string builtinDll()
{
    std::string bytes(256, '\0');
    bytes.replace(0, 2, "MZ");
    bytes.replace(64, 16, "Wine builtin DLL");
    return bytes;
}

const std::string userRegHeader = "WINE REGISTRY Version 2\n;; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n\n";
const std::string systemRegHeader = "WINE REGISTRY Version 2\n;; All keys relative to \\\\Machine\n\n";

}

class TestPrefix : public QObject {
    Q_OBJECT
private slots:
    void init();
    void builtinAndSymlinkAreNotPresent();
    void nativeFilesArePresent();
    void overrideReadFromUserReg();
    void windowsVersionFromUserRegOrSystemReg();
    void commandsCoverOnlyMissing();
    void dotNetIsFollowedByWindows10();
    void findPrefixFromLibraryLayout();
    void runningGameFoundInProc();
    void realPrefixReadsAsVerified();

private:
    std::filesystem::path system32() const
    {
        return prefix_ / "drive_c" / "windows" / "system32";
    }

    std::unique_ptr<QTemporaryDir> directory_;
    std::filesystem::path prefix_;
};

void TestPrefix::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
    prefix_ = pathOf(*directory_) / "pfx";
    writeFile(prefix_ / "user.reg", userRegHeader);
    writeFile(prefix_ / "system.reg", systemRegHeader);
    std::filesystem::create_directories(system32());
}

void TestPrefix::builtinAndSymlinkAreNotPresent()
{
    writeFile(system32() / "d3dcompiler_47.dll", builtinDll());
    writeFile(pathOf(*directory_) / "proton" / "msvcp140.dll", nativeDll());
    std::filesystem::create_symlink(pathOf(*directory_) / "proton" / "msvcp140.dll", system32() / "msvcp140.dll");
    writeFile(system32() / "vcruntime140.dll", nativeDll());
    const auto status = prefixStatus(prefix_, GameEdition::Legacy);
    QVERIFY(!status.at(PrefixComponent::D3dCompiler47));
    QVERIFY(!status.at(PrefixComponent::VcRun2022));
    QVERIFY(!status.at(PrefixComponent::DotNet48));
}

void TestPrefix::nativeFilesArePresent()
{
    writeFile(system32() / "d3dcompiler_47.dll", nativeDll());
    writeFile(system32() / "msvcp140.dll", nativeDll());
    writeFile(system32() / "vcruntime140.dll", nativeDll());
    writeFile(prefix_ / "drive_c" / "windows" / "Microsoft.NET" / "Framework64" / "v4.0.30319" / "clr.dll", nativeDll());
    const auto status = prefixStatus(prefix_, GameEdition::Legacy);
    QVERIFY(status.at(PrefixComponent::D3dCompiler47));
    QVERIFY(status.at(PrefixComponent::VcRun2022));
    QVERIFY(status.at(PrefixComponent::DotNet48));
}

void TestPrefix::overrideReadFromUserReg()
{
    QVERIFY(!prefixStatus(prefix_, GameEdition::Legacy).at(PrefixComponent::LoaderOverride));
    writeFile(prefix_ / "user.reg", userRegHeader + "[Software\\\\Wine\\\\DllOverrides] 1793687164\n#time=1dd6b5c341f083a\n\"d3d11\"=\"native\"\n\"DINPUT8\"=\"native,builtin\"\n\n");
    QVERIFY(prefixStatus(prefix_, GameEdition::Legacy).at(PrefixComponent::LoaderOverride));
    QVERIFY(!prefixStatus(prefix_, GameEdition::Enhanced).at(PrefixComponent::LoaderOverride));
    writeFile(prefix_ / "user.reg", userRegHeader + "[Software\\\\Wine\\\\DllOverrides] 1\n\"xinput1_4\"=\"builtin\"\n");
    QVERIFY(!prefixStatus(prefix_, GameEdition::Enhanced).at(PrefixComponent::LoaderOverride));
    writeFile(prefix_ / "user.reg", userRegHeader + "[Software\\\\Wine\\\\DllOverrides] 1\n\"xinput1_4\"=\"native\"\n");
    QVERIFY(prefixStatus(prefix_, GameEdition::Enhanced).at(PrefixComponent::LoaderOverride));
}

void TestPrefix::windowsVersionFromUserRegOrSystemReg()
{
    QVERIFY(!prefixStatus(prefix_, GameEdition::Legacy).at(PrefixComponent::Windows10));
    writeFile(prefix_ / "system.reg", systemRegHeader + "[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion] 1\n\"CurrentBuild\"=\"19045\"\n\"CurrentMajorVersionNumber\"=dword:0000000a\n\n");
    QVERIFY(prefixStatus(prefix_, GameEdition::Legacy).at(PrefixComponent::Windows10));
    writeFile(prefix_ / "user.reg", userRegHeader + "[Software\\\\Wine] 1\n\"Version\"=\"win7\"\n\n");
    QVERIFY(!prefixStatus(prefix_, GameEdition::Legacy).at(PrefixComponent::Windows10));
    writeFile(prefix_ / "user.reg", userRegHeader + "[Software\\\\Wine] 1\n\"Version\"=\"win11\"\n\n");
    QVERIFY(prefixStatus(prefix_, GameEdition::Legacy).at(PrefixComponent::Windows10));
}

void TestPrefix::commandsCoverOnlyMissing()
{
    std::map<PrefixComponent, bool> status {
        { PrefixComponent::DotNet48, true },
        { PrefixComponent::VcRun2022, true },
        { PrefixComponent::D3dCompiler47, false },
        { PrefixComponent::Windows10, true },
        { PrefixComponent::LoaderOverride, false },
    };
    const auto commands = setupCommands(GameEdition::Legacy, status);
    QCOMPARE(commands.size(), std::size_t(2));
    QCOMPARE(commands[0], QStringList({ "271590", "-q", "d3dcompiler_47" }));
    QCOMPARE(commands[1], QStringList({ "-c", "wine reg add HKCU\\\\Software\\\\Wine\\\\DllOverrides /v dinput8 /d native,builtin /f && \"$WINESERVER\" -w", "271590" }));
    for (auto &[component, present] : status)
        present = true;
    QVERIFY(setupCommands(GameEdition::Enhanced, status).empty());
    status[PrefixComponent::LoaderOverride] = false;
    const auto enhanced = setupCommands(GameEdition::Enhanced, status);
    QCOMPARE(enhanced.size(), std::size_t(1));
    QCOMPARE(enhanced[0], QStringList({ "-c", "wine reg add HKCU\\\\Software\\\\Wine\\\\DllOverrides /v xinput1_4 /d native,builtin /f && \"$WINESERVER\" -w", "3240220" }));
    QCOMPARE(steamAppId(GameEdition::Legacy), std::uint32_t(271590));
    QCOMPARE(steamAppId(GameEdition::Enhanced), std::uint32_t(3240220));
}

void TestPrefix::dotNetIsFollowedByWindows10()
{
    std::map<PrefixComponent, bool> status {
        { PrefixComponent::DotNet48, false },
        { PrefixComponent::VcRun2022, false },
        { PrefixComponent::D3dCompiler47, false },
        { PrefixComponent::Windows10, true },
        { PrefixComponent::LoaderOverride, true },
    };
    const auto commands = setupCommands(GameEdition::Legacy, status);
    QCOMPARE(commands.size(), std::size_t(4));
    QCOMPARE(commands[0], QStringList({ "271590", "-q", "vcrun2022" }));
    QCOMPARE(commands[1], QStringList({ "271590", "-q", "d3dcompiler_47" }));
    QCOMPARE(commands[2], QStringList({ "271590", "-q", "dotnet48" }));
    QCOMPARE(commands[3], QStringList({ "271590", "-q", "win10" }));
}

void TestPrefix::findPrefixFromLibraryLayout()
{
    const std::filesystem::path library = pathOf(*directory_) / "library";
    const std::filesystem::path game = library / "steamapps" / "common" / "Grand Theft Auto V";
    std::filesystem::create_directories(game);
    QVERIFY(!findPrefix(game, GameEdition::Legacy));
    std::filesystem::create_directories(library / "steamapps" / "compatdata" / "271590" / "pfx");
    QCOMPARE(*findPrefix(game, GameEdition::Legacy), library / "steamapps" / "compatdata" / "271590" / "pfx");
    QCOMPARE(*findPrefix(game.string() + "/", GameEdition::Legacy), library / "steamapps" / "compatdata" / "271590" / "pfx");
    QVERIFY(!findPrefix(game, GameEdition::Enhanced));
}

void TestPrefix::runningGameFoundInProc()
{
    const std::filesystem::path proc = pathOf(*directory_) / "proc";
    writeFile(proc / "1" / "cmdline", std::string("/usr/bin/steam\0-silent\0", 23));
    writeFile(proc / "self" / "cmdline", std::string("GTA5.exe\0", 9));
    QVERIFY(!isGameRunning(proc));
    writeFile(proc / "4242" / "cmdline", std::string("Z:\\data\\Gaming\\Grand Theft Auto V\\gta5.EXE\0", 44));
    QVERIFY(isGameRunning(proc));
    std::filesystem::remove_all(proc / "4242");
    writeFile(proc / "77" / "cmdline", std::string("/games/Grand Theft Auto V Enhanced/GTA5_Enhanced.exe\0", 54));
    QVERIFY(isGameRunning(proc));
}

void TestPrefix::realPrefixReadsAsVerified()
{
    const std::filesystem::path real = "/data/Gaming/Steam/steamapps/compatdata/271590/pfx";
    if (qEnvironmentVariableIsEmpty("ARCHON_GTA5_DIR") || !std::filesystem::exists(real))
        QSKIP("ARCHON_GTA5_DIR is not set or the Legacy prefix is absent");
    const auto found = findPrefix(qEnvironmentVariable("ARCHON_GTA5_DIR").toStdString(), GameEdition::Legacy);
    QVERIFY(found);
    QCOMPARE(*found, real);
    const auto status = prefixStatus(real, GameEdition::Legacy);
    QVERIFY(status.at(PrefixComponent::VcRun2022));
    QVERIFY(status.at(PrefixComponent::Windows10));
    QVERIFY(!status.at(PrefixComponent::D3dCompiler47));
    QVERIFY(!status.at(PrefixComponent::DotNet48));
}

QTEST_APPLESS_MAIN(TestPrefix)
#include "tst_prefix.moc"
