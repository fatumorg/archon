#include "core/crypto/ng.h"
#include "support/real_install.h"
#include "support/real_keys.h"
#include "support/test_bytes.h"

#include <QTest>

#include <fstream>
#include <memory>

using namespace archon;
using namespace archon::test;

namespace {

std::vector<std::byte> firstEntry(const std::filesystem::path &archive)
{
    std::vector<std::byte> entry(16);
    std::ifstream stream(archive, std::ios::binary);
    stream.seekg(16);
    stream.read(reinterpret_cast<char *>(entry.data()), 16);
    return stream ? entry : std::vector<std::byte> {};
}

}

class TestNg : public QObject {
    Q_OBJECT
private slots:
    void ngDecryptKeepsPartialBlock();
    void decryptsRealRootEntry();
};

void TestNg::ngDecryptKeepsPartialBlock()
{
    const auto keys = std::make_unique<Keys>();
    const auto input = fromHex("0102030405060708090a0b0c0d0e0f1011121314");
    const auto output = ngDecrypt(*keys, input, "name.rpf", 20);
    QCOMPARE(output.size(), std::size_t(20));
    QCOMPARE(toHex(std::span(output).first(16)), std::string(32, '0'));
    QCOMPARE(toHex(std::span(output).last(4)), std::string("11121314"));
    QCOMPARE(ngHash(*keys, "name.rpf"), 0u);
}

void TestNg::decryptsRealRootEntry()
{
    ARCHON_REQUIRE_REAL();
    const Keys *keys = realKeys();
    QVERIFY(keys);
    const auto x64a = *realGameDir() / "x64a.rpf";
    QCOMPARE(toHex(ngDecrypt(*keys, firstEntry(x64a), "x64a.rpf", static_cast<std::uint32_t>(std::filesystem::file_size(x64a)))),
        std::string("0000000000ffff7f0100000005000000"));
    const auto common = *realGameDir() / "common.rpf";
    const auto root = ngDecrypt(*keys, firstEntry(common), "common.rpf", static_cast<std::uint32_t>(std::filesystem::file_size(common)));
    QCOMPARE(toHex(std::span(root).first(8)), std::string("0000000000ffff7f"));
}

QTEST_APPLESS_MAIN(TestNg)
#include "tst_ng.moc"
