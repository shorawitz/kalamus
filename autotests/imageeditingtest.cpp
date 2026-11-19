// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "markdowneditor.h"
#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QDir>
#include <QTemporaryDir>
#include <QScrollBar>
#include <QTest>
#include <QTextBlock>

using namespace Qt::StringLiterals;

class ImageEditingTest : public QObject
{
    Q_OBJECT

private:
    static void moveTo(MarkdownEditor &editor, const QString &needle)
    {
        QTextCursor c = editor.textCursor();
        c.setPosition(editor.markdown().indexOf(needle));
        editor.setTextCursor(c);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
    }

    static QString makeImage(const QString &path, int width, int height)
    {
        QImage image(width, height, QImage::Format_RGB32);
        image.fill(Qt::blue);
        image.save(path);
        return path;
    }

    static const BlockRenderData *dataOf(MarkdownEditor &editor, int block)
    {
        return dynamic_cast<BlockRenderData *>(editor.document()->findBlockByNumber(block).userData());
    }

    QTemporaryDir m_dir;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QDir(m_dir.path()).mkpath(u"pics"_s);
        makeImage(m_dir.filePath(u"pics/small.png"_s), 300, 100);
        makeImage(m_dir.filePath(u"pics/huge one.png"_s), 3000, 1000);
    }

    void rendersLocalImages()
    {
        MarkdownEditor editor;
        editor.resize(900, 700);
        editor.show();
        editor.setBaseDirectory(m_dir.path());
        editor.loadMarkdown(u"Text\n\n![s](pics/small.png)\n\n![h](<pics/huge one.png>)\n\nAfter"_s);
        moveTo(editor, u"After"_s);

        const auto *small = dataOf(editor, 2);
        QVERIFY(small);
        QCOMPARE(small->images.size(), 1);
        QCOMPARE(small->images.first().size, QSizeF(300, 100));

        // Wider than the text: scaled down to fit, aspect ratio kept.
        const auto *huge = dataOf(editor, 4);
        QVERIFY(huge);
        const QSizeF size = huge->images.first().size;
        QVERIFY2(size.width() < 900 && size.width() > 400, qPrintable(QString::number(size.width())));
        QVERIFY(qAbs(size.width() / size.height() - 3.0) < 0.01);

        // A tall image's line (with its spacing) is as tall as the image: no gap below.
        auto *layout = editor.document()->documentLayout();
        const qreal top = layout->blockBoundingRect(editor.document()->findBlockByNumber(2)).top();
        const qreal next = layout->blockBoundingRect(editor.document()->findBlockByNumber(3)).top();
        QVERIFY2(qAbs((next - top) - 100) < 6, qPrintable(QString::number(next - top)));
    }

    void noHorizontalScrolling()
    {
        // Everything wraps or scales to the window, so there is never anything to scroll to.
        for (int width : {700, 900, 1300}) {
            MarkdownEditor editor;
            editor.resize(width, 600);
            editor.show();
            editor.setBaseDirectory(m_dir.path());
            editor.loadMarkdown(u"Intro text\n\n![h](<pics/huge one.png>)\n\nText ![s](pics/small.png) more text after it\n\nAfter"_s);
            moveTo(editor, u"After"_s);
            QTest::qWait(50);
            QVERIFY2(editor.document()->size().width() <= editor.viewport()->width(),
                     qPrintable(u"document %1 wider than viewport %2"_s.arg(editor.document()->size().width()).arg(editor.viewport()->width())));
            QVERIFY(!editor.horizontalScrollBar()->isVisible());
        }
    }

    void cursorOnTheLineShowsSource()
    {
        MarkdownEditor editor;
        editor.setBaseDirectory(m_dir.path());
        editor.loadMarkdown(u"![s](pics/small.png)\n\nAfter"_s);
        moveTo(editor, u"pics"_s);
        QVERIFY(!dataOf(editor, 0));
        moveTo(editor, u"After"_s);
        QVERIFY(dataOf(editor, 0));
    }

    void missingAndRelativeWithoutFolder()
    {
        MarkdownEditor editor;
        editor.setBaseDirectory(m_dir.path());
        editor.loadMarkdown(u"![gone](pics/missing.png)\n\nAfter"_s);
        moveTo(editor, u"After"_s);
        QVERIFY(!dataOf(editor, 0));
        bool underlined = false;
        for (const auto &range : editor.document()->firstBlock().layout()->formats()) {
            underlined |= range.format.underlineStyle() == QTextCharFormat::WaveUnderline;
        }
        QVERIFY(underlined);
    }

    void dataUrl()
    {
        QImage image(20, 10, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        MarkdownEditor editor;
        editor.loadMarkdown(u"![d](data:image/png;base64,"_s + QString::fromLatin1(png.toBase64()) + u")\n\nAfter"_s);
        moveTo(editor, u"After"_s);
        QVERIFY(dataOf(editor, 0));
        QCOMPARE(dataOf(editor, 0)->images.first().size, QSizeF(20, 10));
    }

    void pasteSavesIntoAssets()
    {
        QTemporaryDir doc;
        MarkdownEditor editor;
        editor.setBaseDirectory(doc.path());
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(editor.insertImageData(image));
        const QStringList files = QDir(doc.filePath(u"assets"_s)).entryList({u"image-*.png"_s});
        QCOMPARE(files.size(), 1);
        QCOMPARE(editor.markdown(), u"![](assets/"_s + files.first() + u')');
    }

    void filesInsideAreLinkedOutsideAreCopied()
    {
        MarkdownEditor editor;
        editor.setBaseDirectory(m_dir.path());
        QVERIFY(editor.insertImageFiles({QUrl::fromLocalFile(m_dir.filePath(u"pics/huge one.png"_s))}));
        QCOMPARE(editor.markdown(), u"![huge one](<pics/huge one.png>)"_s);
        QVERIFY(!QFileInfo::exists(m_dir.filePath(u"assets"_s))); // nothing copied

        QTemporaryDir elsewhere;
        const QString outside = makeImage(elsewhere.filePath(u"photo.png"_s), 10, 10);
        editor.loadMarkdown(QString());
        QVERIFY(editor.insertImageFiles({QUrl::fromLocalFile(outside)}));
        QCOMPARE(editor.markdown(), u"![photo](assets/photo.png)"_s);
        QVERIFY(QFileInfo::exists(m_dir.filePath(u"assets/photo.png"_s)));

        // Same name again: a new file, not an overwrite.
        editor.loadMarkdown(QString());
        QVERIFY(editor.insertImageFiles({QUrl::fromLocalFile(outside)}));
        QCOMPARE(editor.markdown(), u"![photo](assets/photo-2.png)"_s);
        QDir(m_dir.filePath(u"assets"_s)).removeRecursively();
    }

    void unsavedDocumentAsksFirst()
    {
        MarkdownEditor editor;
        int asked = 0;
        editor.setSaveRequester([&] {
            ++asked;
            return false; // the user cancelled
        });
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(Qt::black);
        QVERIFY(!editor.insertImageData(image));
        QCOMPARE(asked, 1);
        QVERIFY(editor.markdown().isEmpty());
    }
};

QTEST_MAIN(ImageEditingTest)
#include "imageeditingtest.moc"
