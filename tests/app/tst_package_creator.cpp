#include "app/package_creator_dialog.h"
#include "support/modal_driver.h"
#include "support/temp_files.h"
#include "support/test_bytes.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>

using namespace archon;
using namespace archon::test;

class TestPackageCreator : public QObject {
    Q_OBJECT
private slots:
    void init();
    void draftFollowsFields();
    void itemsTableAndRemove();
    void createsReadablePackage();
    void validationErrorIsShown();

private:
    void fill(PackageCreatorDialog &dialog)
    {
        dialog.findChild<QLineEdit *>(QStringLiteral("packageName"))->setText(QStringLiteral("Better Adder"));
        dialog.findChild<QSpinBox *>(QStringLiteral("versionMajor"))->setValue(3);
        dialog.findChild<QSpinBox *>(QStringLiteral("versionMinor"))->setValue(1);
        dialog.findChild<QLineEdit *>(QStringLiteral("versionTag"))->setText(QStringLiteral("Beta"));
        dialog.findChild<QLineEdit *>(QStringLiteral("authorName"))->setText(QStringLiteral("Me"));
        dialog.findChild<QPlainTextEdit *>(QStringLiteral("description"))->setPlainText(QStringLiteral("Faster car."));
        dialog.findChild<QCheckBox *>(QStringLiteral("blackHeaderText"))->setChecked(true);
    }
    QString source(const std::string &name, std::string_view content)
    {
        const std::filesystem::path file = pathOf(*directory_) / "sources" / name;
        writeFile(file, content);
        return QString::fromStdString(file.string());
    }

    std::unique_ptr<QTemporaryDir> directory_;
};

void TestPackageCreator::init()
{
    directory_ = std::make_unique<QTemporaryDir>();
}

void TestPackageCreator::draftFollowsFields()
{
    PackageCreatorDialog dialog;
    QCOMPARE(dialog.windowTitle(), QStringLiteral("Package Creator"));
    QCOMPARE(dialog.draft().headerColor, 0xFF23366Au);
    QCOMPARE(dialog.draft().iconColor, 0xFF3B5998u);
    fill(dialog);
    const PackageDraft draft = dialog.draft();
    QCOMPARE(draft.name, std::string("Better Adder"));
    QCOMPARE(draft.major, 3);
    QCOMPARE(draft.minor, 1);
    QCOMPARE(draft.tag, std::string("Beta"));
    QCOMPARE(draft.author, std::string("Me"));
    QCOMPARE(draft.description, std::string("Faster car."));
    QVERIFY(draft.blackHeaderText);
}

void TestPackageCreator::itemsTableAndRemove()
{
    PackageCreatorDialog dialog;
    dialog.addFiles({ source("adder.yft", "y"), source("adder.ytd", "t") }, QStringLiteral("x64e.rpf/levels/gta5/vehicles.rpf"));
    dialog.addDelete(QStringLiteral("x64e.rpf/levels/gta5/vehicles.rpf/old.ytd"));
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("contentTable"));
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("Add"));
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("x64e.rpf/levels/gta5/vehicles.rpf/adder.yft"));
    QCOMPARE(table->item(2, 0)->text(), QStringLiteral("Delete"));
    table->selectRow(1);
    dialog.removeSelected();
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(dialog.draft().items.size(), std::size_t(2));
    QCOMPARE(dialog.draft().items[1].kind, PackageItem::Kind::Delete);
}

void TestPackageCreator::createsReadablePackage()
{
    PackageCreatorDialog dialog;
    fill(dialog);
    dialog.addFiles({ source("mod.asi", "asi") }, QString());
    const QString file = directory_->path() + QStringLiteral("/out.oiv");
    QVERIFY(dialog.createPackage(file));
    const auto zip = ZipArchive::open(readFile(file.toStdString()));
    QVERIFY(zip);
    const auto package = readOivPackage(*zip);
    QVERIFY2(package, package ? "" : package.error().message.c_str());
    QCOMPARE(package->name, std::string("Better Adder"));
    QVERIFY(package->blackHeaderText);
    QCOMPARE(package->contents[0].commands[0].path, std::string("mod.asi"));
}

void TestPackageCreator::validationErrorIsShown()
{
    PackageCreatorDialog dialog;
    ModalDriver driver({ QStringLiteral("OK") });
    QVERIFY(!dialog.createPackage(directory_->path() + QStringLiteral("/out.oiv")));
    QVERIFY(driver.done());
    QCOMPARE(driver.seen, QStringList { QStringLiteral("The package needs a name.") });
    QVERIFY(!QFile::exists(directory_->path() + QStringLiteral("/out.oiv")));
}

QTEST_MAIN(TestPackageCreator)
#include "tst_package_creator.moc"
