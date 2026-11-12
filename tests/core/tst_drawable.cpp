#include "core/drawable/drawable.h"
#include "core/io/ascii.h"
#include "core/meta/name_table.h"
#include "core/rpf/rpf_reader.h"
#include "support/drawable_samples.h"
#include "support/real_install.h"
#include "support/real_keys.h"

#include <QTest>

#include <bit>
#include <functional>

using namespace archon;
using namespace archon::test;

class TestDrawable : public QObject {
    Q_OBJECT
private slots:
    void ydrReadsTriangleWithDiffuse();
    void embeddedTexturesAreRead();
    void yddListsEachDrawable();
    void yftUsesItsDrawable();
    void halfAndDec3nComponentsDecode();
    void hostileDrawablesAreRefused();
    void otherFilesAreNotDrawables();
    void realDrawablesRead();
};

namespace {

void put(std::vector<std::byte> &bytes, std::size_t at, std::uint64_t value, int size)
{
    for (int i = 0; i < size; ++i)
        bytes[at + i] = static_cast<std::byte>(value >> (8 * i));
}

void append(std::vector<std::byte> &bytes, std::uint64_t value, int size)
{
    for (int i = 0; i < size; ++i)
        bytes.push_back(static_cast<std::byte>(value >> (8 * i)));
}

}

void TestDrawable::ydrReadsTriangleWithDiffuse()
{
    const auto drawables = readDrawables(drawableFile(sampleDrawable(), 165), "tri.ydr");
    QVERIFY2(drawables, drawables ? "" : drawables.error().message.c_str());
    QCOMPARE(drawables->size(), std::size_t(1));
    const Drawable &drawable = drawables->front();
    QCOMPARE(drawable.meshes.size(), std::size_t(1));
    const Mesh &mesh = drawable.meshes.front();
    QCOMPARE(mesh.diffuse, std::string("tri_d"));
    QCOMPARE(mesh.indices, (std::vector<std::uint32_t> { 0, 1, 2 }));
    QCOMPARE(mesh.vertices.size(), std::size_t(3));
    QCOMPARE(mesh.vertices[1].position, (std::array<float, 3> { 1, 0, 0 }));
    QCOMPARE(mesh.vertices[2].normal, (std::array<float, 3> { 0, 0, 1 }));
    QCOMPARE(mesh.vertices[1].uv, (std::array<float, 2> { 1, 0 }));
    QCOMPARE(mesh.vertices[2].uv, (std::array<float, 2> { 0, 1 }));
    QVERIFY(drawable.textures.empty());
}

void TestDrawable::embeddedTexturesAreRead()
{
    DrawableSampleOptions options;
    options.embeddedTexture = true;
    const auto drawables = readDrawables(drawableFile(sampleDrawable(options), 165), "tri.ydr");
    QVERIFY2(drawables, drawables ? "" : drawables.error().message.c_str());
    QCOMPARE(drawables->front().textures.size(), std::size_t(1));
    QCOMPARE(drawables->front().textures.front().name, std::string("tri_d"));
    QCOMPARE(drawables->front().textures.front().width, 4u);
}

void TestDrawable::yddListsEachDrawable()
{
    DrawableSampleOptions options;
    options.kind = SampleKind::Ydd;
    const auto drawables = readDrawables(drawableFile(sampleDrawable(options), 165), "pair.ydd");
    QVERIFY2(drawables, drawables ? "" : drawables.error().message.c_str());
    QCOMPARE(drawables->size(), std::size_t(2));
    QCOMPARE((*drawables)[0].nameHash, joaat("first"));
    QCOMPARE((*drawables)[1].nameHash, joaat("second"));
    QCOMPARE((*drawables)[1].meshes.size(), std::size_t(1));
}

void TestDrawable::yftUsesItsDrawable()
{
    DrawableSampleOptions options;
    options.kind = SampleKind::Yft;
    const auto drawables = readDrawables(drawableFile(sampleDrawable(options), 162), "car.yft");
    QVERIFY2(drawables, drawables ? "" : drawables.error().message.c_str());
    QCOMPARE(drawables->size(), std::size_t(1));
    QCOMPARE(drawables->front().meshes.front().vertices[2].position, (std::array<float, 3> { 0, 1, 0 }));
}

void TestDrawable::halfAndDec3nComponentsDecode()
{
    DrawableSampleOptions options;
    options.mask = 0x49;
    options.types = 0x0100A006;
    options.stride = 20;
    for (int vertex = 0; vertex < 3; ++vertex) {
        for (const float value : { 2.0f, 3.0f, 4.0f })
            append(options.vertices, std::bit_cast<std::uint32_t>(value), 4);
        append(options.vertices, 511u << 20, 4);
        append(options.vertices, 0x3800, 2);
        append(options.vertices, 0x3400, 2);
    }
    const auto drawables = readDrawables(drawableFile(sampleDrawable(options), 165), "half.ydr");
    QVERIFY2(drawables, drawables ? "" : drawables.error().message.c_str());
    const MeshVertex &vertex = drawables->front().meshes.front().vertices[0];
    QCOMPARE(vertex.position, (std::array<float, 3> { 2, 3, 4 }));
    QCOMPARE(vertex.normal, (std::array<float, 3> { 0, 0, 1 }));
    QCOMPARE(vertex.uv, (std::array<float, 2> { 0.5f, 0.25f }));
}

void TestDrawable::hostileDrawablesAreRefused()
{
    const std::vector<std::function<void(DrawableSample &)>> breakers {
        [](DrawableSample &sample) { put(sample.system, sample.vertexBuffer + 0x10, 0x50100000, 8); },
        [](DrawableSample &sample) { put(sample.system, sample.indexData + 4, 7, 2); },
        [](DrawableSample &sample) {
            put(sample.system, sample.vertexBuffer + 0x08, 8, 2);
            put(sample.system, sample.declaration + 4, 8, 2);
        },
        [](DrawableSample &sample) { put(sample.system, sample.indexBuffer + 0x08, 0xFFFFFFFF, 4); },
        [](DrawableSample &sample) { put(sample.system, sample.vertexBuffer + 0x18, 0xFFFFFFFF, 4); },
        [](DrawableSample &sample) { put(sample.system, sample.declaration + 8, 0x7755555555996990, 8); },
        [](DrawableSample &sample) { put(sample.system, 0x50, 0x5FFFFFF0, 8); },
    };
    for (std::size_t index = 0; index < breakers.size(); ++index) {
        DrawableSample sample = sampleDrawable();
        breakers[index](sample);
        const auto drawables = readDrawables(drawableFile(sample, 165), "bad.ydr");
        QVERIFY2(!drawables, qPrintable(QString::number(index)));
        QVERIFY(!drawables.error().message.empty());
    }
    QVERIFY(!readDrawables(drawableFile(sampleDrawable(), 99), "tri.ydr"));
}

void TestDrawable::otherFilesAreNotDrawables()
{
    QVERIFY(isDrawableName("Adder.YFT"));
    QVERIFY(isDrawableName("a.ydd"));
    QVERIFY(isDrawableName("a.ydr"));
    QVERIFY(!isDrawableName("a.ytd"));
    QVERIFY(!readDrawables(drawableFile(sampleDrawable(), 165), "tri.ytd"));
}

void TestDrawable::realDrawablesRead()
{
    ARCHON_REQUIRE_REAL();
    const Keys *keys = realKeys();
    QVERIFY(keys);
    const auto reader = RpfReader::open(openFileSource(*realGameDir() / "x64e.rpf"), "x64e.rpf", keys);
    QVERIFY(reader);
    std::size_t read = 0;
    std::vector<std::string> failed;
    bool adder = false;
    const std::function<void(const RpfReader &, const std::string &)> walk = [&](const RpfReader &archive, const std::string &prefix) {
        const auto &entries = archive.archive().entries();
        for (std::uint32_t index = 0; index < entries.size(); ++index) {
            const std::string name = asciiLower(entries[index].name);
            if (entries[index].kind == RpfEntryKind::Binary && name.ends_with(".rpf")) {
                if (const auto nested = archive.openNested(index))
                    walk(*nested, prefix + archive.archive().path(index) + "/");
                continue;
            }
            if (!isDrawableName(name))
                continue;
            const auto file = archive.storedFile(index);
            QVERIFY(file);
            const auto drawables = readDrawables(*file, name);
            if (!drawables) {
                failed.push_back(prefix + archive.archive().path(index) + ": " + drawables.error().message);
                continue;
            }
            ++read;
            if (name == "adder.yft") {
                adder = !drawables->front().meshes.empty() && !drawables->front().meshes.front().diffuse.empty();
            }
        }
    };
    walk(*reader, "x64e.rpf/");
    for (std::size_t i = 0; i < failed.size() && i < 20; ++i)
        qWarning("%s", failed[i].c_str());
    qInfo("x64e.rpf: %zu drawable files read, %zu failed", read, failed.size());
    QVERIFY(failed.empty());
    QVERIFY(read > 1000);
    QVERIFY(adder);
}

QTEST_APPLESS_MAIN(TestDrawable)
#include "tst_drawable.moc"
