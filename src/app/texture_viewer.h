#pragma once

#include "core/ytd/texture_dictionary.h"

#include <QDialog>

#include <functional>

class QLabel;
class QListWidget;
class QPushButton;

namespace archon {

class TextureViewer : public QDialog {
    Q_OBJECT
public:
    using Save = std::function<Result<void>(const std::vector<std::byte> &file)>;

    TextureViewer(const QString &title, std::vector<Texture> textures, bool editable, Save save, QWidget *parent = nullptr);

    bool exportSelected(const QString &file);
    bool exportAll(const QString &folder, bool png);
    void addFiles(const QStringList &files);
    void replaceSelected(const QString &file);
    void renameSelected(const QString &name);
    void removeSelected();
    bool save();
    void reject() override;

private:
    void fill(int row);
    void showSelected();
    void updateButtons();
    bool warnIfFailed(const Result<void> &result);
    Result<Texture> load(const QString &file) const;
    bool exportTexture(const Texture &texture, const QString &file);
    const Texture *selected() const;

    std::vector<Texture> textures_;
    bool editable_;
    bool modified_ = false;
    Save save_;
    QListWidget *list_ = nullptr;
    QLabel *preview_ = nullptr;
    QLabel *info_ = nullptr;
    QPushButton *export_ = nullptr;
    QPushButton *exportAll_ = nullptr;
    QPushButton *add_ = nullptr;
    QPushButton *replace_ = nullptr;
    QPushButton *rename_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPushButton *saveButton_ = nullptr;
};

}
