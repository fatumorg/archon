#pragma once

#include <QMainWindow>

namespace archon {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
};

}
