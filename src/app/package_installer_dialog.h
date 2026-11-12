#pragma once

#include "core/oiv/oiv_installer.h"

#include <QDialog>

#include <optional>

class QButtonGroup;
class QLabel;
class QStackedWidget;

namespace archon {

class PackageInstallerDialog : public QDialog {
    Q_OBJECT
public:
    PackageInstallerDialog(const std::filesystem::path &package, std::filesystem::path root, std::shared_ptr<const Keys> keys, std::filesystem::path store, QWidget *parent = nullptr);

    bool isValid() const;
    QString errorText() const;
    void install(InstallTarget target);

private:
    QWidget *buildHeader();
    QWidget *buildDescriptionStep();
    QWidget *buildTargetStep();
    QWidget *buildResultStep();

    std::filesystem::path root_;
    std::shared_ptr<const Keys> keys_;
    std::filesystem::path store_;
    std::optional<ZipArchive> zip_;
    std::optional<OivPackage> package_;
    QString error_;
    QStackedWidget *steps_ = nullptr;
    QButtonGroup *contents_ = nullptr;
    QLabel *result_ = nullptr;
};

}
