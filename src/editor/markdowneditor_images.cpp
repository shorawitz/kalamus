// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

// Adding images to a document: paste, drag and drop, and Insert Image.

#include "markdowneditor.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>

#include <KLocalizedString>
#include <KMessageBox>

using namespace Qt::StringLiterals;

namespace
{
const auto kAssets = u"assets"_s;

/// A Markdown link destination; angle brackets when the path has spaces or parentheses.
QString destination(const QString &relativePath)
{
    const bool needsBrackets = relativePath.contains(u' ') || relativePath.contains(u'(') || relativePath.contains(u')');
    return needsBrackets ? u'<' + relativePath + u'>' : relativePath;
}

/// name.png, or name-2.png, name-3.png ... if that is taken.
QString uniquePath(const QDir &dir, const QString &baseName, const QString &suffix)
{
    QString candidate = dir.filePath(baseName + u'.' + suffix);
    for (int n = 2; QFileInfo::exists(candidate); ++n) {
        candidate = dir.filePath(baseName + u'-' + QString::number(n) + u'.' + suffix);
    }
    return candidate;
}

QString altText(const QString &path)
{
    return QFileInfo(path).completeBaseName().replace(u'[', u'(').replace(u']', u')');
}
} // namespace

void MarkdownEditor::setSaveRequester(const std::function<bool()> &requester)
{
    m_saveRequester = requester;
}

bool MarkdownEditor::isImageFile(const QString &path)
{
    return QFileInfo(path).isFile() && !QImageReader::imageFormat(path).isEmpty();
}

bool MarkdownEditor::insertImageFiles(const QList<QUrl> &files)
{
    if (baseDirectory().isEmpty() && !(m_saveRequester && m_saveRequester())) {
        return false;
    }
    const QDir base(baseDirectory());
    QStringList links;
    for (const QUrl &url : files) {
        const QString path = url.toLocalFile();
        if (!isImageFile(path)) {
            continue;
        }
        QString target = QFileInfo(path).absoluteFilePath();
        const QString relative = base.relativeFilePath(target);
        if (relative.startsWith(u"../"_s) || QDir::isAbsolutePath(relative)) {
            // Outside the document's folder: copy it into assets/ so the document stays portable.
            if (!base.mkpath(kAssets)) {
                KMessageBox::error(this, i18n("Could not create the folder %1.", base.filePath(kAssets)));
                return false;
            }
            const QFileInfo info(path);
            target = uniquePath(QDir(base.filePath(kAssets)), info.completeBaseName(), info.suffix());
            if (!QFile::copy(path, target)) {
                KMessageBox::error(this, i18n("Could not copy %1 to %2.", path, target));
                return false;
            }
        }
        links.append(u"!["_s + altText(path) + u"]("_s + destination(base.relativeFilePath(target)) + u')');
    }
    if (links.isEmpty()) {
        return false;
    }
    textCursor().insertText(links.join(u"\n\n"_s));
    return true;
}

bool MarkdownEditor::insertImageData(const QImage &image)
{
    if (image.isNull() || (baseDirectory().isEmpty() && !(m_saveRequester && m_saveRequester()))) {
        return false;
    }
    const QDir base(baseDirectory());
    if (!base.mkpath(kAssets)) {
        KMessageBox::error(this, i18n("Could not create the folder %1.", base.filePath(kAssets)));
        return false;
    }
    const QString name = u"image-"_s + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s);
    const QString target = uniquePath(QDir(base.filePath(kAssets)), name, u"png"_s);
    if (!image.save(target, "PNG")) {
        KMessageBox::error(this, i18n("Could not save the image to %1.", target));
        return false;
    }
    textCursor().insertText(u"![]("_s + destination(base.relativeFilePath(target)) + u')');
    return true;
}

bool MarkdownEditor::canInsertFromMimeData(const QMimeData *source) const
{
    if (source->hasImage()) {
        return true;
    }
    if (source->hasUrls()) {
        const auto urls = source->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile() && isImageFile(url.toLocalFile())) {
                return true;
            }
        }
    }
    return QTextEdit::canInsertFromMimeData(source);
}

void MarkdownEditor::insertFromMimeData(const QMimeData *source)
{
    // Files first: a file manager also offers the file's path as text.
    if (source->hasUrls()) {
        QList<QUrl> images;
        const auto urls = source->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile() && isImageFile(url.toLocalFile())) {
                images.append(url);
            }
        }
        if (!images.isEmpty()) {
            insertImageFiles(images);
            return;
        }
    }
    // Image data (a screenshot, an image copied in a browser). Some apps add text too;
    // only real text should win over the picture.
    if (source->hasImage() && source->text().trimmed().isEmpty()) {
        insertImageData(qvariant_cast<QImage>(source->imageData()));
        return;
    }
    QTextEdit::insertFromMimeData(source); // plain text (rich text is off)
}
