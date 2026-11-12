#include "app/model_viewer.h"

#include "core/io/ascii.h"
#include "core/ytd/texture_pixels.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace archon {

namespace {

constexpr float fieldOfView = 45.0f;
constexpr float homeYaw = -135.0f;
constexpr float homePitch = 20.0f;
constexpr float orbitSpeed = 0.5f;
constexpr float panSpeed = 0.002f;
constexpr float minimumRadius = 0.01f;
constexpr float maximumRadius = 1.0e6f;

constexpr const char *vertexShader = R"(#version 330 core
uniform mat4 transform;
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
out vec3 surfaceNormal;
out vec2 surfaceUv;
void main()
{
    gl_Position = transform * vec4(position, 1.0);
    surfaceNormal = normal;
    surfaceUv = uv;
}
)";

constexpr const char *fragmentShader = R"(#version 330 core
uniform sampler2D diffuse;
in vec3 surfaceNormal;
in vec2 surfaceUv;
out vec4 colour;
void main()
{
    vec4 texel = texture(diffuse, surfaceUv);
    if (texel.a < 0.3)
        discard;
    float light = 1.0;
    if (length(surfaceNormal) > 0.5)
        light = 0.35 + 0.65 * abs(dot(normalize(surfaceNormal), normalize(vec3(0.4, 0.3, 0.85))));
    colour = vec4(texel.rgb * light, 1.0);
}
)";

QString counted(std::size_t count, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(count).arg(QLatin1StringView(count == 1 ? one : many));
}

}

Camera framing(const Drawable &drawable)
{
    std::array<double, 3> low { 0, 0, 0 };
    std::array<double, 3> high { 0, 0, 0 };
    bool any = false;
    for (const Mesh &mesh : drawable.meshes) {
        for (const MeshVertex &vertex : mesh.vertices) {
            if (!std::ranges::all_of(vertex.position, [](float value) { return std::isfinite(value); }))
                continue;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                low[axis] = any ? std::min(low[axis], double(vertex.position[axis])) : vertex.position[axis];
                high[axis] = any ? std::max(high[axis], double(vertex.position[axis])) : vertex.position[axis];
            }
            any = true;
        }
    }
    const double radius = std::clamp(std::hypot(high[0] - low[0], high[1] - low[1], high[2] - low[2]) / 2, double(minimumRadius), double(maximumRadius));
    const QVector3D target(float((low[0] + high[0]) / 2), float((low[1] + high[1]) / 2), float((low[2] + high[2]) / 2));
    return Camera { target, float(radius / std::sin(qDegreesToRadians(fieldOfView / 2)) * 1.1), homeYaw, homePitch };
}

ModelView::ModelView(std::vector<Texture> textures, QWidget *parent)
    : QOpenGLWidget(parent)
    , textures_(std::move(textures))
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    setFormat(format);
    setMinimumSize(320, 240);
}

ModelView::~ModelView()
{
    if (available_) {
        makeCurrent();
        release();
        grey_.reset();
        program_.reset();
        doneCurrent();
    }
}

void ModelView::show(const Drawable &drawable)
{
    drawable_ = &drawable;
    home_ = framing(drawable);
    camera_ = home_;
    dirty_ = true;
    update();
}

Camera ModelView::camera() const
{
    return camera_;
}

bool ModelView::available() const
{
    return available_;
}

void ModelView::initializeGL()
{
    const QSurfaceFormat actual = context()->format();
    if (actual.version() < qMakePair(3, 3) || actual.profile() != QSurfaceFormat::CoreProfile || !initializeOpenGLFunctions())
        return;
    program_ = std::make_unique<QOpenGLShaderProgram>();
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader) || !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader) || !program_->link()) {
        program_.reset();
        return;
    }
    QImage grey(1, 1, QImage::Format_RGBA8888);
    grey.fill(QColor(160, 160, 160));
    grey_ = std::make_unique<QOpenGLTexture>(grey);
    if (!grey_->isCreated()) {
        grey_.reset();
        program_.reset();
        return;
    }
    available_ = true;
    dirty_ = true;
}

QOpenGLTexture *ModelView::texture(const std::string &name)
{
    const std::string key = asciiLower(name);
    if (const auto found = uploaded_.find(key); found != uploaded_.end())
        return found->second ? found->second.get() : grey_.get();
    std::unique_ptr<QOpenGLTexture> made;
    const auto named = [&key](const Texture &candidate) { return asciiLower(candidate.name) == key; };
    const auto find = [&named](const std::vector<Texture> &list) -> const Texture * {
        const auto found = std::ranges::find_if(list, named);
        return found == list.end() ? nullptr : &*found;
    };
    const Texture *source = find(drawable_->textures);
    if (source == nullptr)
        source = find(textures_);
    if (source != nullptr) {
        if (const auto rgba = decodeRgba(*source, 0)) {
            const QImage image(reinterpret_cast<const uchar *>(rgba->data()), static_cast<int>(source->width), static_cast<int>(source->height), QImage::Format_RGBA8888);
            made = std::make_unique<QOpenGLTexture>(image.copy());
            made->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            made->setMagnificationFilter(QOpenGLTexture::Linear);
            made->setWrapMode(QOpenGLTexture::Repeat);
            if (!made->isCreated())
                made.reset();
        }
    }
    QOpenGLTexture *result = made ? made.get() : grey_.get();
    uploaded_.emplace(key, std::move(made));
    return result;
}

void ModelView::release()
{
    meshes_.clear();
    uploaded_.clear();
}

void ModelView::upload()
{
    release();
    dirty_ = false;
    if (drawable_ == nullptr)
        return;
    for (const Mesh &mesh : drawable_->meshes) {
        GpuMesh gpu;
        gpu.vertexArray = std::make_unique<QOpenGLVertexArrayObject>();
        gpu.vertexArray->create();
        gpu.vertexArray->bind();
        gpu.vertices = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
        gpu.vertices->create();
        gpu.vertices->bind();
        gpu.vertices->allocate(mesh.vertices.data(), static_cast<int>(mesh.vertices.size() * sizeof(MeshVertex)));
        gpu.indices = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer);
        gpu.indices->create();
        gpu.indices->bind();
        gpu.indices->allocate(mesh.indices.data(), static_cast<int>(mesh.indices.size() * sizeof(std::uint32_t)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, uv)));
        gpu.vertexArray->release();
        gpu.indexCount = static_cast<int>(mesh.indices.size());
        gpu.texture = texture(mesh.diffuse);
        meshes_.push_back(std::move(gpu));
    }
}

void ModelView::paintGL()
{
    if (!available_) {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0x30, 0x30, 0x30));
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("OpenGL 3.3 is not available."));
        return;
    }
    if (dirty_)
        upload();
    glClearColor(0.19f, 0.19f, 0.19f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    QMatrix4x4 projection;
    projection.perspective(fieldOfView, static_cast<float>(width()) / static_cast<float>(std::max(1, height())), camera_.distance * 0.01f, camera_.distance * 100.0f);
    const float yaw = qDegreesToRadians(camera_.yaw);
    const float pitch = qDegreesToRadians(camera_.pitch);
    const QVector3D eye = camera_.target + camera_.distance * QVector3D(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
    QMatrix4x4 view;
    view.lookAt(eye, camera_.target, QVector3D(0, 0, 1));
    program_->bind();
    program_->setUniformValue("transform", projection * view);
    program_->setUniformValue("diffuse", 0);
    glActiveTexture(GL_TEXTURE0);
    for (GpuMesh &mesh : meshes_) {
        mesh.texture->bind(0);
        mesh.vertexArray->bind();
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        mesh.vertexArray->release();
    }
    program_->release();
}

void ModelView::mousePressEvent(QMouseEvent *event)
{
    last_ = event->position();
}

void ModelView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF delta = event->position() - last_;
    last_ = event->position();
    if (event->buttons() & Qt::LeftButton) {
        camera_.yaw -= static_cast<float>(delta.x()) * orbitSpeed;
        camera_.pitch = std::clamp(camera_.pitch + static_cast<float>(delta.y()) * orbitSpeed, -89.0f, 89.0f);
    } else if (event->buttons() & (Qt::RightButton | Qt::MiddleButton)) {
        const float yaw = qDegreesToRadians(camera_.yaw);
        const float pitch = qDegreesToRadians(camera_.pitch);
        const QVector3D forward = -QVector3D(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
        const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 0, 1)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
        const float scale = camera_.distance * panSpeed;
        camera_.target += (-right * static_cast<float>(delta.x()) + up * static_cast<float>(delta.y())) * scale;
    } else {
        return;
    }
    update();
}

void ModelView::mouseDoubleClickEvent(QMouseEvent *)
{
    camera_ = home_;
    update();
}

void ModelView::wheelEvent(QWheelEvent *event)
{
    camera_.distance = std::max(minimumRadius, camera_.distance * std::pow(0.9f, static_cast<float>(event->angleDelta().y()) / 120.0f));
    update();
}

ModelViewer::ModelViewer(const QString &name, std::vector<Drawable> drawables, std::vector<Texture> textures, const NameTable &names, QWidget *parent)
    : QDialog(parent)
    , drawables_(std::move(drawables))
{
    setWindowTitle(tr("%1 - Model viewer").arg(name));
    resize(1000, 700);
    for (const Texture &texture : textures)
        available_.push_back(asciiLower(texture.name));

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("drawableList"));
    list_->setMaximumWidth(260);
    for (const Drawable &drawable : drawables_)
        list_->addItem(QString::fromStdString(names.name(drawable.nameHash)));
    list_->setVisible(drawables_.size() > 1);
    QWidget *display = nullptr;
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        auto *message = new QLabel(tr("OpenGL 3.3 is not available."), this);
        message->setAlignment(Qt::AlignCenter);
        display = message;
    } else {
        view_ = new ModelView(std::move(textures), this);
        view_->setObjectName(QStringLiteral("modelView"));
        display = view_;
    }
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("modelStatus"));
    auto *close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    auto *body = new QHBoxLayout;
    body->addWidget(list_);
    body->addWidget(display, 1);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(status_, 1);
    buttons->addWidget(close);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addLayout(buttons);

    connect(list_, &QListWidget::currentRowChanged, this, &ModelViewer::showDrawable);
    if (!drawables_.empty())
        list_->setCurrentRow(0);
}

void ModelViewer::showDrawable(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= drawables_.size())
        return;
    const Drawable &drawable = drawables_[static_cast<std::size_t>(index)];
    if (view_ != nullptr)
        view_->show(drawable);
    std::size_t triangles = 0;
    std::set<std::string> missing;
    for (const Mesh &mesh : drawable.meshes) {
        triangles += mesh.indices.size() / 3;
        const std::string key = asciiLower(mesh.diffuse);
        const auto named = [&key](const Texture &texture) { return asciiLower(texture.name) == key; };
        if (!key.empty() && std::ranges::find(available_, key) == available_.end() && std::ranges::none_of(drawable.textures, named))
            missing.insert(key);
    }
    QString text = counted(drawable.meshes.size(), "mesh", "meshes") + QStringLiteral(", ") + counted(triangles, "triangle", "triangles");
    if (!missing.empty())
        text += QStringLiteral(", ") + counted(missing.size(), "texture not found", "textures not found");
    status_->setText(text);
}

}
