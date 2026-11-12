#include "app/model_viewer.h"
#include "core/meta/name_table.h"
#include "core/ytd/texture_pixels.h"

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QTest>
#include <QWheelEvent>

using namespace archon;

class TestModelViewer : public QObject {
    Q_OBJECT
private slots:
    void framingCoversVertexExtents();
    void orbitPanZoomChangeCamera();
    void missingTexturesAreCounted();
    void yddListSwitchesDrawable();
    void rendersPixels();
};

namespace {

Mesh triangles(int count, std::string diffuse)
{
    Mesh mesh;
    for (int index = 0; index < count; ++index) {
        const float x = static_cast<float>(index);
        mesh.vertices.push_back(MeshVertex { { x - 1, -1, 0 }, { 0, 0, 1 }, { 0, 0 } });
        mesh.vertices.push_back(MeshVertex { { x + 3, -1, 0 }, { 0, 0, 1 }, { 1, 0 } });
        mesh.vertices.push_back(MeshVertex { { x - 1, 2, 1 }, { 0, 0, 1 }, { 0, 1 } });
        for (std::uint32_t corner = 0; corner < 3; ++corner)
            mesh.indices.push_back(static_cast<std::uint32_t>(index) * 3 + corner);
    }
    mesh.diffuse = std::move(diffuse);
    return mesh;
}

Drawable drawable(int triangleCount, std::string diffuse, std::uint32_t hash = 0)
{
    Drawable result;
    result.nameHash = hash;
    result.meshes.push_back(triangles(triangleCount, std::move(diffuse)));
    return result;
}

void send(QWidget *view, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, at, view->mapToGlobal(at), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(view, &event);
}

}

void TestModelViewer::framingCoversVertexExtents()
{
    const Camera camera = framing(drawable(1, ""));
    QCOMPARE(camera.target, QVector3D(1, 0.5f, 0.5f));
    const float radius = QVector3D(2, 1.5f, 0.5f).length();
    QVERIFY(camera.distance > radius);
    QVERIFY(camera.distance < radius * 4);
}

void TestModelViewer::orbitPanZoomChangeCamera()
{
    ModelViewer viewer(QStringLiteral("tri.ydr"), { drawable(1, "") }, {}, NameTable());
    QCOMPARE(viewer.windowTitle(), QStringLiteral("tri.ydr - Model viewer"));
    if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
        QSKIP("the offscreen platform cannot host OpenGL widgets");
    auto *view = viewer.findChild<ModelView *>(QStringLiteral("modelView"));
    QVERIFY(view);
    const Camera start = view->camera();
    send(view, QEvent::MouseButtonPress, { 50, 50 }, Qt::LeftButton, Qt::LeftButton);
    send(view, QEvent::MouseMove, { 90, 70 }, Qt::NoButton, Qt::LeftButton);
    send(view, QEvent::MouseButtonRelease, { 90, 70 }, Qt::LeftButton, Qt::NoButton);
    QVERIFY(view->camera().yaw != start.yaw);
    QVERIFY(view->camera().pitch != start.pitch);
    QCOMPARE(view->camera().target, start.target);
    send(view, QEvent::MouseButtonPress, { 50, 50 }, Qt::RightButton, Qt::RightButton);
    send(view, QEvent::MouseMove, { 80, 50 }, Qt::NoButton, Qt::RightButton);
    send(view, QEvent::MouseButtonRelease, { 80, 50 }, Qt::RightButton, Qt::NoButton);
    QVERIFY(view->camera().target != start.target);
    QWheelEvent wheel({ 50, 50 }, view->mapToGlobal(QPointF(50, 50)), {}, { 0, 120 }, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view, &wheel);
    QVERIFY(view->camera().distance < start.distance);
    send(view, QEvent::MouseButtonDblClick, { 50, 50 }, Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(view->camera().target, start.target);
    QCOMPARE(view->camera().distance, start.distance);
}

void TestModelViewer::missingTexturesAreCounted()
{
    Drawable model = drawable(2, "found");
    model.meshes.push_back(triangles(1, "nope"));
    model.meshes.push_back(triangles(1, "NOPE"));
    model.meshes.push_back(triangles(1, ""));
    std::vector<Texture> external { rgbaTexture("Found", 2, 2, std::vector<std::byte>(16, std::byte { 200 })) };
    ModelViewer viewer(QStringLiteral("tri.ydr"), { model }, external, NameTable());
    QCOMPARE(viewer.findChild<QLabel *>(QStringLiteral("modelStatus"))->text(), QStringLiteral("4 meshes, 5 triangles, 1 texture not found"));
    ModelViewer complete(QStringLiteral("tri.ydr"), { drawable(1, "found") }, external, NameTable());
    QCOMPARE(complete.findChild<QLabel *>(QStringLiteral("modelStatus"))->text(), QStringLiteral("1 mesh, 1 triangle"));
}

void TestModelViewer::yddListSwitchesDrawable()
{
    NameTable names;
    names.add("first");
    ModelViewer viewer(QStringLiteral("pair.ydd"), { drawable(1, "", joaat("first")), drawable(3, "", joaat("second")) }, {}, names);
    auto *list = viewer.findChild<QListWidget *>(QStringLiteral("drawableList"));
    QVERIFY(list);
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(0)->text(), QStringLiteral("first"));
    QCOMPARE(list->item(1)->text(), QString::fromStdString(hashText(joaat("second"))));
    auto *status = viewer.findChild<QLabel *>(QStringLiteral("modelStatus"));
    QCOMPARE(status->text(), QStringLiteral("1 mesh, 1 triangle"));
    list->setCurrentRow(1);
    QCOMPARE(status->text(), QStringLiteral("1 mesh, 3 triangles"));
    ModelViewer single(QStringLiteral("tri.ydr"), { drawable(1, "") }, {}, NameTable());
    QVERIFY(single.findChild<QListWidget *>(QStringLiteral("drawableList"))->isHidden());
}

void TestModelViewer::rendersPixels()
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
        QSKIP("the offscreen platform cannot host OpenGL widgets");
    std::vector<Texture> external { rgbaTexture("red", 2, 2, std::vector<std::byte> { std::byte { 255 }, {}, {}, std::byte { 255 }, std::byte { 255 }, {}, {}, std::byte { 255 }, std::byte { 255 }, {}, {}, std::byte { 255 }, std::byte { 255 }, {}, {}, std::byte { 255 } }) };
    ModelViewer viewer(QStringLiteral("tri.ydr"), { drawable(1, "red") }, external, NameTable());
    viewer.resize(640, 480);
    viewer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewer));
    auto *view = viewer.findChild<ModelView *>(QStringLiteral("modelView"));
    QTRY_VERIFY_WITH_TIMEOUT(view->available() || !view->isValid(), 2000);
    if (!view->available())
        QSKIP("OpenGL 3.3 is not available here");
    const QImage frame = view->grabFramebuffer();
    int red = 0;
    for (int y = 0; y < frame.height(); y += 4)
        for (int x = 0; x < frame.width(); x += 4)
            if (const QColor pixel = frame.pixelColor(x, y); pixel.red() > 120 && pixel.green() < 60 && pixel.blue() < 60)
                ++red;
    QVERIFY2(red > 100, qPrintable(QString::number(red)));
}

QTEST_MAIN(TestModelViewer)
#include "tst_model_viewer.moc"
