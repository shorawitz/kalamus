// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exporter.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class ExporterTest : public QObject
{
    Q_OBJECT

private:
    const QString m_markdown = u"# Title\n\n**bold** ~~gone~~\n\n- [x] done\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n$E = mc^2$\n"_s;

private Q_SLOTS:
    void htmlHasGfmFeatures()
    {
        const QString html = Exporter::toHtmlFragment(m_markdown);
        QVERIFY(html.contains(u"<h1>Title</h1>"_s));
        QVERIFY(html.contains(u"<strong>bold</strong>"_s));
        QVERIFY(html.contains(u"<del>gone</del>"_s));
        QVERIFY(html.contains(u"<table>"_s));
        QVERIFY(html.contains(u"checkbox"_s));
        QVERIFY(html.contains(u"$E = mc^2$"_s)); // left for KaTeX
    }

    void pdfIsWritten()
    {
        QTemporaryDir dir;
        const QString out = dir.filePath(u"out.pdf"_s);
        QString error;
        QVERIFY2(Exporter::exportPdf(m_markdown, dir.path(), out, &error), qPrintable(error));
        QVERIFY(QFileInfo(out).size() > 1000);
    }

    void pandocDocx()
    {
        if (!Exporter::pandocAvailable()) {
            QSKIP("pandoc not installed");
        }
        QTemporaryDir dir;
        const QString out = dir.filePath(u"out.docx"_s);
        QString error;
        QVERIFY2(Exporter::exportWithPandoc(m_markdown, u"docx"_s, dir.path(), out, &error), qPrintable(error));
        QVERIFY(QFileInfo(out).size() > 1000);
    }
};

QTEST_MAIN(ExporterTest)
#include "exportertest.moc"
