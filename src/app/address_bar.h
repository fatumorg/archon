#pragma once

#include <QFrame>

class QHBoxLayout;
class QLineEdit;
class QStackedLayout;

namespace archon {

class AddressBar : public QFrame {
    Q_OBJECT
public:
    explicit AddressBar(QWidget *parent = nullptr);

    void setPath(const QString &path);
    void setRootName(const QString &name);
    void beginEdit();

signals:
    void pathRequested(const QString &path);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void endEdit();

    QString path_;
    QString rootName_ = QStringLiteral("GTA V");
    QStackedLayout *pages_;
    QWidget *crumbs_;
    QHBoxLayout *crumbLayout_;
    QLineEdit *edit_;
};

}
