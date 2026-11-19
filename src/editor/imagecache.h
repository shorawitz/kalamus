// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;

/**
 * Loads the images a document refers to. Local files and data: URLs load at once;
 * http(s) images load in the background and imageReady() fires when they arrive.
 */
class ImageCache : public QObject
{
    Q_OBJECT

public:
    enum class Status {
        Loading,
        Ready,
        Failed,
    };

    /// Images wider than this are scaled down when loaded, to keep memory in check.
    static constexpr int MaxWidth = 2400;

    explicit ImageCache(QObject *parent = nullptr);

    /// Folder that relative image paths are resolved against (the document's folder).
    void setBaseDirectory(const QString &directory);
    QString baseDirectory() const;

    /// The location a Markdown link destination refers to; invalid if it can't be resolved.
    QUrl resolve(const QString &destination) const;

    /// Starts loading on first use.
    Status status(const QUrl &url);
    QImage image(const QUrl &url) const;
    QString error(const QUrl &url) const;

    /// Forget everything (e.g. after files changed on disk).
    void clear();

    /// Whether http(s) images are fetched; when off they show an explanation instead.
    void setLoadRemote(bool load);
    bool loadsRemote() const;

Q_SIGNALS:
    void imageReady(const QUrl &url);

private:
    struct Entry {
        Status status = Status::Loading;
        QImage image;
        QString error;
    };

    void loadLocal(const QUrl &url, Entry &entry);
    void loadRemote(const QUrl &url);
    static QImage limitSize(QImage image);

    QString m_baseDirectory;
    QHash<QUrl, Entry> m_entries;
    QNetworkAccessManager *m_network = nullptr;
    bool m_loadRemote = true;
};
