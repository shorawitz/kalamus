// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imagecache.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <KLocalizedString>

using namespace Qt::StringLiterals;

ImageCache::ImageCache(QObject *parent)
    : QObject(parent)
{
}

void ImageCache::setBaseDirectory(const QString &directory)
{
    m_baseDirectory = directory;
}

QString ImageCache::baseDirectory() const
{
    return m_baseDirectory;
}

void ImageCache::clear()
{
    m_entries.clear();
}

QUrl ImageCache::resolve(const QString &destination) const
{
    QString dest = destination.trimmed();
    if (dest.startsWith(u'<') && dest.endsWith(u'>')) {
        dest = dest.mid(1, dest.size() - 2);
    }
    if (dest.isEmpty()) {
        return {};
    }
    if (dest.startsWith(u"http://"_s) || dest.startsWith(u"https://"_s) || dest.startsWith(u"data:"_s) || dest.startsWith(u"file:"_s)) {
        return QUrl(dest);
    }
    // A local path; Markdown links often percent-encode spaces.
    QString path = QUrl::fromPercentEncoding(dest.toUtf8());
    if (path.startsWith(u"~/"_s)) {
        path = QDir::homePath() + path.mid(1);
    }
    if (QDir::isRelativePath(path)) {
        if (m_baseDirectory.isEmpty()) {
            return {};
        }
        path = QDir(m_baseDirectory).filePath(path);
    }
    return QUrl::fromLocalFile(QDir::cleanPath(path));
}

ImageCache::Status ImageCache::status(const QUrl &url)
{
    if (!url.isValid()) {
        return Status::Failed;
    }
    if (const auto it = m_entries.constFind(url); it != m_entries.cend()) {
        return it->status;
    }
    Entry &entry = m_entries[url];
    if (url.scheme() == u"http"_s || url.scheme() == u"https"_s) {
        entry.status = Status::Loading;
        loadRemote(url);
    } else {
        loadLocal(url, entry);
    }
    return entry.status;
}

QImage ImageCache::image(const QUrl &url) const
{
    return m_entries.value(url).image;
}

QString ImageCache::error(const QUrl &url) const
{
    return m_entries.value(url).error;
}

QImage ImageCache::limitSize(QImage image)
{
    if (image.width() > MaxWidth) {
        image = image.scaledToWidth(MaxWidth, Qt::SmoothTransformation);
    }
    return image;
}

void ImageCache::loadLocal(const QUrl &url, Entry &entry)
{
    QImage image;
    if (url.scheme() == u"data"_s) {
        // data:image/png;base64,....
        const QString text = url.toString();
        const int comma = text.indexOf(u',');
        if (comma > 0 && text.left(comma).endsWith(u";base64"_s)) {
            image.loadFromData(QByteArray::fromBase64(text.mid(comma + 1).toLatin1()));
        }
    } else {
        QImageReader reader(url.toLocalFile());
        reader.setAutoTransform(true); // honour EXIF rotation
        const QSize size = reader.size();
        if (size.width() > MaxWidth) {
            reader.setScaledSize(size.scaled(MaxWidth, size.height(), Qt::KeepAspectRatio));
        }
        image = reader.read();
        if (image.isNull()) {
            entry.status = Status::Failed;
            entry.error = QFileInfo::exists(url.toLocalFile()) ? reader.errorString() : i18n("File not found: %1", url.toLocalFile());
            return;
        }
    }
    if (image.isNull()) {
        entry.status = Status::Failed;
        entry.error = i18n("Not an image.");
        return;
    }
    entry.status = Status::Ready;
    entry.image = limitSize(image);
}

void ImageCache::loadRemote(const QUrl &url)
{
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        Entry &entry = m_entries[url];
        QImage image;
        if (reply->error() == QNetworkReply::NoError) {
            image.loadFromData(reply->readAll());
        }
        if (image.isNull()) {
            entry.status = Status::Failed;
            entry.error = reply->error() != QNetworkReply::NoError ? reply->errorString() : i18n("Not an image.");
        } else {
            entry.status = Status::Ready;
            entry.image = limitSize(image);
        }
        Q_EMIT imageReady(url);
    });
}
