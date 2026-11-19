// SPDX-FileCopyrightText: 2026 David Christensen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exporter.h"

#include <QFile>
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

    void htmlKeepsLatexIntact()
    {
        const QString md = u"Set $\\{x \\mid x > 0\\}$ and `$code$`.\n\n$$\n\\frac{a}{b} \\\\ c\n$$\n\nTwo: $a$ and $b$.\n"_s;
        const QString html = Exporter::toHtmlDocument(md, u"t"_s);
        QVERIFY2(html.contains(u"<span class=\"math\">$\\{x \\mid x &gt; 0\\}$</span>"_s), qPrintable(html));
        QVERIFY(html.contains(u"<div class=\"math\">$$\\frac{a}{b} \\\\ c$$</div>"_s));
        QVERIFY(html.contains(u"<code>$code$</code>"_s));
        QVERIFY(html.contains(u"<span class=\"math\">$a$</span> and <span class=\"math\">$b$</span>"_s));
        QVERIFY(!html.contains(u"KALAMUSMATH"_s));
    }

    void pdfWithMath()
    {
        QTemporaryDir dir;
        QString error;
        const QString plain = dir.filePath(u"plain.pdf"_s);
        const QString math = dir.filePath(u"math.pdf"_s);
        QVERIFY(Exporter::exportPdf(u"Text E = mc2\n"_s, dir.path(), plain, &error));
        QVERIFY2(Exporter::exportPdf(u"Text $E = mc^2$\n\n$$\\int_0^1 x\\,dx$$\n"_s, dir.path(), math, &error), qPrintable(error));
        QVERIFY(QFileInfo(math).size() > QFileInfo(plain).size()); // images embedded
        if (const QString keep = qEnvironmentVariable("KALAMUS_KEEP_EXPORTS"); !keep.isEmpty()) {
            QFile::copy(math, keep + u"/math.pdf"_s); // for looking at the result by eye
        }
    }

    void pdfFitsLargeImages()
    {
        QTemporaryDir dir;
        QImage image(3000, 500, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(dir.filePath(u"wide pic.png"_s)));
        const QString out = dir.filePath(u"images.pdf"_s);
        QString error;
        const QString plain = dir.filePath(u"plain.pdf"_s);
        QVERIFY(Exporter::exportPdf(u"Before\n\nAfter\n"_s, dir.path(), plain, &error));
        QVERIFY2(Exporter::exportPdf(u"Before\n\n![wide](<wide pic.png>)\n\n![web](https://example.com/x.png)\n\nAfter\n"_s, dir.path(), out, &error),
                 qPrintable(error));
        QVERIFY(QFileInfo(out).size() > QFileInfo(plain).size() + 1000); // the picture is embedded
        if (const QString keep = qEnvironmentVariable("KALAMUS_KEEP_EXPORTS"); !keep.isEmpty()) {
            QFile::copy(out, keep + u"/images.pdf"_s);
        }
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
