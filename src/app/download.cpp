#include "app/download.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>

namespace archon {

namespace {

constexpr int downloadTimeoutMs = 30000;

}

Result<QByteArray> download(QWidget *parent, const QString &label, const QUrl &url, qint64 limit)
{
    QNetworkAccessManager network;
    QNetworkRequest request { url };
    request.setTransferTimeout(downloadTimeoutMs);
    QNetworkReply *reply = network.get(request);
    QProgressDialog progress(label, QObject::tr("Cancel"), 0, 0, parent);
    progress.setWindowTitle(QStringLiteral("ARCHON"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply, limit, &progress](qint64 received, qint64 total) {
        if (received > limit) {
            reply->abort();
            return;
        }
        if (total > 0) {
            progress.setMaximum(static_cast<int>(total / 1024));
            progress.setValue(static_cast<int>(received / 1024));
        }
    });
    progress.show();
    loop.exec();
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return std::unexpected(Error { "download", 0, reply->errorString().toStdString() });
    return reply->readAll();
}

}
