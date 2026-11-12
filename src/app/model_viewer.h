#pragma once

#include "core/drawable/drawable.h"
#include "core/meta/name_table.h"

#include <QDialog>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointF>
#include <QVector3D>

#include <map>
#include <memory>

class QLabel;
class QListWidget;

namespace archon {

struct Camera {
    QVector3D target;
    float distance = 1;
    float yaw = 0;
    float pitch = 0;
};

Camera framing(const Drawable &drawable);

class ModelView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    ModelView(std::vector<Texture> textures, QWidget *parent = nullptr);
    ~ModelView() override;

    void show(const Drawable &drawable);
    Camera camera() const;
    bool available() const;

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct GpuMesh {
        std::unique_ptr<QOpenGLVertexArrayObject> vertexArray;
        std::unique_ptr<QOpenGLBuffer> vertices;
        std::unique_ptr<QOpenGLBuffer> indices;
        int indexCount = 0;
        QOpenGLTexture *texture = nullptr;
    };

    void upload();
    void release();
    QOpenGLTexture *texture(const std::string &name);

    std::vector<Texture> textures_;
    const Drawable *drawable_ = nullptr;
    Camera camera_;
    Camera home_;
    QPointF last_;
    bool available_ = false;
    bool dirty_ = false;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::vector<GpuMesh> meshes_;
    std::map<std::string, std::unique_ptr<QOpenGLTexture>> uploaded_;
    std::unique_ptr<QOpenGLTexture> grey_;
};

class ModelViewer : public QDialog {
    Q_OBJECT
public:
    ModelViewer(const QString &name, std::vector<Drawable> drawables, std::vector<Texture> textures, const NameTable &names, QWidget *parent = nullptr);

private:
    void showDrawable(int index);

    std::vector<Drawable> drawables_;
    std::vector<std::string> available_;
    ModelView *view_ = nullptr;
    QListWidget *list_ = nullptr;
    QLabel *status_ = nullptr;
};

}
